#pragma once

/// @file FluidWorkers.hpp
/// @brief FluidParallel の口に挿す、待機しているスレッドの組 (任意)。
/// @details 流れる体は仕事の番号ごとに書く先が決まっていて、どのスレッドがどの番号を取っても結果のビットは同じ。
///          1 フレームに何十回も呼ぶので、スレッドは作り直さず、仕事が来るまで短く回って待ち、来なければ眠る。
///          仕事の番号は「何回目の呼び出しか」と組にして取るので、前の呼び出しに遅れて起きたスレッドが次の呼び出しの
///          仕事を取ることはない。GameMemory には何も書かない (作業の分け方だけを変える)。

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <mitiru/platform/win32/UndefNearFar.inl>
#endif

#include <mitiru/softbody/FluidBody.hpp>

namespace mitiru::softbody
{

class FluidWorkers
{
public:
	/// @param threads 呼び手を含めたスレッドの数 (1 なら呼び手だけで回す)
	explicit FluidWorkers(int threads)
	{
		for (int i = 1; i < threads; ++i)
		{
			m_threads.emplace_back([this] { loop(0); });
			raisePriority(m_threads.back());
		}
		m_parallel.user = this;
		m_parallel.run = &FluidWorkers::dispatch;
		m_parallel.jobCount = threads > 1 ? threads : 1;
	}

	FluidWorkers(const FluidWorkers&) = delete;
	FluidWorkers& operator=(const FluidWorkers&) = delete;

	~FluidWorkers()
	{
		m_quit.store(true, std::memory_order_release);
		m_generation.fetch_add(1, std::memory_order_acq_rel);
		wake();
		for (std::thread& t : m_threads) { t.join(); }
	}

	/// @brief FluidStepInput::parallel と buildFluidMesh に渡す口
	[[nodiscard]] const FluidParallel* parallel() const noexcept { return &m_parallel; }

private:
	struct Call
	{
		void (*job)(void*, int);
		void* ctx;
		int count;
	};

	static void dispatch(void* user, int jobCount, void* ctx, void (*job)(void*, int))
	{
		auto& self = *static_cast<FluidWorkers*>(user);
		const std::uint32_t gen = self.m_generation.load(std::memory_order_relaxed) + 1;
		self.m_job.store(job, std::memory_order_relaxed);
		self.m_ctx.store(ctx, std::memory_order_relaxed);
		self.m_count.store(jobCount, std::memory_order_relaxed);
		self.m_done.store(0, std::memory_order_relaxed);
		self.m_claim.store(static_cast<std::uint64_t>(gen) << 32, std::memory_order_relaxed);
		self.m_generation.store(gen, std::memory_order_release);
		self.wake();
		self.work(gen, {job, ctx, jobCount});
		int spins = 0;
		while (self.m_done.load(std::memory_order_acquire) < jobCount)
		{
			if (spins < kSpinsBeforeYield) { ++spins; pause(); }
			else { std::this_thread::yield(); }
		}
	}

	// 手伝いの残りは普通、数十 µs で終わる。yield は同じコアで待つ他のプロセスに番を譲るので、ビルドで CPU が
	// 埋まっていると戻るまで 1 刻み (十数 ms) 待たされ、何十回の呼び出しで積もる。回って待ち、長引いたときだけ譲る
	static constexpr int kSpinsBeforeYield = 1 << 15;

	static void pause() noexcept
	{
#ifdef _WIN32
		YieldProcessor();
#endif
	}

	// 呼び手は手伝いが全部終わるまで進めず、手伝いが他のプロセスに CPU を取られたぶんフレームが止まる。
	// 普通の優先度の仕事に割り込めるよう 1 段だけ上げる
	static void raisePriority(std::thread& t) noexcept
	{
#ifdef _WIN32
		(void)::SetThreadPriority(t.native_handle(), THREAD_PRIORITY_ABOVE_NORMAL);
#else
		(void)t;
#endif
	}

	/// 眠っているスレッドを起こす。待つ側は鍵を持って番号を確かめてから眠るので、鍵を取ってから起こせば取りこぼさない
	void wake()
	{
		{
			const std::lock_guard<std::mutex> lock(m_mutex);
		}
		m_sleep.notify_all();
	}

	void work(std::uint32_t gen, const Call& call)
	{
		while (true)
		{
			std::uint64_t claim = m_claim.load(std::memory_order_acquire);
			if (static_cast<std::uint32_t>(claim >> 32) != gen || static_cast<int>(claim & 0xFFFFFFFFu) >= call.count) { return; }
			if (!m_claim.compare_exchange_weak(claim, claim + 1, std::memory_order_acq_rel)) { continue; }
			call.job(call.ctx, static_cast<int>(claim & 0xFFFFFFFFu));
			m_done.fetch_add(1, std::memory_order_acq_rel);
		}
	}

	/// 1 フレームの中の次の呼び出しまでは短く回って待ち、それより長く来なければ眠る (フレームの間に CPU を使わない)。
	/// seen は作った時の番号 (0)。走り出してから読むと、すぐに壊された時の終わりの合図を見逃して眠り続ける
	void loop(std::uint32_t seen)
	{
		while (true)
		{
			std::uint32_t now = m_generation.load(std::memory_order_acquire);
			for (int spin = 0; now == seen && spin < 4000; ++spin) { now = m_generation.load(std::memory_order_acquire); }
			if (now == seen)
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				m_sleep.wait(lock, [&] { return m_generation.load(std::memory_order_acquire) != seen; });
				now = m_generation.load(std::memory_order_acquire);
			}
			seen = now;
			if (m_quit.load(std::memory_order_acquire)) { return; }
			work(now, {m_job.load(std::memory_order_relaxed), m_ctx.load(std::memory_order_relaxed), m_count.load(std::memory_order_relaxed)});
		}
	}

	std::vector<std::thread> m_threads;
	FluidParallel m_parallel{};
	std::atomic<std::uint32_t> m_generation{0};
	std::atomic<std::uint64_t> m_claim{0};
	std::atomic<int> m_done{0};
	std::atomic<int> m_count{0};
	std::atomic<void (*)(void*, int)> m_job{nullptr};
	std::atomic<void*> m_ctx{nullptr};
	std::atomic<bool> m_quit{false};
	std::mutex m_mutex;
	std::condition_variable m_sleep;
};

} // namespace mitiru::softbody
