#pragma once

/// @file BusDelay.hpp
/// @brief バスに掛けるディレイ (エコー) とコーラス。どちらも mix.json で書いたときだけ掛かる
/// @details ディレイは時間を整数のサンプルに丸めた遅延線で、戻りの経路に 1 次の low-pass (dampHz) を置ける。元の音はそのまま通し、
///          wet の大きさで遅れた音を足す。コーラスは正弦波の LFO で遅延を揺らした 1 本の tap を、mix の割合で元の音と混ぜる。右は LFO の位相を
///          4 分の 1 周期ずらして左右の広がりを作る。遅延の端数は線形補間で読む。どちらも設定はチェーンに入れる前に決め、差した後は変えない。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mitiru::audio::mix
{

struct DelaySettings
{
	float timeSec = 0.3f;
	float feedback = 0.3f;   ///< 0..0.95。戻りの大きさ
	float wet = 0.3f;        ///< 遅れた音の大きさ。元の音は 1 のまま
	float dampHz = 0.0f;     ///< 戻りの経路の low-pass。0 は掛けない
};

struct ChorusSettings
{
	float rateHz = 0.8f;
	float depthMs = 4.0f;    ///< 揺れの振幅
	float delayMs = 15.0f;   ///< 揺れの中心の遅延
	float mix = 0.5f;        ///< 0 は元の音だけ、1 は遅れた音だけ
};

inline constexpr float kDelayMaxSec = 2.0f;
inline constexpr float kChorusMaxDelayMs = 50.0f;

[[nodiscard]] inline const char* validateDelay(const DelaySettings& d) noexcept
{
	if (!(d.timeSec >= 0.001f && d.timeSec <= kDelayMaxSec)) { return "timeSec は 0.001 以上、2 以下"; }
	if (!(d.feedback >= 0.0f && d.feedback <= 0.95f)) { return "feedback は 0 以上、0.95 以下"; }
	if (!(d.wet >= 0.0f && d.wet <= 1.0f)) { return "wet は 0 以上、1 以下"; }
	if (!(d.dampHz >= 0.0f && d.dampHz <= 20000.0f)) { return "dampHz は 0 以上、20000 以下 (0 で掛けない)"; }
	return "";
}

[[nodiscard]] inline const char* validateChorus(const ChorusSettings& c) noexcept
{
	if (!(c.rateHz >= 0.0f && c.rateHz <= 10.0f)) { return "rateHz は 0 以上、10 以下"; }
	if (!(c.depthMs >= 0.0f && c.depthMs <= 20.0f)) { return "depthMs は 0 以上、20 以下"; }
	if (!(c.delayMs >= 1.0f && c.delayMs <= kChorusMaxDelayMs)) { return "delayMs は 1 以上、50 以下"; }
	if (!(c.delayMs - c.depthMs >= 0.5f)) { return "delayMs - depthMs は 0.5 以上 (揺れが 0 を割らない)"; }
	if (!(c.mix >= 0.0f && c.mix <= 1.0f)) { return "mix は 0 以上、1 以下"; }
	return "";
}

/// @brief チャンネルごとの輪の遅延線。書いた位置から n サンプル前を読む
class DelayLine
{
public:
	void init(std::size_t maxFrames, std::uint32_t channels)
	{
		m_channels = channels;
		m_size = maxFrames + 1;
		m_buf.assign(m_size * channels, 0.0f);
		m_write = 0;
	}

	void push(const float* frame) noexcept
	{
		std::copy_n(frame, m_channels, m_buf.data() + m_write * m_channels);
		m_write = (m_write + 1) % m_size;
	}

	/// 直前に push した標本を 0 サンプル前とみなして、整数の遅れで読む
	[[nodiscard]] float read(std::uint32_t channel, std::size_t back) const noexcept
	{
		const std::size_t at = (m_write + m_size - 1 - std::min(back, m_size - 1)) % m_size;
		return m_buf[at * m_channels + channel];
	}

	/// 端数の遅れを線形補間で読む
	[[nodiscard]] float readFractional(std::uint32_t channel, double back) const noexcept
	{
		const double clamped = std::clamp(back, 0.0, static_cast<double>(m_size - 2));
		const auto whole = static_cast<std::size_t>(clamped);
		const auto frac = static_cast<float>(clamped - static_cast<double>(whole));
		const float a = read(channel, whole);
		const float b = read(channel, whole + 1);
		return a + (b - a) * frac;
	}

	[[nodiscard]] std::size_t capacity() const noexcept { return m_size - 1; }

private:
	std::vector<float> m_buf;
	std::size_t        m_size = 1;
	std::size_t        m_write = 0;
	std::uint32_t      m_channels = 2;
};

class Delay
{
public:
	static constexpr std::uint32_t kMaxChannels = 2;

	void init(float sampleRate, std::uint32_t channels)
	{
		m_rate = sampleRate;
		m_channels = std::min(channels, kMaxChannels);
		m_line.init(static_cast<std::size_t>(kDelayMaxSec * sampleRate) + 1, m_channels);
		for (float& z : m_damp) { z = 0.0f; }
	}

	void setSettings(const DelaySettings& s) noexcept
	{
		m_settings = s;
		m_delayFrames = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(s.timeSec * m_rate)));
		constexpr double kTwoPi = 2.0 * 3.14159265358979323846;
		m_dampCoeff = (s.dampHz > 0.0f) ? static_cast<float>(1.0 - std::exp(-kTwoPi * s.dampHz / m_rate)) : 1.0f;
	}

	/// @brief frames フレームを処理する。in と out は同じ配列でもよい (インターリーブ)
	void process(const float* in, float* out, std::uint32_t frames) noexcept
	{
		for (std::uint32_t i = 0; i < frames; ++i)
		{
			const float* x = in + static_cast<std::size_t>(i) * m_channels;
			float* y = out + static_cast<std::size_t>(i) * m_channels;
			float fed[kMaxChannels];
			for (std::uint32_t c = 0; c < m_channels; ++c)
			{
				// 遅延線には「入力 + 戻り」を書く。戻りは読んだ音に damp の low-pass を通した物
				const float echo = m_line.read(c, m_delayFrames - 1);
				m_damp[c] += (echo - m_damp[c]) * m_dampCoeff;
				fed[c] = x[c] + m_damp[c] * m_settings.feedback;
				y[c] = x[c] + echo * m_settings.wet;
			}
			m_line.push(fed);
		}
	}

	[[nodiscard]] std::size_t delayFrames() const noexcept { return m_delayFrames; }

private:
	DelaySettings m_settings;
	DelayLine     m_line;
	float         m_rate = 48000.0f;
	float         m_dampCoeff = 1.0f;
	float         m_damp[kMaxChannels]{};
	std::size_t   m_delayFrames = 1;
	std::uint32_t m_channels = 2;
};

class Chorus
{
public:
	static constexpr std::uint32_t kMaxChannels = 2;

	void init(float sampleRate, std::uint32_t channels)
	{
		m_rate = sampleRate;
		m_channels = std::min(channels, kMaxChannels);
		m_line.init(static_cast<std::size_t>(kChorusMaxDelayMs * 0.001f * sampleRate) + 2, m_channels);
		m_phase = 0.0;
	}

	void setSettings(const ChorusSettings& s) noexcept
	{
		m_settings = s;
		m_phaseStep = 2.0 * 3.14159265358979323846 * static_cast<double>(s.rateHz) / m_rate;
		m_centerFrames = static_cast<double>(s.delayMs) * 0.001 * m_rate;
		m_depthFrames = static_cast<double>(s.depthMs) * 0.001 * m_rate;
	}

	/// @brief frames フレームを処理する。in と out は同じ配列でもよい (インターリーブ)
	void process(const float* in, float* out, std::uint32_t frames) noexcept
	{
		constexpr double kQuarter = 3.14159265358979323846 * 0.5;
		const float mix = m_settings.mix;
		for (std::uint32_t i = 0; i < frames; ++i)
		{
			const float* x = in + static_cast<std::size_t>(i) * m_channels;
			float* y = out + static_cast<std::size_t>(i) * m_channels;
			m_line.push(x);
			for (std::uint32_t c = 0; c < m_channels; ++c)
			{
				const double back = m_centerFrames + m_depthFrames * std::sin(m_phase + (c == 1 ? kQuarter : 0.0));
				const float tap = m_line.readFractional(c, back);
				y[c] = x[c] * (1.0f - mix) + tap * mix;
			}
			m_phase += m_phaseStep;
			if (m_phase >= 2.0 * 3.14159265358979323846) { m_phase -= 2.0 * 3.14159265358979323846; }
		}
	}

private:
	ChorusSettings m_settings;
	DelayLine      m_line;
	float          m_rate = 48000.0f;
	double         m_phase = 0.0;
	double         m_phaseStep = 0.0;
	double         m_centerFrames = 0.0;
	double         m_depthFrames = 0.0;
	std::uint32_t  m_channels = 2;
};

}  // namespace mitiru::audio::mix
