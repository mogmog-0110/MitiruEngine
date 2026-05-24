#pragma once

/// @file ThreadPool.hpp
/// @brief ワーカースレッドプール
///
/// タスクのサブミットと並列forを提供する。
/// コピー・ムーブ不可。デストラクタで全スレッドをjoinする。
///
/// @code
/// sgc::ThreadPool pool(4);
/// auto future = pool.submit([] { return 42; });
/// int result = future.get(); // 42
///
/// pool.parallelFor(0, 1000, [](std::size_t i) {
///     processItem(i);
/// });
/// @endcode

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

namespace sgc
{

/// @brief ワーカースレッドプール
///
/// 固定数のワーカースレッドでタスクキューを処理する。
class ThreadPool
{
public:
	/// @brief スレッドプールを構築する
	/// @param threadCount ワーカースレッド数（0でhardware_concurrency）
	explicit ThreadPool(std::size_t threadCount = 0)
	{
		if (threadCount == 0)
		{
			threadCount = std::thread::hardware_concurrency();
			if (threadCount == 0) threadCount = 2;
		}

		m_workers.reserve(threadCount);
		for (std::size_t i = 0; i < threadCount; ++i)
		{
			m_workers.emplace_back([this] { workerLoop(); });
		}
	}

	/// @brief コピー不可
	ThreadPool(const ThreadPool&) = delete;
	/// @brief コピー不可
	ThreadPool& operator=(const ThreadPool&) = delete;
	/// @brief ムーブ不可
	ThreadPool(ThreadPool&&) = delete;
	/// @brief ムーブ不可
	ThreadPool& operator=(ThreadPool&&) = delete;

	/// @brief デストラクタ — 全タスク完了後にスレッドをjoinする
	~ThreadPool()
	{
		{
			std::scoped_lock lock(m_mutex);
			m_stop = true;
		}
		m_condition.notify_all();
		for (auto& worker : m_workers)
		{
			if (worker.joinable()) worker.join();
		}
	}

	/// @brief タスクをサブミットする
	/// @tparam F 呼び出し可能型
	/// @tparam Args 引数型
	/// @param func タスク関数
	/// @param args 引数
	/// @return 結果を保持するfuture
	template <typename F, typename... Args>
	[[nodiscard]] auto submit(F&& func, Args&&... args)
		-> std::future<std::invoke_result_t<F, Args...>>
	{
		using ReturnType = std::invoke_result_t<F, Args...>;

		auto task = std::make_shared<std::packaged_task<ReturnType()>>(
			std::bind(std::forward<F>(func), std::forward<Args>(args)...)
		);

		auto future = task->get_future();

		{
			std::scoped_lock lock(m_mutex);
			m_tasks.push([task]() { (*task)(); });
		}
		m_condition.notify_one();

		return future;
	}

	/// @brief 範囲を並列処理する
	/// @tparam Func コールバック型 (std::size_t index)
	/// @param begin 開始インデックス
	/// @param end 終了インデックス（排他）
	/// @param func 各インデックスに対するコールバック
	template <typename Func>
	void parallelFor(std::size_t begin, std::size_t end, Func&& func)
	{
		if (begin >= end) return;

		const std::size_t totalWork = end - begin;
		const std::size_t numWorkers = m_workers.size();
		const std::size_t chunkSize = (totalWork + numWorkers - 1) / numWorkers;

		std::vector<std::future<void>> futures;
		futures.reserve(numWorkers);

		for (std::size_t i = 0; i < numWorkers; ++i)
		{
			const std::size_t chunkBegin = begin + i * chunkSize;
			if (chunkBegin >= end) break;
			const std::size_t chunkEnd = std::min(chunkBegin + chunkSize, end);

			futures.push_back(submit([&func, chunkBegin, chunkEnd]() {
				for (std::size_t j = chunkBegin; j < chunkEnd; ++j)
				{
					func(j);
				}
			}));
		}

		for (auto& f : futures)
		{
			f.get();
		}
	}

	/// @brief ワーカースレッド数
	/// @return スレッド数
	[[nodiscard]] std::size_t threadCount() const noexcept
	{
		return m_workers.size();
	}

private:
	/// @brief ワーカースレッドのメインループ
	void workerLoop()
	{
		while (true)
		{
			std::function<void()> task;
			{
				std::unique_lock lock(m_mutex);
				m_condition.wait(lock, [this] {
					return m_stop || !m_tasks.empty();
				});
				if (m_stop && m_tasks.empty()) return;
				task = std::move(m_tasks.front());
				m_tasks.pop();
			}
			task();
		}
	}

	std::vector<std::thread> m_workers;             ///< ワーカースレッド
	std::queue<std::function<void()>> m_tasks;      ///< タスクキュー
	std::mutex m_mutex;                             ///< タスクキューのミューテックス
	std::condition_variable m_condition;             ///< タスク通知
	bool m_stop{false};                             ///< 停止フラグ
};

} // namespace sgc
