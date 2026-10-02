#pragma once

/// @file AsyncAssetLoader.hpp
/// @brief 非同期アセットローダーの宣言

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
#include <mitiru/core/JobSystem.hpp>

namespace mitiru::resource
{

enum class AssetPriority : std::uint8_t
{
	Low = 0,       ///< 低優先度（遠方のアセット等）
	Normal = 1,    ///< 通常優先度
	High = 2,      ///< 高優先度（プレイヤー近傍等）
	Urgent = 3     ///< 最高優先度（即座に必要なアセット）
};

/// @brief 複数のロード要求で共有できるキャンセルトークン
class CancellationToken
{
public:
	CancellationToken()
		: m_cancelled(std::make_shared<std::atomic<bool>>(false))
	{
	}

	void cancel() noexcept
	{
		m_cancelled->store(true, std::memory_order_release);
	}

	[[nodiscard]] bool isCancelled() const noexcept
	{
		return m_cancelled->load(std::memory_order_acquire);
	}

private:
	std::shared_ptr<std::atomic<bool>> m_cancelled; ///< キャンセルフラグ（共有）
};

struct BatchProgress
{
	std::size_t completed = 0;   ///< 完了済み数
	std::size_t total = 0;       ///< 合計数
	std::size_t failed = 0;      ///< 失敗数

	/// @return 0.0 から 1.0 までの進捗率
	[[nodiscard]] double ratio() const noexcept
	{
		return (total > 0)
			? (static_cast<double>(completed) / static_cast<double>(total))
			: 0.0;
	}
};

/// @brief AssetManager と連携し、優先度とキャンセルに対応する非同期アセットローダー
class AsyncAssetLoader
{
public:
	/// @param threadCount ワーカースレッド数。0 のときは hardware_concurrency より 1 少ない数
	explicit AsyncAssetLoader(AssetManager& assetManager, std::size_t threadCount = 0)
		: m_assetManager(assetManager)
		, m_jobs(threadCount)
	{
	}

	AsyncAssetLoader(const AsyncAssetLoader&) = delete;
	AsyncAssetLoader& operator=(const AsyncAssetLoader&) = delete;

	AsyncAssetLoader(AsyncAssetLoader&&) = delete;
	AsyncAssetLoader& operator=(AsyncAssetLoader&&) = delete;

	template <typename T>
	[[nodiscard]] std::future<AssetHandle<T>> load(
		const std::string& id,
		std::string_view path,
		AssetPriority priority = AssetPriority::Normal,
		CancellationToken token = CancellationToken{})
	{
		/// priority はまだ実行順に反映していない (投げた順に実行する)
		const std::string pathStr(path);

		return m_jobs.submit(
			[this, id, pathStr, token = std::move(token)]() -> AssetHandle<T> {
				if (token.isCancelled())
				{
					return AssetHandle<T>{id, std::shared_ptr<T>{}};
				}

				return m_assetManager.load<T>(id, pathStr);
			});
	}

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
			futures.push_back(m_jobs.submit(
				[this, id, path, token, progress, failCount, totalCount,
				 callback, callbackMutex]() -> AssetHandle<T> {
					if (token.isCancelled())
					{
						failCount->fetch_add(1, std::memory_order_relaxed);
						notifyProgress(callback, callbackMutex, progress, failCount, totalCount);
						return AssetHandle<T>{id, std::shared_ptr<T>{}};
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

	/// @return まだ終わっていないロードの数 (実行中のものも含む)
	[[nodiscard]] std::size_t pendingCount() const
	{
		return m_jobs.pendingCount();
	}

	[[nodiscard]] std::size_t workerCount() const noexcept
	{
		return m_jobs.workerCount();
	}

private:
	/// @brief 進捗コールバックは mutex で直列に呼び出す
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
	JobSystem m_jobs;               ///< 最後に破棄 (走っているロードが m_assetManager を触り終えてから)
};

} // namespace mitiru::resource
