#pragma once

/// @file FrameStallWatch.hpp
/// @brief フレームを処理するスレッドの停止を別スレッドから監視する
/// @details Windows はメッセージを 5 秒間処理しないウィンドウを「応答なし」とし、利用者が閉じると終了コード 0xCFFFFFFF で記録を残さず終了する。停止中のスタックを記録するため、監視スレッドから 1 回の停止につき 1 度だけ知らせる。フレームが戻ると次の停止に備える。フレームが回り続けたまま窓だけが応答しない場合を拾うため、任意の probe で窓の応答を調べ、応答なしが limit 続いたときも同じ形で知らせる。起動直後は UI の文字の焼き込みなどに数秒かかるため、armAfter 回 beat するまでは監視しない。

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace mitiru::debug
{

class FrameStallWatch
{
public:
	using clock   = std::chrono::steady_clock;

	enum class Kind { FramesStalled, WindowNotAnswering };

	struct Info
	{
		Kind                      kind;
		std::chrono::milliseconds stalledFor;
		std::uint64_t             frame;  ///< 最後に beat した時点までの通算フレーム数 (armAfter 前も数える)
	};

	using OnStall   = void (*)(void* user, const Info& info);
	/// 監視スレッドから tick ごとに呼ぶ。窓が応答しなければ true。時間のかかる呼び出し (SendMessageTimeout 等) をしてよい
	using HungProbe = bool (*)(void* probeUser);

	/// @param maxReports 1 回の起動で知らせる上限 (停止と応答なしの合計)。同じ原因による記録で disk を埋めない
	FrameStallWatch(std::chrono::milliseconds limit, OnStall onStall, void* user, std::uint32_t armAfter = 0,
	                int maxReports = 3, HungProbe probe = nullptr, void* probeUser = nullptr)
		: m_limit(limit), m_onStall(onStall), m_user(user), m_armAfter(armAfter), m_reportsLeft(maxReports),
		  m_probe(probe), m_probeUser(probeUser)
	{
		m_thread = std::thread([this] { watch(); });
	}

	FrameStallWatch(const FrameStallWatch&)            = delete;
	FrameStallWatch& operator=(const FrameStallWatch&) = delete;

	~FrameStallWatch()
	{
		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			m_stop = true;
		}
		m_wake.notify_all();
		m_thread.join();
	}

	/// @brief フレームを処理するスレッドが、各フレームの最初に呼ぶ
	void beat() noexcept
	{
		m_frames.store(m_frames.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
		if (m_beats < m_armAfter) { ++m_beats; return; }
		m_lastBeat.store(clock::now().time_since_epoch().count(), std::memory_order_release);
	}

	[[nodiscard]] int reports() const noexcept { return m_reports.load(std::memory_order_acquire); }

private:
	void watch()
	{
		const auto tick = m_limit / 8 > std::chrono::milliseconds(1) ? m_limit / 8 : std::chrono::milliseconds(1);
		clock::rep reportedBeat = 0;
		clock::time_point hungSince{};
		bool hungReported = false;
		std::unique_lock<std::mutex> lock(m_mutex);
		while (!m_wake.wait_for(lock, tick, [this] { return m_stop; }))
		{
			const clock::rep last = m_lastBeat.load(std::memory_order_acquire);
			if (last == 0 || m_reportsLeft <= 0) { continue; }

			bool hung = false;
			if (m_probe != nullptr)
			{
				const auto polledAt = clock::now();
				lock.unlock();
				hung = m_probe(m_probeUser);
				lock.lock();
				if (m_stop) { break; }
				if (!hung) { hungSince = {}; hungReported = false; }
				else if (hungSince == clock::time_point{}) { hungSince = polledAt; }
			}

			const auto now = clock::now();
			if (last != reportedBeat && now - clock::time_point(clock::duration(last)) >= m_limit)
			{
				reportedBeat = last;
				// 同じ停止で窓も応答していないなら 1 件にまとめる
				hungReported = hung;
				report(lock, Kind::FramesStalled, now - clock::time_point(clock::duration(last)));
			}
			else if (hung && !hungReported && now - hungSince >= m_limit)
			{
				hungReported = true;
				report(lock, Kind::WindowNotAnswering, now - hungSince);
			}
		}
	}

	void report(std::unique_lock<std::mutex>& lock, Kind kind, clock::duration stalled)
	{
		--m_reportsLeft;
		const Info info{kind, std::chrono::duration_cast<std::chrono::milliseconds>(stalled),
		                m_frames.load(std::memory_order_relaxed)};
		lock.unlock();
		m_onStall(m_user, info);
		m_reports.fetch_add(1, std::memory_order_acq_rel);
		lock.lock();
	}

	std::chrono::milliseconds m_limit;
	OnStall                   m_onStall;
	void*                     m_user;
	std::uint32_t             m_armAfter;
	std::uint32_t             m_beats = 0;  ///< beat を呼ぶスレッドだけが触る
	int                       m_reportsLeft;
	HungProbe                 m_probe;
	void*                     m_probeUser;
	std::atomic<std::uint64_t> m_frames{0};
	std::atomic<clock::rep>   m_lastBeat{0};
	std::atomic<int>          m_reports{0};
	std::mutex                m_mutex;
	std::condition_variable   m_wake;
	bool                      m_stop = false;
	std::thread               m_thread;
};

} // namespace mitiru::debug
