#pragma once

/// @file HostMmlBake.hpp
/// @brief --watch-assets で保存された .mml を、フレームを止めずに別 thread で .wav へ焼く
/// @details 曲 1 分の焼きに数秒かかるので、保存のたびにゲームが固まらないようにする。

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <mitiru/audio/mml/MmlBake.hpp>

namespace mitiru::host
{

class HostMmlBake
{
public:
	HostMmlBake() : m_worker([this] { run(); }) {}
	HostMmlBake(const HostMmlBake&) = delete;
	HostMmlBake& operator=(const HostMmlBake&) = delete;

	~HostMmlBake()
	{
		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			m_stop = true;
		}
		m_wake.notify_one();
		m_worker.join();
	}

	void submit(const std::filesystem::path& mml)
	{
		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			for (const auto& queued : m_pending) { if (queued == mml) { return; } }
			m_pending.push_back(mml);
		}
		m_wake.notify_one();
	}

	struct Finished
	{
		std::filesystem::path                   mml;
		mitiru::audio::mml::BakeResult     result;
	};

	/// 焼き終わった結果を、焼いた順に全部取り出す
	[[nodiscard]] std::vector<Finished> takeFinished()
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		std::vector<Finished> out(std::make_move_iterator(m_finished.begin()), std::make_move_iterator(m_finished.end()));
		m_finished.clear();
		return out;
	}

private:
	void run()
	{
		for (;;)
		{
			std::filesystem::path next;
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				m_wake.wait(lock, [this] { return m_stop || !m_pending.empty(); });
				if (m_stop) { return; }
				next = std::move(m_pending.front());
				m_pending.pop_front();
			}
			auto result = mitiru::audio::mml::bakeFile(next);
			const std::lock_guard<std::mutex> lock(m_mutex);
			m_finished.push_back(Finished{std::move(next), std::move(result)});
		}
	}

	std::mutex                        m_mutex;
	std::condition_variable           m_wake;
	std::deque<std::filesystem::path> m_pending;
	std::vector<Finished>             m_finished;
	bool                              m_stop = false;
	std::thread                       m_worker;   ///< 最後に作る (他のメンバーが揃ってから動き出す)
};

}  // namespace mitiru::host
