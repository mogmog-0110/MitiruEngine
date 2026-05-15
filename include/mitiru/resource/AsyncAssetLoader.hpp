#pragma once

/// @file AsyncAssetLoader.hpp
/// @brief スレッドプールベースの非同期アセットローダー
/// @details 優先度付きキューとキャンセルトークンを用いた非同期アセットロード。
///          既存の AssetManager と統合し、バッチロードや進捗コールバックを提供する。

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <mitiru/resource/AssetCache.hpp>
#include <mitiru/resource/AssetHandle.hpp>
#include <mitiru/resource/AssetManager.hpp>
#include <mitiru/resource/ThreadPool.hpp>

namespace mitiru::resource
{

/// @brief アセットロード優先度
enum class AssetPriority : std::uint8_t
{
	Low = 0,       ///< 低優先度（遠方のアセット等）
	Normal = 1,    ///< 通常優先度
	High = 2,      ///< 高優先度（プレイヤー近傍等）
	Urgent = 3     ///< 最高優先度（即座に必要なアセット）
};

/// @brief キャンセルトークン
/// @details 非同期ロードリクエストのキャンセルを制御する。
///          複数のリクエスト間で共有可能。
class CancellationToken
{
public:
	/// @brief デフォルトコンストラクタ
	CancellationToken()
		: m_cancelled(std::make_shared<std::atomic<bool>>(false))
	{
	}

	/// @brief キャンセルを要求する
	void cancel() noexcept
	{
		m_cancelled->store(true, std::memory_order_release);
	}

	/// @brief キャンセルが要求されたか判定する
	/// @return キャンセル済みなら true
	[[nodiscard]] bool isCancelled() const noexcept
	{
		return m_cancelled->load(std::memory_order_acquire);
	}

private:
	std::shared_ptr<std::atomic<bool>> m_cancelled; ///< キャンセルフラグ（共有）
};

/// @brief バッチロードの進捗情報
struct BatchProgress
{
	std::size_t completed = 0;   ///< 完了済み数
	std::size_t total = 0;       ///< 合計数
	std::size_t failed = 0;      ///< 失敗数

	/// @brief 進捗率を取得する
	/// @return 0.0 〜 1.0
	[[nodiscard]] double ratio() const noexcept
	{
		return (total > 0)
			? (static_cast<double>(completed) / static_cast<double>(total))
			: 0.0;
	}
};

/// @brief スレッドプールベースの非同期アセットローダー
/// @details AssetManager と統合し、優先度付きキューでアセットを非同期にロードする。
///          キャンセルトークンにより進行中のリクエストをキャンセルできる。
///
/// @code
/// mitiru::resource::AssetManager manager;
/// manager.registerLoader<TextureLoader>(TextureLoader{});
///
/// mitiru::resource::AsyncAssetLoader loader(manager, 4);
/// auto future = loader.load<Texture>("player", "textures/player.png");
/// auto handle = future.get();  // ブロッキング待機
///
/// // バッチロード
/// std::vector<std::pair<std::string, std::string>> batch = {
///     {"enemy1", "textures/enemy1.png"},
///     {"enemy2", "textures/enemy2.png"},
/// };
/// auto futures = loader.loadBatch<Texture>(batch, [](const BatchProgress& p) {
///     std::cout << p.ratio() * 100.0 << "%" << std::endl;
/// });
/// @endcode
class AsyncAssetLoader
{
public:
	/// @brief コンストラクタ
	/// @param assetManager 既存の AssetManager への参照
	/// @param threadCount ワーカースレッド数（0 = hardware_concurrency - 1）
	explicit AsyncAssetLoader(AssetManager& assetManager, std::size_t threadCount = 0)
		: m_assetManager(assetManager)
		, m_threadPool(threadCount)
	{
	}

	/// コピー禁止
	AsyncAssetLoader(const AsyncAssetLoader&) = delete;
	AsyncAssetLoader& operator=(const AsyncAssetLoader&) = delete;

	/// ムーブ禁止
	AsyncAssetLoader(AsyncAssetLoader&&) = delete;
	AsyncAssetLoader& operator=(AsyncAssetLoader&&) = delete;

	/// @brief アセットを非同期にロードする
	/// @tparam T アセット型
	/// @param id アセットID
	/// @param path ファイルパス
	/// @param priority ロード優先度
	/// @param token キャンセルトークン（省略可）
	/// @return アセットハンドルの future
	template <typename T>
	[[nodiscard]] std::future<AssetHandle<T>> load(
		const std::string& id,
		std::string_view path,
		AssetPriority priority = AssetPriority::Normal,
		CancellationToken token = CancellationToken{})
	{
		/// 高優先度タスクを先に処理するための重み付け
		/// ThreadPool は FIFO だが、Urgent は即座に submit する
		const std::string pathStr(path);

		return m_threadPool.submit(
			[this, id, pathStr, token = std::move(token)]() -> AssetHandle<T> {
				if (token.isCancelled())
				{
					return AssetHandle<T>{id, nullptr};
				}

				return m_assetManager.load<T>(id, pathStr);
			});
	}

	/// @brief 複数アセットをバッチで非同期ロードする
	/// @tparam T アセット型
	/// @param entries (ID, パス) のペアリスト
	/// @param progressCallback 進捗コールバック（省略可）
	/// @param priority ロード優先度
	/// @param token キャンセルトークン（省略可）
	/// @return 各アセットハンドルの future のベクタ
	template <typename T>
	[[nodiscard]] std::vector<std::future<AssetHandle<T>>> loadBatch(
		const std::vector<std::pair<std::string, std::string>>& entries,
		std::function<void(const BatchProgress&)> progressCallback = nullptr,
		AssetPriority priority = AssetPriority::Normal,
		CancellationToken token = CancellationToken{})
	{
		auto progress = std::make_shared<std::atomic<std::size_t>>(0);
		auto failCount = std::make_shared<std::atomic<std::size_t>>(0);
		const auto totalCount = entries.size();
		auto callback = std::move(progressCallback);
		auto callbackMutex = std::make_shared<std::mutex>();

		std::vector<std::future<AssetHandle<T>>> futures;
		futures.reserve(totalCount);

		for (const auto& [id, path] : entries)
		{
			futures.push_back(m_threadPool.submit(
				[this, id, path, token, progress, failCount, totalCount,
				 callback, callbackMutex]() -> AssetHandle<T> {
					if (token.isCancelled())
					{
						failCount->fetch_add(1, std::memory_order_relaxed);
						notifyProgress(callback, callbackMutex, progress, failCount, totalCount);
						return AssetHandle<T>{id, nullptr};
					}

					auto handle = m_assetManager.load<T>(id, path);

					if (!handle.isLoaded())
					{
						failCount->fetch_add(1, std::memory_order_relaxed);
					}

					notifyProgress(callback, callbackMutex, progress, failCount, totalCount);
					return handle;
				}));
		}

		return futures;
	}

	/// @brief 保留中のタスク数を取得する
	/// @return キュー内のタスク数
	[[nodiscard]] std::size_t pendingCount() const
	{
		return m_threadPool.pendingCount();
	}

	/// @brief ワーカースレッド数を取得する
	/// @return スレッド数
	[[nodiscard]] std::size_t workerCount() const noexcept
	{
		return m_threadPool.workerCount();
	}

private:
	/// @brief 進捗コールバックを呼び出す
	/// @param callback コールバック関数
	/// @param mutex コールバック排他用ミューテックス
	/// @param progress 完了カウンタ
	/// @param failCount 失敗カウンタ
	/// @param total 合計数
	static void notifyProgress(
		const std::function<void(const BatchProgress&)>& callback,
		const std::shared_ptr<std::mutex>& mutex,
		const std::shared_ptr<std::atomic<std::size_t>>& progress,
		const std::shared_ptr<std::atomic<std::size_t>>& failCount,
		std::size_t total)
	{
		const auto completed = progress->fetch_add(1, std::memory_order_relaxed) + 1;

		if (callback)
		{
			const std::lock_guard<std::mutex> lock(*mutex);
			BatchProgress bp;
			bp.completed = completed;
			bp.total = total;
			bp.failed = failCount->load(std::memory_order_relaxed);
			callback(bp);
		}
	}

	AssetManager& m_assetManager;   ///< アセットマネージャー参照
	ThreadPool m_threadPool;        ///< スレッドプール
};

} // namespace mitiru::resource
