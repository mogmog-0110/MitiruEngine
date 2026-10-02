#pragma once

/// @file MusicDirector.hpp
/// @brief 区間の切り替え、層の強さ、スティンガーをいつ鳴らすかサンプル単位で決める
/// @details 時計は advance(frames) で渡した分だけ進む。各ステップで決まったフレーム数を渡すと、同じ要求から同じ命令列が出る。命令はすべて [advance 前の now, now + frames)
///          に入る時刻順で出すので、受け取る MusicRenderer は先頭から順に実行すればよい。

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include <mitiru/audio/music/MusicCommand.hpp>
#include <mitiru/audio/music/MusicGrid.hpp>
#include <mitiru/audio/music/MusicManifest.hpp>

namespace mitiru::audio::music
{

struct MusicPosition
{
	bool         playing = false;
	int          segment = -1;
	std::int64_t sampleInSegment = 0;
	std::int64_t bar = 0;
	int          beatInBar = 0;
};

class MusicDirector
{
public:
	MusicDirector(std::shared_ptr<const MusicManifest> manifest, std::uint32_t sampleRate)
		: m_manifest(std::move(manifest))
		, m_rate(sampleRate)
		, m_layerTarget(m_manifest->layers.size(), 0.0f)
		, m_layerPending(m_manifest->layers.size())
	{
		for (std::size_t i = 0; i < m_layerTarget.size(); ++i)
		{
			m_layerTarget[i] = m_manifest->layerGain(static_cast<int>(i), 0.0f);
			m_layerPending[i] = Ramp{m_layerTarget[i], 0, 0, true};
		}
	}

	/// @brief segment への切り替えを求める。MusicManifest::rule の区切りまで待ち、何も鳴っていなければすぐに始める
	void request(int segment)
	{
		if (segment < 0 || segment >= static_cast<int>(m_manifest->segments.size())) { return; }
		if (m_pending.active && m_pending.segment == segment) { return; }
		if (m_current.playing && (segment == m_current.segment || segment == m_current.afterExit))
		{
			m_pending.active = false;  // 今の区間 (か、挟んだ区間の次) のままでよい
			return;
		}
		const MusicTransition rule = m_manifest->rule(m_current.playing ? m_current.segment : -1, segment);
		m_pending = Pending{segment, syncTime(m_current.playing ? rule.sync : MusicSync::Immediate), rule, true};
		m_stopPending = false;
	}

	/// @brief 区間の後ろに残る響きを含むすべての音を fadeSec かけて止める
	void stop(float fadeSec)
	{
		m_stopPending = true;
		m_stopLength = samples(fadeSec);
		m_pending.active = false;
	}

	/// @brief 層ごとの曲線で音量を決め、層の sync の区切りから fadeSec かけて変える
	void setIntensity(float intensity)
	{
		m_intensity = intensity;
		for (std::size_t i = 0; i < m_layerTarget.size(); ++i)
		{
			const float g = m_manifest->layerGain(static_cast<int>(i), intensity);
			if (g == m_layerTarget[i]) { continue; }
			m_layerTarget[i] = g;
			const MusicLayer& layer = m_manifest->layers[i];
			m_layerPending[i] = Ramp{g, syncTime(layer.sync), samples(layer.fadeSec), true};
		}
	}

	/// @brief スティンガーを sync の区切りに重ねる。待てるのは 8 本までで、それ以上は捨てる
	void playStinger(int stinger)
	{
		if (stinger < 0 || stinger >= static_cast<int>(m_manifest->stingers.size())) { return; }
		if (m_stingerCount >= m_stingers.size()) { return; }
		const MusicStinger& s = m_manifest->stingers[static_cast<std::size_t>(stinger)];
		m_stingers[m_stingerCount++] = PendingStinger{stinger, syncTime(s.sync)};
	}

	/// @brief 曲全体の音量を rampSec かけて変える。バス音量とダッキングに使う
	void setMasterGain(float gain, float rampSec)
	{
		m_master = Ramp{gain, m_now, samples(rampSec), true};
	}

	/// @brief 時計を frames 進め、その間の命令を out に追加する。out の内容は消さない
	void advance(std::uint64_t frames, MusicCommandBuffer& out)
	{
		const std::int64_t end = m_now + static_cast<std::int64_t>(frames);
		for (std::int64_t t = nextEventTime(); t < end; t = nextEventTime())
		{
			processAt(std::max(t, m_now), out);
		}
		m_now = end;
	}

	[[nodiscard]] MusicPosition position() const noexcept
	{
		MusicPosition p;
		if (!m_current.playing) { return p; }
		const MusicSegment& s = segment(m_current.segment);
		const MusicGrid g = MusicGrid::of(s, m_rate);
		const std::int64_t beat = g.beatAt(m_now - m_current.start);
		p.playing = true;
		p.segment = m_current.segment;
		p.sampleInSegment = m_now - m_current.start;
		p.bar = beat / s.beatsPerBar;
		p.beatInBar = static_cast<int>(beat % s.beatsPerBar);
		return p;
	}

	[[nodiscard]] std::int64_t now() const noexcept { return m_now; }
	[[nodiscard]] int currentSegment() const noexcept { return m_current.playing ? m_current.segment : -1; }
	[[nodiscard]] int pendingSegment() const noexcept { return m_pending.active ? m_pending.segment : -1; }
	[[nodiscard]] float intensity() const noexcept { return m_intensity; }
	[[nodiscard]] const MusicManifest& manifest() const noexcept { return *m_manifest; }

private:
	static constexpr std::int64_t kNever = std::numeric_limits<std::int64_t>::max();

	struct Current
	{
		int           segment = -1;
		std::uint32_t instance = 0;
		std::int64_t  start = 0;
		int           afterExit = -1;  ///< 挟んだ区間 (via) が終わったら入る区間
		bool          playing = false;
	};
	struct Pending
	{
		int             segment = -1;
		std::int64_t    at = 0;
		MusicTransition rule;
		bool            active = false;
	};
	struct Ramp
	{
		float        gain = 1.0f;
		std::int64_t at = 0;
		std::int64_t length = 0;
		bool         active = false;
	};
	struct PendingStinger
	{
		int          stinger = -1;
		std::int64_t at = 0;
	};

	[[nodiscard]] const MusicSegment& segment(int i) const { return m_manifest->segments[static_cast<std::size_t>(i)]; }
	[[nodiscard]] std::int64_t samples(float sec) const noexcept
	{
		return std::llround(static_cast<double>(std::max(sec, 0.0f)) * static_cast<double>(m_rate));
	}
	[[nodiscard]] std::int64_t exitTime() const
	{
		return m_current.start + segmentLength(segment(m_current.segment), m_rate);
	}

	/// 鳴っている区間の拍に合わせ、今から sync を待った時刻。何も鳴っていなければ現在時刻
	[[nodiscard]] std::int64_t syncTime(MusicSync sync) const
	{
		if (!m_current.playing) { return m_now; }
		return m_current.start + syncPoint(segment(m_current.segment), m_rate, sync, m_now - m_current.start);
	}

	[[nodiscard]] std::int64_t nextEventTime() const
	{
		std::int64_t t = kNever;
		if (m_master.active)       { t = std::min(t, m_master.at); }
		if (m_stopPending)         { t = std::min(t, m_now); }
		if (m_pending.active)      { t = std::min(t, m_pending.at); }
		if (m_current.playing)     { t = std::min(t, exitTime()); }
		for (const Ramp& r : m_layerPending) { if (r.active) { t = std::min(t, r.at); } }
		for (std::size_t i = 0; i < m_stingerCount; ++i) { t = std::min(t, m_stingers[i].at); }
		return t;
	}

	/// 同じ時刻では、音量、停止、切り替え、区間の終わり、層、スティンガーの順に出す
	/// 切り替えと区間の終わりが重なったときは、繰り返さずに切り替える
	void processAt(std::int64_t t, MusicCommandBuffer& out)
	{
		if (m_master.active && m_master.at <= t)
		{
			out.push(gainCommand(MusicCommandKind::MasterGain, -1, m_master, t));
			m_master.active = false;
		}
		if (m_stopPending) { emitStop(t, out); }
		if (m_pending.active && m_pending.at <= t) { transition(t, out); }
		if (m_current.playing && exitTime() <= t) { onExit(t, out); }
		for (std::size_t i = 0; i < m_layerPending.size(); ++i)
		{
			Ramp& r = m_layerPending[i];
			if (!r.active || r.at > t) { continue; }
			out.push(gainCommand(MusicCommandKind::LayerGain, static_cast<int>(i), r, t));
			r.active = false;
		}
		emitStingers(t, out);
	}

	static MusicCommand gainCommand(MusicCommandKind kind, int layer, const Ramp& r, std::int64_t t)
	{
		MusicCommand c;
		c.kind = kind;
		c.at = t;
		c.length = r.length;
		c.layer = static_cast<std::int16_t>(layer);
		c.gain = r.gain;
		return c;
	}

	void emitStop(std::int64_t t, MusicCommandBuffer& out)
	{
		MusicCommand c;
		c.kind = MusicCommandKind::StopInstance;
		c.at = t;
		c.length = m_stopLength;
		c.instance = 0;  // 0 = 鳴っているものすべて
		out.push(c);
		m_current.playing = false;
		m_stopPending = false;
	}

	void startSegment(int seg, std::int64_t t, std::int64_t fadeIn, int afterExit, MusicCommandBuffer& out)
	{
		MusicCommand c;
		c.kind = MusicCommandKind::StartInstance;
		c.at = t;
		c.length = fadeIn;
		c.instance = m_nextInstance++;
		c.segment = static_cast<std::int16_t>(seg);
		out.push(c);
		m_current = Current{seg, c.instance, t, afterExit, true};
	}

	void transition(std::int64_t t, MusicCommandBuffer& out)
	{
		const MusicTransition rule = m_pending.rule;
		const int target = m_pending.segment;
		m_pending.active = false;
		// Exit で待った切り替えでは、前の区間の後ろに残る響きを切らない
		if (m_current.playing && rule.sync != MusicSync::Exit)
		{
			MusicCommand c;
			c.kind = MusicCommandKind::StopInstance;
			c.at = t;
			c.length = samples(rule.fadeOutSec);
			c.instance = m_current.instance;
			out.push(c);
		}
		if (rule.via >= 0) { startSegment(rule.via, t, samples(rule.fadeInSec), target, out); }
		else               { startSegment(target, t, samples(rule.fadeInSec), -1, out); }
	}

	void onExit(std::int64_t t, MusicCommandBuffer& out)
	{
		const MusicSegment& s = segment(m_current.segment);
		if (m_current.afterExit >= 0) { startSegment(m_current.afterExit, t, 0, -1, out); }
		else if (s.loop)              { startSegment(m_current.segment, t, 0, -1, out); }
		else if (s.next >= 0)         { startSegment(s.next, t, 0, -1, out); }
		else                          { m_current.playing = false; }
	}

	void emitStingers(std::int64_t t, MusicCommandBuffer& out)
	{
		std::size_t kept = 0;
		for (std::size_t i = 0; i < m_stingerCount; ++i)
		{
			const PendingStinger p = m_stingers[i];
			if (p.at > t) { m_stingers[kept++] = p; continue; }
			MusicCommand c;
			c.kind = MusicCommandKind::StartInstance;
			c.at = t;
			c.instance = m_nextInstance++;
			c.stinger = static_cast<std::int16_t>(p.stinger);
			c.gain = m_manifest->stingers[static_cast<std::size_t>(p.stinger)].gain;
			out.push(c);
		}
		m_stingerCount = kept;
	}

	std::shared_ptr<const MusicManifest> m_manifest;
	std::uint32_t                        m_rate;
	std::int64_t                         m_now = 0;
	std::uint32_t                        m_nextInstance = 1;
	Current                              m_current;
	Pending                              m_pending;
	bool                                 m_stopPending = false;
	std::int64_t                         m_stopLength = 0;
	float                                m_intensity = 0.0f;
	std::vector<float>                   m_layerTarget;
	std::vector<Ramp>                    m_layerPending;
	Ramp                                 m_master;
	std::array<PendingStinger, 8>        m_stingers{};
	std::size_t                          m_stingerCount = 0;
};

}  // namespace mitiru::audio::music
