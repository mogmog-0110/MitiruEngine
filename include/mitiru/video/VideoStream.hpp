#pragma once

/// @file VideoStream.hpp
/// @brief 1 本の動画を専用スレッドで先読みし、指定した番号の絵を返す宣言 (ADR 0104)
/// @details 描く絵は常に floor (秒 × fps) 番の 1 枚で、内容は番号だけで決まる。スレッドはキャッシュとして使う。
/// exact = true (撮影、headless、リプレイ) では、その番号の復号を待つ。false (ウィンドウで遊ぶ) では、間に合わないと直前の絵を保ち、落とした数を数える。
/// 過去の番号は最初から復号し直す。キーフレームの索引は持たない。

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/render/Texture.hpp>
#include <mitiru/video/IVideoSource.hpp>

namespace mitiru::video
{

struct VideoFrame
{
	int            index = 0;
	render::Texture tex;   ///< RGBA8、不透明
};

struct VideoStreamConfig
{
	int ahead = 3;    ///< 取り出す番号より先に復号しておく枚数
	int behind = 1;   ///< 取り出した番号より後ろに残す枚数 (同じ絵を続けて引く、1 つ戻る)
	/// exact で待つ上限。壊れた動画や固まった source で復号が止まっても、ゲーム全体を止めないため
	std::chrono::milliseconds exactTimeout{60000};
};

class VideoStream
{
public:
	explicit VideoStream(std::unique_ptr<IVideoSource> source, VideoStreamConfig cfg = {})
		: m_source(std::move(source)), m_cfg(cfg), m_info(m_source->info())
	{
		m_cfg.ahead = std::max(1, m_cfg.ahead);
		m_cfg.behind = std::max(0, m_cfg.behind);
	}

	VideoStream(const VideoStream&) = delete;
	VideoStream& operator=(const VideoStream&) = delete;

	~VideoStream()
	{
		{
			std::lock_guard lock(m_mu);
			m_stop = true;
		}
		m_cv.notify_all();
		if (m_thread.joinable()) { m_thread.join(); }
	}

	[[nodiscard]] const VideoInfo& info() const noexcept { return m_info; }

	/// @brief index 番の絵。exact では復号を待つ。nullptr は、ウィンドウで遊び始めてから 1 枚も出せていないときだけ返す。
	/// 動画の枚数が分かった後に末尾を超えた場合は、最後の絵を返す
	[[nodiscard]] std::shared_ptr<const VideoFrame> acquire(int index, bool exact)
	{
		std::unique_lock lock(m_mu);
		if (!m_thread.joinable()) { m_thread = std::thread([this] { run(); }); }
		index = std::max(0, index);
		const auto deadline = std::chrono::steady_clock::now() + m_cfg.exactTimeout;
		for (;;)
		{
			const int want = effectiveIndex(index);
			m_wanted = want;
			if (auto hit = findLocked(want))
			{
				trimLocked(want);
				m_last = hit;
				m_cv.notify_all();
				return hit;
			}
			if (want < m_nextIndex && !m_rewindPending)
			{
				m_rewindPending = true;
				++m_generation;
			}
			m_cv.notify_all();
			if (m_ended && m_total == 0) { return nullptr; }   // 1 枚も無い動画
			if (!exact)
			{
				++m_dropped;
				if (auto prev = latestBeforeLocked(want)) { m_last = prev; }
				return m_last;
			}
			if (m_cvDone.wait_until(lock, deadline) == std::cv_status::timeout)
			{
				debug::warnOnce("video.exact_timeout", "動画の復号が 60 秒たっても進まないので、直前の絵で続けます");
				return m_last;
			}
		}
	}

	/// ウィンドウで遊んでいる間に、復号が間に合わず直前の絵を返した回数
	[[nodiscard]] std::uint64_t droppedFrames() const
	{
		std::lock_guard lock(m_mu);
		return m_dropped;
	}

	/// 動画の枚数。最後まで復号するまでは -1
	[[nodiscard]] int totalFrames() const
	{
		std::lock_guard lock(m_mu);
		return m_total;
	}

private:
	[[nodiscard]] int effectiveIndex(int index) const
	{
		return (m_total > 0 && index >= m_total) ? m_total - 1 : index;
	}

	[[nodiscard]] std::shared_ptr<const VideoFrame> findLocked(int index) const
	{
		for (const auto& f : m_ready)
		{
			if (f->index == index) { return f; }
		}
		return nullptr;
	}

	[[nodiscard]] std::shared_ptr<const VideoFrame> latestBeforeLocked(int index) const
	{
		std::shared_ptr<const VideoFrame> best;
		for (const auto& f : m_ready)
		{
			if (f->index <= index && (!best || f->index > best->index)) { best = f; }
		}
		return best;
	}

	void trimLocked(int index)
	{
		while (!m_ready.empty() && m_ready.front()->index < index - m_cfg.behind) { m_ready.pop_front(); }
	}

	[[nodiscard]] bool canDecodeLocked() const
	{
		return !m_ended && static_cast<long long>(m_nextIndex) <= static_cast<long long>(m_wanted) + m_cfg.ahead;
	}

	void run()
	{
		std::unique_lock lock(m_mu);
		while (!m_stop)
		{
			m_cv.wait(lock, [this] { return m_stop || m_rewindPending || canDecodeLocked(); });
			if (m_stop) { break; }
			if (m_rewindPending)
			{
				m_rewindPending = false;
				lock.unlock();
				m_source->rewind();
				lock.lock();
				m_ready.clear();
				m_nextIndex = 0;
				m_ended = false;
				continue;
			}
			const int idx = m_nextIndex;
			const std::uint64_t gen = m_generation;
			const bool keep = idx >= m_wanted - m_cfg.behind;
			lock.unlock();
			std::vector<std::uint8_t> rgba;
			if (keep) { rgba.resize(static_cast<std::size_t>(m_info.width) * static_cast<std::size_t>(m_info.height) * 4); }
			const bool ok = m_source->decodeNext(keep ? rgba.data() : nullptr);
			lock.lock();
			if (gen != m_generation) { continue; }
			if (!ok)
			{
				m_ended = true;
				m_total = m_nextIndex;
			}
			else
			{
				++m_nextIndex;
				if (keep)
				{
					auto frame = std::make_shared<VideoFrame>();
					frame->index = idx;
					frame->tex = render::Texture(m_info.width, m_info.height, std::move(rgba));
					m_ready.push_back(std::move(frame));
				}
			}
			m_cvDone.notify_all();
		}
		m_cvDone.notify_all();
	}

	mutable std::mutex      m_mu;
	std::condition_variable m_cv;       ///< consumer → decoder
	std::condition_variable m_cvDone;   ///< decoder → consumer
	std::unique_ptr<IVideoSource> m_source;
	VideoStreamConfig       m_cfg;
	VideoInfo               m_info;
	std::thread             m_thread;
	std::deque<std::shared_ptr<const VideoFrame>> m_ready;   ///< 番号の昇順
	std::shared_ptr<const VideoFrame> m_last;
	int                     m_wanted = 0;
	int                     m_nextIndex = 0;   ///< 次に復号する番号
	int                     m_total = -1;
	bool                    m_ended = false;
	bool                    m_rewindPending = false;
	bool                    m_stop = false;
	std::uint64_t           m_generation = 0;
	std::uint64_t           m_dropped = 0;
};

}  // namespace mitiru::video
