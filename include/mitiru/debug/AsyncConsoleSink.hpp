#pragma once

/// @file AsyncConsoleSink.hpp
/// @brief console の知らせを別スレッドで端末へ書く出力先
/// @details 端末が読むのを止めると、標準エラーへの書き込みは端末が読み出すまで戻らない (コンソールの
///          QuickEdit で選択を始めた、端末の画面を切り替えて流れが止まった、など)。フレームを回すスレッドで
///          書くと、その間メッセージを汲めずに窓が「応答なし」になる。書く側は列に積むだけにし、
///          詰まった端末を待つのは書き出し用のスレッドだけにする。列が溢れた分は捨てて、数だけ後で伝える。

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include <mitiru/debug/ConsoleOut.hpp>

namespace mitiru::console
{

class AsyncConsoleSink
{
public:
	/// 列の上限。知らせは数行しか出ないので、溢れるのは端末が長く止まっているときだけ
	static constexpr std::size_t kMaxQueued = 256;
	/// 終わるときに端末が読み出すのを待つ上限。止まった端末のせいで終われなくなるのを避ける
	static constexpr std::chrono::milliseconds kExitWait{1000};

	/// @param writer 実際に書く関数。既定は stderr。テストは止まる書き手を渡す
	explicit AsyncConsoleSink(Sink writer = &detail::stderrSink)
		: m_state(std::make_shared<State>())
	{
		m_state->writer = writer;
		std::thread(&AsyncConsoleSink::drain, m_state).detach();
		std::lock_guard<std::mutex> lock(activeMutex());
		activeState() = m_state;
		m_previous = setSink(&enqueue);
	}

	AsyncConsoleSink(const AsyncConsoleSink&) = delete;
	AsyncConsoleSink& operator=(const AsyncConsoleSink&) = delete;

	/// 積んだ行を書き終えるまで kExitWait だけ待つ。書き手が戻らなければ待つのをやめる (スレッドは
	/// State を共有しているので、このオブジェクトが先に消えても壊れたメモリには触らない)
	~AsyncConsoleSink()
	{
		{
			std::lock_guard<std::mutex> lock(activeMutex());
			(void)setSink(m_previous);
			activeState().reset();
		}
		std::unique_lock<std::mutex> lock(m_state->mutex);
		m_state->stop = true;
		m_state->wake.notify_all();
		(void)m_state->idle.wait_for(lock, kExitWait, [&] { return m_state->lines.empty() && !m_state->writing; });
	}

	/// @brief 積んだまま、まだ書き終えていない行の数
	[[nodiscard]] std::size_t pending() const
	{
		std::lock_guard<std::mutex> lock(m_state->mutex);
		return m_state->lines.size() + (m_state->writing ? 1u : 0u);
	}

private:
	struct State
	{
		Sink                    writer = nullptr;
		std::mutex              mutex;
		std::condition_variable wake;
		std::condition_variable idle;
		std::deque<std::string> lines;
		std::size_t             dropped = 0;
		bool                    writing = false;
		bool                    stop    = false;
	};

	static std::mutex& activeMutex()
	{
		static std::mutex m;
		return m;
	}

	static std::shared_ptr<State>& activeState()
	{
		static std::shared_ptr<State> s;
		return s;
	}

	static void enqueue(std::string_view line)
	{
		std::shared_ptr<State> state;
		{
			std::lock_guard<std::mutex> lock(activeMutex());
			state = activeState();
		}
		if (!state) { detail::stderrSink(line); return; }
		std::lock_guard<std::mutex> lock(state->mutex);
		if (state->lines.size() >= kMaxQueued) { ++state->dropped; return; }
		state->lines.emplace_back(line);
		state->wake.notify_one();
	}

	static void drain(std::shared_ptr<State> state)
	{
		std::unique_lock<std::mutex> lock(state->mutex);
		while (true)
		{
			state->wake.wait(lock, [&] { return !state->lines.empty() || state->stop; });
			if (state->lines.empty()) { break; }
			std::string line = std::move(state->lines.front());
			state->lines.pop_front();
			if (state->lines.empty() && state->dropped > 0)
			{
				line += "mitiru: 端末が読み出さない間に知らせが溢れ、" + std::to_string(state->dropped) + " 行を捨てました\n";
				state->dropped = 0;
			}
			state->writing = true;
			lock.unlock();
			state->writer(line);
			lock.lock();
			state->writing = false;
			if (state->lines.empty()) { state->idle.notify_all(); }
		}
		state->idle.notify_all();
	}

	std::shared_ptr<State> m_state;
	Sink                   m_previous = nullptr;
};

} // namespace mitiru::console
