#pragma once

/// @file StreamingManager.hpp
/// @brief LOD ストリーミングマネージャー
/// @details カメラ距離ベースの優先度付きストリーミングキュー。
///          バックグラウンドでミップレベルをロードし、
///          メモリバジェット管理とフォールバックアセットを提供する。

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <mitiru/resource/AssetCache.hpp>
#include <mitiru/resource/ThreadPool.hpp>

namespace mitiru::resource
{

/// @brief ストリーミングリクエストの状態
enum class StreamingState : std::uint8_t
{
	Pending,    ///< キュー待ち
	Loading,    ///< ロード中
	Loaded,     ///< ロード完了
	Failed,     ///< ロード失敗
	Evicted     ///< メモリバジェットにより追い出し済み
};

/// @brief ストリーミングリクエスト
/// @tparam T アセット型
template <typename T>
struct StreamingRequest
{
	std::string id;                        ///< アセットID
	std::string path;                      ///< ファイルパス
	float distanceFromCamera = 0.0f;       ///< カメラからの距離
	std::uint8_t desiredMipLevel = 0;      ///< 要求ミップレベル（0 = 最高品質）
	StreamingState state = StreamingState::Pending; ///< 現在の状態

	/// @brief 優先度を計算する（距離が近いほど高優先度）
	/// @return 優先度スコア（大きいほど高優先度）
	[[nodiscard]] float priority() const noexcept
	{
		/// 距離 0 で最高優先度、距離が離れるほど低優先度
		/// ミップレベル 0（最高品質）を優先する
		const float distancePriority = 1.0f / (1.0f + distanceFromCamera);
		const float mipPriority = 1.0f / (1.0f + static_cast<float>(desiredMipLevel));
		return distancePriority * mipPriority;
	}
};

/// @brief ストリーミング統計情報
struct StreamingStatistics
{
	std::size_t totalRequests = 0;      ///< 累計リクエスト数
	std::size_t activeLoads = 0;        ///< 現在ロード中の数
	std::size_t completedLoads = 0;     ///< 完了済みロード数
	std::size_t failedLoads = 0;        ///< 失敗したロード数
	std::size_t pendingRequests = 0;    ///< 保留中のリクエスト数
	std::size_t memoryUsedBytes = 0;    ///< 使用メモリ（バイト）
	std::size_t memoryBudgetBytes = 0;  ///< メモリバジェット（バイト）
};

/// @brief LOD ストリーミングマネージャー
/// @tparam T アセット型
/// @details カメラ距離に基づく優先度でアセットをバックグラウンドロードする。
///          メモリバジェットを超過した場合、低優先度のアセットを自動的にアンロードする。
///          ロード完了までプレースホルダーアセットを返す。
///
/// @code
/// auto placeholder = std::make_shared<Texture>(Texture::solid(1, 1, 128, 128, 128, 255));
/// mitiru::resource::StreamingManager<Texture> streaming(
///     64 * 1024 * 1024,  // 64MB バジェット
///     placeholder,
///     4                  // 4 ワーカースレッド
/// );
///
/// streaming.setLoadFunction([](const std::string& path, std::uint8_t mip) {
///     return loadTextureFromDisk(path, mip);
/// });
///
/// streaming.requestAsset("terrain_0_0", "terrain/tile_0_0.tex", 50.0f, 0);
/// streaming.update();  // メインループで毎フレーム呼ぶ
///
/// auto tex = streaming.getAsset("terrain_0_0");  // ロード完了前はプレースホルダー
/// @endcode
template <typename T>
class StreamingManager
{
public:
	/// @brief アセットロード関数型
	using LoadFunction = std::function<std::shared_ptr<T>(const std::string& path, std::uint8_t mipLevel)>;

	/// @brief サイズ推定関数型
	using SizeFunction = std::function<std::size_t(const T& asset)>;

	/// @brief コンストラクタ
	/// @param memoryBudgetBytes メモリバジェット（バイト単位）
	/// @param placeholder フォールバックアセット（ロード中に返す）
	/// @param threadCount ワーカースレッド数（0 = hardware_concurrency - 1）
	explicit StreamingManager(
		std::size_t memoryBudgetBytes,
		std::shared_ptr<T> placeholder = nullptr,
		std::size_t threadCount = 0)
		: m_cache(memoryBudgetBytes)
		, m_placeholder(std::move(placeholder))
		, m_threadPool(threadCount > 0 ? threadCount : 2)
		, m_memoryBudgetBytes(memoryBudgetBytes)
	{
	}

	/// コピー禁止
	StreamingManager(const StreamingManager&) = delete;
	StreamingManager& operator=(const StreamingManager&) = delete;

	/// ムーブ禁止
	StreamingManager(StreamingManager&&) = delete;
	StreamingManager& operator=(StreamingManager&&) = delete;

	/// @brief ロード関数を設定する
	/// @param func パスとミップレベルからアセットをロードする関数
	void setLoadFunction(LoadFunction func)
	{
		const std::lock_guard lock(m_mutex);
		m_loadFunction = std::move(func);
	}

	/// @brief サイズ推定関数を設定する
	/// @param func アセットからバイトサイズを推定する関数
	void setSizeEstimator(SizeFunction func)
	{
		m_cache.setSizeEstimator(std::move(func));
	}

	/// @brief プレースホルダーアセットを設定する
	/// @param placeholder ロード中に返すフォールバックアセット
	void setPlaceholder(std::shared_ptr<T> placeholder)
	{
		const std::lock_guard lock(m_mutex);
		m_placeholder = std::move(placeholder);
	}

	/// @brief アセットのストリーミングをリクエストする
	/// @param id アセットID
	/// @param path ファイルパス
	/// @param distanceFromCamera カメラからの距離
	/// @param mipLevel 要求ミップレベル（0 = 最高品質）
	void requestAsset(
		const std::string& id,
		const std::string& path,
		float distanceFromCamera,
		std::uint8_t mipLevel = 0)
	{
		const std::lock_guard lock(m_mutex);

		/// 既にキャッシュ済みの場合はスキップ
		if (m_cache.contains(id))
		{
			/// 距離情報を更新する
			auto reqIt = m_requests.find(id);
			if (reqIt != m_requests.end())
			{
				reqIt->second.distanceFromCamera = distanceFromCamera;
				reqIt->second.desiredMipLevel = mipLevel;
			}
			return;
		}

		/// 既にリクエスト済みの場合は距離を更新する
		auto reqIt = m_requests.find(id);
		if (reqIt != m_requests.end())
		{
			reqIt->second.distanceFromCamera = distanceFromCamera;
			reqIt->second.desiredMipLevel = mipLevel;
			return;
		}

		/// 新規リクエストを登録する
		StreamingRequest<T> request;
		request.id = id;
		request.path = path;
		request.distanceFromCamera = distanceFromCamera;
		request.desiredMipLevel = mipLevel;
		request.state = StreamingState::Pending;

		m_requests[id] = std::move(request);
		++m_stats.totalRequests;
	}

	/// @brief ストリーミングを更新する（毎フレーム呼び出す）
	/// @param maxDispatches 1回の更新で発行する最大ロードリクエスト数
	void update(std::size_t maxDispatches = 4)
	{
		const std::lock_guard lock(m_mutex);

		/// 完了したロードを回収する
		collectCompleted();

		/// 保留中のリクエストを優先度順にソートする
		std::vector<std::string> pendingIds;
		for (const auto& [id, request] : m_requests)
		{
			if (request.state == StreamingState::Pending)
			{
				pendingIds.push_back(id);
			}
		}

		/// 優先度降順にソートする
		std::sort(pendingIds.begin(), pendingIds.end(),
			[this](const std::string& a, const std::string& b) {
				return m_requests[a].priority() > m_requests[b].priority();
			});

		/// 上位リクエストをディスパッチする
		std::size_t dispatched = 0;
		for (const auto& id : pendingIds)
		{
			if (dispatched >= maxDispatches)
			{
				break;
			}

			if (m_activeLoads >= m_maxConcurrentLoads)
			{
				break;
			}

			dispatchLoad(id);
			++dispatched;
		}
	}

	/// @brief アセットを取得する（ロード中はプレースホルダーを返す）
	/// @param id アセットID
	/// @return アセット（キャッシュ済み or プレースホルダー）
	[[nodiscard]] std::shared_ptr<T> getAsset(const std::string& id)
	{
		auto cached = m_cache.get(id);
		if (cached.has_value())
		{
			return cached.value();
		}

		/// プレースホルダーを返す
		return m_placeholder;
	}

	/// @brief アセットのロード状態を取得する
	/// @param id アセットID
	/// @return ストリーミング状態（リクエストがなければ nullopt）
	[[nodiscard]] std::optional<StreamingState> getState(const std::string& id) const
	{
		const std::lock_guard lock(m_mutex);
		auto it = m_requests.find(id);
		if (it == m_requests.end())
		{
			/// キャッシュにあれば Loaded
			if (m_cache.contains(id))
			{
				return StreamingState::Loaded;
			}
			return std::nullopt;
		}
		return it->second.state;
	}

	/// @brief 指定アセットのリクエストをキャンセルする
	/// @param id アセットID
	void cancelRequest(const std::string& id)
	{
		const std::lock_guard lock(m_mutex);
		auto it = m_requests.find(id);
		if (it != m_requests.end() && it->second.state == StreamingState::Pending)
		{
			m_requests.erase(it);
		}
	}

	/// @brief 同時ロード数の上限を設定する
	/// @param count 最大同時ロード数
	void setMaxConcurrentLoads(std::size_t count) noexcept
	{
		m_maxConcurrentLoads = count;
	}

	/// @brief ストリーミング統計情報を取得する
	/// @return 統計のスナップショット
	[[nodiscard]] StreamingStatistics statistics() const
	{
		const std::lock_guard lock(m_mutex);
		StreamingStatistics result = m_stats;
		result.activeLoads = m_activeLoads;
		result.memoryUsedBytes = m_cache.currentBytes();
		result.memoryBudgetBytes = m_memoryBudgetBytes;

		std::size_t pending = 0;
		for (const auto& [id, request] : m_requests)
		{
			if (request.state == StreamingState::Pending)
			{
				++pending;
			}
		}
		result.pendingRequests = pending;

		return result;
	}

	/// @brief 全リクエストをクリアしキャッシュを空にする
	void clear()
	{
		const std::lock_guard lock(m_mutex);
		m_requests.clear();
		m_cache.clear();
		m_activeLoads = 0;
		m_stats = StreamingStatistics{};
	}

private:
	/// @brief 完了タスクの結果
	struct CompletedLoad
	{
		std::string id;
		std::shared_ptr<T> asset;
		bool success = false;
	};

	/// @brief ロードタスクをディスパッチする
	/// @param id リクエストID
	void dispatchLoad(const std::string& id)
	{
		auto& request = m_requests[id];
		request.state = StreamingState::Loading;
		++m_activeLoads;

		const std::string path = request.path;
		const std::uint8_t mipLevel = request.desiredMipLevel;
		auto loadFunc = m_loadFunction;

		m_threadPool.submit(
			[this, id, path, mipLevel, loadFunc]() {
				std::shared_ptr<T> result = nullptr;
				bool success = false;

				if (loadFunc)
				{
					try
					{
						result = loadFunc(path, mipLevel);
						success = (result != nullptr);
					}
					catch (...)
					{
						success = false;
					}
				}

				/// 完了キューに追加する
				const std::lock_guard lock(m_completedMutex);
				m_completedLoads.push_back(CompletedLoad{id, std::move(result), success});
			});
	}

	/// @brief 完了したロードを回収してキャッシュに格納する
	void collectCompleted()
	{
		std::vector<CompletedLoad> completed;
		{
			const std::lock_guard lock(m_completedMutex);
			completed.swap(m_completedLoads);
		}

		for (auto& load : completed)
		{
			if (m_activeLoads > 0)
			{
				--m_activeLoads;
			}

			auto reqIt = m_requests.find(load.id);
			if (reqIt == m_requests.end())
			{
				continue;  ///< キャンセル済み
			}

			if (load.success)
			{
				reqIt->second.state = StreamingState::Loaded;
				m_cache.put(load.id, std::move(load.asset));
				++m_stats.completedLoads;
			}
			else
			{
				reqIt->second.state = StreamingState::Failed;
				++m_stats.failedLoads;
			}
		}
	}

	mutable std::mutex m_mutex;              ///< メインミューテックス
	std::mutex m_completedMutex;             ///< 完了キュー用ミューテックス

	AssetCache<T> m_cache;                   ///< LRU キャッシュ
	ThreadPool m_threadPool;                 ///< ワーカースレッドプール
	std::shared_ptr<T> m_placeholder;        ///< プレースホルダーアセット

	LoadFunction m_loadFunction;             ///< ロード関数

	/// @brief リクエストマップ
	std::unordered_map<std::string, StreamingRequest<T>> m_requests;

	/// @brief 完了したロードのキュー
	std::vector<CompletedLoad> m_completedLoads;

	std::size_t m_activeLoads = 0;           ///< 現在アクティブなロード数
	std::size_t m_maxConcurrentLoads = 8;    ///< 同時ロード上限
	std::size_t m_memoryBudgetBytes = 0;     ///< メモリバジェット

	StreamingStatistics m_stats;             ///< 統計情報
};

} // namespace mitiru::resource
