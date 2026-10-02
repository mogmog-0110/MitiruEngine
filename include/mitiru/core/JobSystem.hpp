#pragma once

/// @file JobSystem.hpp
/// @brief アセットの非同期ロード用に、関数をワーカースレッドで実行して結果を std::future で返す。
/// @details 実体には Taskflow の tf::Executor を使う。Taskflow がないビルドでは submit が関数をその場で実行し、完了済みの future を返す。
/// GameMemory を書く処理は渡さない。完了順がスレッドの都合で変わり、リプレイを再現できなくなる。

#include <atomic>
#include <cstddef>
#include <future>
#include <thread>
#include <type_traits>
#include <utility>

#if defined(MITIRU_HAS_TASKFLOW)
#	include <taskflow/taskflow.hpp>
#endif

namespace mitiru
{

class JobSystem
{
public:
	/// @param workerCount 0 のときは hardware_concurrency より 1 少ない数。最低 1
	explicit JobSystem(std::size_t workerCount = 0)
#if defined(MITIRU_HAS_TASKFLOW)
		: m_executor(resolveWorkerCount(workerCount))
#endif
	{
#if !defined(MITIRU_HAS_TASKFLOW)
		(void)workerCount;
#endif
	}

	JobSystem(const JobSystem&) = delete;
	JobSystem& operator=(const JobSystem&) = delete;
	JobSystem(JobSystem&&) = delete;
	JobSystem& operator=(JobSystem&&) = delete;

	/// @brief 関数が持ち主の this を捕まえていても、すべて終わってから破棄する。
	~JobSystem() { waitAll(); }

	/// @brief どのスレッドからでも呼べる。例外は future.get() で投げ直される。
	template <typename F>
	[[nodiscard]] auto submit(F&& job) -> std::future<std::invoke_result_t<std::decay_t<F>>>
	{
		using Result = std::invoke_result_t<std::decay_t<F>>;
		auto work = [this, fn = std::forward<F>(job)]() mutable -> Result {
			const PendingGuard guard{m_pending};
			return fn();
		};
#if defined(MITIRU_HAS_TASKFLOW)
		// 走り出す前に数えておく。登録に失敗したら走らないので戻す
		m_pending.fetch_add(1, std::memory_order_relaxed);
		try
		{
			return m_executor.async(std::move(work));
		}
		catch (...)
		{
			m_pending.fetch_sub(1, std::memory_order_relaxed);
			throw;
		}
#else
		std::packaged_task<Result()> task(std::move(work));
		auto future = task.get_future();
		m_pending.fetch_add(1, std::memory_order_relaxed);
		task();
		return future;
#endif
	}

	/// @brief submit 済みでまだ終わっていない関数の数 (待ち行列にあるものだけではない)
	[[nodiscard]] std::size_t pendingCount() const noexcept
	{
		return m_pending.load(std::memory_order_relaxed);
	}

	/// @brief Taskflow がないビルドでは 0。submit はその場で実行する。
	[[nodiscard]] std::size_t workerCount() const noexcept
	{
#if defined(MITIRU_HAS_TASKFLOW)
		return m_executor.num_workers();
#else
		return 0;
#endif
	}

	void waitAll()
	{
#if defined(MITIRU_HAS_TASKFLOW)
		m_executor.wait_for_all();
#endif
	}

private:
	struct PendingGuard
	{
		std::atomic<std::size_t>& count;
		~PendingGuard() { count.fetch_sub(1, std::memory_order_relaxed); }
	};

	static std::size_t resolveWorkerCount(std::size_t requested) noexcept
	{
		if (requested > 0) { return requested; }
		const auto hw = std::thread::hardware_concurrency();
		return hw > 1 ? hw - 1 : 1;
	}

	std::atomic<std::size_t> m_pending{0};
#if defined(MITIRU_HAS_TASKFLOW)
	tf::Executor m_executor;
#endif
};

}  // namespace mitiru
