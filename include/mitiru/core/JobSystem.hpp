#pragma once

/// @file JobSystem.hpp
/// @brief マルチスレッドジョブシステム
/// @details ワークスティーリングキューを持つスレッドプールベースのジョブシステム。
///          ジョブ依存関係・優先度レベル・並列forをサポートする。

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include <mitiru/debug/Log.hpp>

namespace mitiru
{

/// @brief ジョブ優先度レベル
enum class JobPriority
{
	High,    ///< 高優先度（最優先実行）
	Normal,  ///< 通常優先度
	Low      ///< 低優先度（バックグラウンド処理）
};

/// @brief ジョブハンドル
/// @details ジョブの完了状態を追跡するためのハンドル。
///          アトミックな完了フラグを持つ。
class JobHandle
{
public:
	/// @brief デフォルトコンストラクタ（完了済みハンドル）
	JobHandle() noexcept
		: m_state(std::make_shared<State>())
	{
		m_state->completed.store(true, std::memory_order_release);
	}

	/// @brief ジョブが完了したかを判定する
	[[nodiscard]] bool isComplete() const noexcept
	{
		return m_state->completed.load(std::memory_order_acquire);
	}

	/// @brief ジョブの完了を待機する
	void wait() const
	{
		std::unique_lock<std::mutex> lock(m_state->mutex);
		m_state->cv.wait(lock, [this]
		{
			return m_state->completed.load(std::memory_order_acquire);
		});
	}

private:
	friend class JobSystem;

	struct State
	{
		std::atomic<bool> completed{false};
		std::atomic<int> pendingCount{0};
		std::mutex mutex;
		std::condition_variable cv;
	};

	explicit JobHandle(std::shared_ptr<State> state) noexcept
		: m_state(std::move(state))
	{
	}

	/// @brief 完了を通知する
	void markComplete() const
	{
		m_state->completed.store(true, std::memory_order_release);
		std::lock_guard<std::mutex> lock(m_state->mutex);
		m_state->cv.notify_all();
	}

	/// @brief 保留カウントをデクリメントし、0になったら完了をマークする
	void decrementPending() const
	{
		if (m_state->pendingCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
		{
			markComplete();
		}
	}

	std::shared_ptr<State> m_state;
};

/// @brief ジョブシステム
/// @details ワークスティーリングキューを持つスレッドプール。
///          ジョブの投入・依存関係・並列forを提供する。
///
/// @code
/// mitiru::JobSystem jobs;
/// jobs.init(4); // 4ワーカースレッド
///
/// auto handle = jobs.schedule([]{ /* heavy work */ });
/// jobs.wait(handle);
///
/// // 並列for
/// std::vector<int> data(1000);
/// auto pHandle = jobs.scheduleParallel(
///     data.size(), 100,
///     [&data](std::size_t begin, std::size_t end) {
///         for (auto i = begin; i < end; ++i)
///             data[i] = computeSomething(i);
///     });
/// jobs.wait(pHandle);
///
/// jobs.shutdown();
/// @endcode
class JobSystem
{
public:
	/// @brief デフォルトコンストラクタ
	JobSystem() noexcept = default;

	/// @brief デストラクタ（自動シャットダウン）
	~JobSystem()
	{
		shutdown();
	}

	/// コピー禁止
	JobSystem(const JobSystem&) = delete;
	JobSystem& operator=(const JobSystem&) = delete;

	/// ムーブ禁止（スレッドを管理するため）
	JobSystem(JobSystem&&) = delete;
	JobSystem& operator=(JobSystem&&) = delete;

	/// @brief ジョブシステムを初期化する
	/// @param threadCount ワーカースレッド数（0=hardware_concurrency - 1）
	void init(int threadCount = 0)
	{
		if (m_initialized) return;

		if (threadCount <= 0)
		{
			threadCount = std::max(1,
				static_cast<int>(std::thread::hardware_concurrency()) - 1);
		}

		m_threadCount = threadCount;
		m_running.store(true, std::memory_order_release);

		/// ワーカーキューを初期化する。WorkerQueue は std::mutex を持ち move/copy 不可な
		/// ため、unique_ptr で保持して vector に間接的に並べる。
		m_queues.clear();
		m_queues.reserve(static_cast<std::size_t>(threadCount));
		for (int q = 0; q < threadCount; ++q)
		{
			m_queues.push_back(std::make_unique<WorkerQueue>());
		}

		/// ワーカースレッドを起動する
		m_workers.reserve(static_cast<std::size_t>(threadCount));
		for (int i = 0; i < threadCount; ++i)
		{
			m_workers.emplace_back([this, i] { workerLoop(i); });
		}

		m_initialized = true;
		MITIRU_LOG_INFO("JobSystem",
			"initialized: " + std::to_string(threadCount) + " workers");
	}

	/// @brief 初期化済みかどうかを返す
	[[nodiscard]] bool isInitialized() const noexcept
	{
		return m_initialized;
	}

	/// @brief ワーカースレッド数を取得する
	[[nodiscard]] int threadCount() const noexcept
	{
		return m_threadCount;
	}

	/// @brief ジョブを投入する
	/// @param job 実行する関数
	/// @param priority 優先度
	/// @return ジョブハンドル
	[[nodiscard]] JobHandle schedule(std::function<void()> job,
	                                 JobPriority priority = JobPriority::Normal)
	{
		auto state = std::make_shared<JobHandle::State>();
		state->pendingCount.store(1, std::memory_order_release);

		JobHandle handle(state);

		JobEntry entry;
		entry.work = std::move(job);
		entry.handle = handle;
		entry.priority = priority;

		pushJob(std::move(entry));

		return handle;
	}

	/// @brief 依存関係付きジョブを投入する
	/// @param job 実行する関数
	/// @param dependency 依存するジョブハンドル
	/// @param priority 優先度
	/// @return ジョブハンドル
	[[nodiscard]] JobHandle schedule(std::function<void()> job,
	                                 const JobHandle& dependency,
	                                 JobPriority priority = JobPriority::Normal)
	{
		auto state = std::make_shared<JobHandle::State>();
		state->pendingCount.store(1, std::memory_order_release);

		JobHandle handle(state);

		JobEntry entry;
		entry.work = std::move(job);
		entry.handle = handle;
		entry.priority = priority;

		// 依存が未完なら worker キューでなく pending に積み、依存完了時
		// (executeJob → promotePending) にキューへ昇格させる。worker をブロック
		// しないので、依存解決を待つ間も他のジョブを処理できる。
		bool runNow = dependency.isComplete();
		if (!runNow)
		{
			std::lock_guard<std::mutex> lock(m_pendingMutex);
			// lock 内で再確認: 直前に依存が完了して promotePending を取り逃すレースを防ぐ。
			if (dependency.isComplete())
			{
				runNow = true;
			}
			else
			{
				m_pending.push_back(PendingJob{std::move(entry), dependency});
				m_pendingCount.store(m_pending.size(), std::memory_order_relaxed);
			}
		}
		if (runNow) { pushJob(std::move(entry)); }

		return handle;
	}

	/// @brief 並列forを投入する
	/// @param itemCount 要素数
	/// @param batchSize バッチサイズ
	/// @param func 実行関数（begin, end を受け取る）
	/// @return ジョブハンドル（全バッチの完了を表す）
	[[nodiscard]] JobHandle scheduleParallel(
		std::size_t itemCount,
		std::size_t batchSize,
		std::function<void(std::size_t, std::size_t)> func)
	{
		if (itemCount == 0 || batchSize == 0)
		{
			return JobHandle{};
		}

		const std::size_t batchCount =
			(itemCount + batchSize - 1) / batchSize;

		auto state = std::make_shared<JobHandle::State>();
		state->pendingCount.store(
			static_cast<int>(batchCount), std::memory_order_release);

		JobHandle handle(state);

		for (std::size_t b = 0; b < batchCount; ++b)
		{
			const std::size_t begin = b * batchSize;
			const std::size_t end = std::min(begin + batchSize, itemCount);

			JobEntry entry;
			entry.work = [func, begin, end]{ func(begin, end); };
			entry.handle = handle;
			entry.priority = JobPriority::Normal;
			entry.isParallelBatch = true;

			pushJob(std::move(entry));
		}

		return handle;
	}

	/// @brief ジョブの完了を待機する
	/// @param handle 待機するジョブハンドル
	void wait(const JobHandle& handle)
	{
		handle.wait();
	}

	/// @brief ジョブが完了したかを判定する
	/// @param handle チェックするジョブハンドル
	/// @return 完了済みならtrue
	[[nodiscard]] bool isComplete(const JobHandle& handle) const noexcept
	{
		return handle.isComplete();
	}

	/// @brief ジョブシステムをシャットダウンする
	/// @details 全ワーカースレッドを停止し、キューをクリアする。
	void shutdown()
	{
		if (!m_initialized) return;

		m_running.store(false, std::memory_order_release);

		/// 全スレッドを起床させる
		m_globalCV.notify_all();

		for (auto& worker : m_workers)
		{
			if (worker.joinable())
			{
				worker.join();
			}
		}

		m_workers.clear();
		m_queues.clear();
		{
			std::lock_guard<std::mutex> lock(m_pendingMutex);
			m_pending.clear();
			m_pendingCount.store(0, std::memory_order_relaxed);
		}
		m_initialized = false;

		MITIRU_LOG_INFO("JobSystem", "shutdown complete");
	}

private:
	/// @brief ジョブエントリ
	struct JobEntry
	{
		std::function<void()> work;
		JobHandle handle;
		JobPriority priority = JobPriority::Normal;
		bool isParallelBatch = false;
	};

	/// @brief 依存待ちジョブ。依存完了までキューに入れず保持する。
	struct PendingJob
	{
		JobEntry  entry;
		JobHandle dependency;
	};

	/// @brief ワーカーキュー（ワークスティーリング対応）
	struct WorkerQueue
	{
		mutable std::mutex mutex;
		std::deque<JobEntry> highQueue;
		std::deque<JobEntry> normalQueue;
		std::deque<JobEntry> lowQueue;

		/// @brief 優先度に応じてジョブを追加する
		void push(JobEntry entry)
		{
			std::lock_guard<std::mutex> lock(mutex);
			switch (entry.priority)
			{
			case JobPriority::High:
				highQueue.push_back(std::move(entry));
				break;
			case JobPriority::Normal:
				normalQueue.push_back(std::move(entry));
				break;
			case JobPriority::Low:
				lowQueue.push_back(std::move(entry));
				break;
			}
		}

		/// @brief キューからジョブをポップする（優先度順）
		[[nodiscard]] bool pop(JobEntry& out)
		{
			std::lock_guard<std::mutex> lock(mutex);

			if (!highQueue.empty())
			{
				out = std::move(highQueue.front());
				highQueue.pop_front();
				return true;
			}
			if (!normalQueue.empty())
			{
				out = std::move(normalQueue.front());
				normalQueue.pop_front();
				return true;
			}
			if (!lowQueue.empty())
			{
				out = std::move(lowQueue.front());
				lowQueue.pop_front();
				return true;
			}
			return false;
		}

		/// @brief キューの末尾からジョブをスティールする
		[[nodiscard]] bool steal(JobEntry& out)
		{
			std::lock_guard<std::mutex> lock(mutex);

			if (!normalQueue.empty())
			{
				out = std::move(normalQueue.back());
				normalQueue.pop_back();
				return true;
			}
			if (!lowQueue.empty())
			{
				out = std::move(lowQueue.back());
				lowQueue.pop_back();
				return true;
			}
			return false;
		}

		/// @brief キューが空かどうかを判定する
		[[nodiscard]] bool empty() const
		{
			std::lock_guard<std::mutex> lock(mutex);
			return highQueue.empty() && normalQueue.empty() && lowQueue.empty();
		}
	};

	/// @brief ジョブをキューに投入する（ラウンドロビン）
	void pushJob(JobEntry entry)
	{
		if (m_queues.empty()) return;

		const std::size_t idx =
			m_pushCounter.fetch_add(1, std::memory_order_relaxed)
			% m_queues.size();

		m_queues[idx]->push(std::move(entry));
		m_globalCV.notify_one();
	}

	/// @brief ワーカースレッドのメインループ
	void workerLoop(int workerIndex)
	{
		const auto idx = static_cast<std::size_t>(workerIndex);

		/// スティール用の乱数生成器
		std::mt19937 rng(static_cast<std::mt19937::result_type>(workerIndex));

		while (m_running.load(std::memory_order_acquire))
		{
			JobEntry entry;
			bool found = false;

			/// 自分のキューからポップする
			if (m_queues[idx]->pop(entry))
			{
				found = true;
			}
			else
			{
				/// 他のキューからスティールする
				if (m_queues.size() > 1)
				{
					const auto victimStart =
						rng() % m_queues.size();
					for (std::size_t i = 0; i < m_queues.size(); ++i)
					{
						const auto victim =
							(victimStart + i) % m_queues.size();
						if (victim == idx) continue;

						if (m_queues[victim]->steal(entry))
						{
							found = true;
							break;
						}
					}
				}
			}

			if (found)
			{
				executeJob(entry);
			}
			else
			{
				/// ジョブがない場合は待機する
				std::unique_lock<std::mutex> lock(m_globalMutex);
				m_globalCV.wait_for(lock, std::chrono::milliseconds(1),
					[this]
					{
						return !m_running.load(std::memory_order_acquire)
							|| hasAnyWork();
					});
			}
		}
	}

	/// @brief ジョブを実行する
	void executeJob(JobEntry& entry)
	{
		if (entry.work)
		{
			entry.work();
		}

		/// 完了を通知する
		if (entry.isParallelBatch)
		{
			entry.handle.decrementPending();
		}
		else
		{
			entry.handle.markComplete();
		}

		/// 依存が満たされた pending ジョブをキューへ昇格させる。
		promotePending();
	}

	/// @brief 依存が完了した pending ジョブをワーカーキューへ昇格させる。
	/// @details 各ジョブ完了後に呼ぶ。worker をブロックせずに依存関係を解決する。
	void promotePending()
	{
		// fast path: pending 無し (大半のジョブは依存なし) は lock 回避。
		// カウンタは atomic なので unlocked 読みでも data race にならない。
		if (m_pendingCount.load(std::memory_order_relaxed) == 0) { return; }
		std::vector<JobEntry> ready;
		{
			std::lock_guard<std::mutex> lock(m_pendingMutex);
			auto it = std::remove_if(m_pending.begin(), m_pending.end(),
				[&ready](PendingJob& p)
				{
					if (p.dependency.isComplete())
					{
						ready.push_back(std::move(p.entry));
						return true;
					}
					return false;
				});
			m_pending.erase(it, m_pending.end());
			m_pendingCount.store(m_pending.size(), std::memory_order_relaxed);
		}
		for (auto& e : ready) { pushJob(std::move(e)); }
	}

	/// @brief いずれかのキューにジョブがあるかを判定する
	[[nodiscard]] bool hasAnyWork() const
	{
		for (const auto& q : m_queues)
		{
			if (!q->empty()) return true;
		}
		return false;
	}

	std::vector<std::thread> m_workers;            ///< ワーカースレッド
	std::vector<std::unique_ptr<WorkerQueue>> m_queues;  ///< ワークスティーリングキュー (mutex 保持のため間接化)
	std::atomic<bool> m_running{false};            ///< 実行中フラグ
	std::atomic<std::size_t> m_pushCounter{0};     ///< ラウンドロビンカウンター
	std::mutex m_globalMutex;                      ///< グローバル待機用ミューテックス
	std::condition_variable m_globalCV;             ///< グローバル待機用条件変数
	int m_threadCount = 0;                         ///< ワーカースレッド数
	bool m_initialized = false;                    ///< 初期化済みフラグ

	std::mutex m_pendingMutex;                     ///< 依存待ちジョブ用ミューテックス
	std::vector<PendingJob> m_pending;             ///< 依存完了待ちのジョブ (worker を塞がない)
	std::atomic<std::size_t> m_pendingCount{0};    ///< m_pending サイズの atomic ヒント (fast path 用)
};

} // namespace mitiru
