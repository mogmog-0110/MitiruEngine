#pragma once

/// @file ThreadPool.hpp
/// @brief ワークスティーリング型スレッドプール
/// @details std::jthread ベースのスレッドプール。
///          タスクをキューに投入し、ワーカースレッドが非同期に処理する。
///          RAII ライフサイクル（デストラクタでグレースフルシャットダウン）。

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace mitiru::resource
{

/// @brief ワークスティーリング型スレッドプール
/// @details 設定可能なスレッド数でタスクを並列実行する。
///          submit() で callable を投入し、std::future で結果を受け取る。
///          デストラクタで保留中のタスクを完了してからスレッドを join する。
///
/// @code
/// mitiru::resource::ThreadPool pool(4);
/// auto future = pool.submit([]{ return 42; });
/// int result = future.get();  // 42
/// @endcode
class ThreadPool
{
public:
	/// @brief コンストラクタ
	/// @param threadCount ワーカースレッド数（0 の場合は hardware_concurrency - 1）
	explicit ThreadPool(std::size_t threadCount = 0)
		: m_stopping(false)
	{
		if (threadCount == 0)
		{
			const auto hw = std::thread::hardware_concurrency();
			threadCount = (hw > 1) ? (hw - 1) : 1;
		}

		m_workers.reserve(threadCount);
		for (std::size_t i = 0; i < threadCount; ++i)
		{
			m_workers.emplace_back([this](std::stop_token stopToken) {
				workerLoop(stopToken);
			});
		}
	}

	/// コピー禁止
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

	/// ムーブ禁止
	ThreadPool(ThreadPool&&) = delete;
	ThreadPool& operator=(ThreadPool&&) = delete;

	/// @brief デストラクタ（グレースフルシャットダウン）
	/// @details 停止フラグを立て、全ワーカースレッドの完了を待つ。
	///          保留中のタスクは破棄される。
	~ThreadPool()
	{
		shutdown();
	}

	/// @brief タスクを投入する
	/// @tparam F callable 型
	/// @tparam Args 引数型パック
	/// @param func 実行する callable
	/// @param args callable への引数
	/// @return タスク結果の future
	template <typename F, typename... Args>
	[[nodiscard]] auto submit(F&& func, Args&&... args)
		-> std::future<std::invoke_result_t<F, Args...>>
	{
		using ReturnType = std::invoke_result_t<F, Args...>;

		auto task = std::make_shared<std::packaged_task<ReturnType()>>(
			std::bind(std::forward<F>(func), std::forward<Args>(args)...));

		auto future = task->get_future();

		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			if (m_stopping)
			{
				throw std::runtime_error("ThreadPool: submit() called after shutdown");
			}
			m_tasks.emplace_back([task = std::move(task)]() { (*task)(); });
		}

		m_condition.notify_one();
		return future;
	}

	/// @brief 保留中のタスク数を取得する
	/// @return キュー内のタスク数
	[[nodiscard]] std::size_t pendingCount() const
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		return m_tasks.size();
	}

	/// @brief ワーカースレッド数を取得する
	/// @return スレッド数
	[[nodiscard]] std::size_t workerCount() const noexcept
	{
		return m_workers.size();
	}

	/// @brief スレッドプールを停止する
	/// @details 停止フラグを立て、全ワーカースレッドを join する。
	void shutdown()
	{
		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			if (m_stopping)
			{
				return;
			}
			m_stopping = true;
		}

		m_condition.notify_all();

		for (auto& worker : m_workers)
		{
			worker.request_stop();
			if (worker.joinable())
			{
				worker.join();
			}
		}
	}

private:
	/// @brief ワーカースレッドのメインループ
	/// @param stopToken jthread の停止トークン
	void workerLoop(std::stop_token stopToken)
	{
		while (!stopToken.stop_requested())
		{
			std::function<void()> task;
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				m_condition.wait(lock, [this, &stopToken] {
					return m_stopping || stopToken.stop_requested() || !m_tasks.empty();
				});

				if ((m_stopping || stopToken.stop_requested()) && m_tasks.empty())
				{
					return;
				}

				if (m_tasks.empty())
				{
					continue;
				}

				task = std::move(m_tasks.front());
				m_tasks.pop_front();
			}

			task();
		}
	}

	mutable std::mutex m_mutex;                  ///< タスクキュー保護用ミューテックス
	std::condition_variable m_condition;          ///< タスク到着通知
	std::deque<std::function<void()>> m_tasks;   ///< タスクキュー
	std::vector<std::jthread> m_workers;         ///< ワーカースレッド
	bool m_stopping;                             ///< 停止フラグ
};

} // namespace mitiru::resource
