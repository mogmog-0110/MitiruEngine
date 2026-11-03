#pragma once

/// @file BusPitchShift.hpp
/// @brief バスに掛けるピッチシフトと、時間の伸縮 (ADR 0113)
/// @details 2 つの算法を持つ。signalsmith は Signalsmith Stretch (external/signalsmith-stretch、MIT) の位相ボコーダで、
///          倍音を保ったまま高さを変え、時間の伸縮もできる。grain は自前の 2 tap の遅延線で、ウィンドウの長さ (grainMs) を一定の速さで
///          読み進めて交差させる古い方式。遅れが小さく (ウィンドウの半分) 計算も軽いが、低い音や広い帯域ではうなりが出る。
///          どちらも同じ入力に同じ出力を返す (乱数の種を固定してある)。設定はチェーンに入れる前に決め、差した後は変えない。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <mitiru/audio/detail/SignalsmithInclude.hpp>
#include <mitiru/audio/mix/BusDelay.hpp>

namespace mitiru::audio::mix
{

enum class PitchShiftAlgorithm : std::uint8_t
{
	Signalsmith = 0,
	Grain,
};

struct PitchShiftSettings
{
	float               semitones = 0.0f;   ///< -24..24
	PitchShiftAlgorithm algorithm = PitchShiftAlgorithm::Signalsmith;
	float               blockMs = 40.0f;    ///< signalsmith の 1 ブロックの長さ。長いほど低い音が澄み、遅れが増える
	float               grainMs = 40.0f;    ///< grain のウィンドウの長さ
	float               tonalityLimitHz = 0.0f;   ///< signalsmith だけ。この上の帯域は高さを変えず音色を保つ。0 で使わない
};

[[nodiscard]] inline bool pitchShiftAlgorithmFromName(std::string_view name, PitchShiftAlgorithm& out) noexcept
{
	if (name == "signalsmith") { out = PitchShiftAlgorithm::Signalsmith; return true; }
	if (name == "grain") { out = PitchShiftAlgorithm::Grain; return true; }
	return false;
}

[[nodiscard]] inline const char* validatePitchShift(const PitchShiftSettings& p) noexcept
{
	if (!(p.semitones >= -24.0f && p.semitones <= 24.0f)) { return "semitones は -24 以上、24 以下"; }
	if (!(p.blockMs >= 10.0f && p.blockMs <= 200.0f)) { return "blockMs は 10 以上、200 以下"; }
	if (!(p.grainMs >= 10.0f && p.grainMs <= 200.0f)) { return "grainMs は 10 以上、200 以下"; }
	if (!(p.tonalityLimitHz >= 0.0f && p.tonalityLimitHz <= 20000.0f)) { return "tonalityLimitHz は 0 以上、20000 以下"; }
	return "";
}

namespace detail
{

/// インターリーブの配列を、Signalsmith が求める buffer[channel][index] の形で見せる
struct StridedIn
{
	struct Chan
	{
		const float*  base;
		std::uint32_t stride;
		float operator[](int i) const noexcept { return base[static_cast<std::size_t>(i) * stride]; }
	};
	const float*  p;
	std::uint32_t channels;
	Chan operator[](int c) const noexcept { return {p + c, channels}; }
};

struct StridedOut
{
	struct Chan
	{
		float*        base;
		std::uint32_t stride;
		float& operator[](int i) const noexcept { return base[static_cast<std::size_t>(i) * stride]; }
	};
	float*        p;
	std::uint32_t channels;
	Chan operator[](int c) const noexcept { return {p + c, channels}; }
};

}  // namespace detail

/// @brief Signalsmith Stretch の包み。高さの倍率と、入力と出力の長さの比 (時間の伸縮) を別々に持つ
class SignalsmithStretcher
{
public:
	static constexpr long kSeed = 0x5A17;   // 同じ入力に同じ出力を返すために固定する (既定は random_device)

	void init(float sampleRate, std::uint32_t channels, float blockMs)
	{
		m_channels = std::min<std::uint32_t>(channels, 2);
		const int block = std::max(64, static_cast<int>(sampleRate * blockMs * 0.001f));
		m_stretch.configure(static_cast<int>(m_channels), block, block / 4, false);
		m_scratch.assign(static_cast<std::size_t>(block) * 4 * m_channels, 0.0f);
	}

	void setSemitones(float semitones, float tonalityLimitHz, float sampleRate) noexcept
	{
		m_stretch.setTransposeSemitones(semitones, (tonalityLimitHz > 0.0f) ? tonalityLimitHz / sampleRate : 0.0f);
	}

	/// @brief 高さだけ変える (入力と出力は同じ長さ)。in と out は別の配列
	void process(const float* in, float* out, std::uint32_t frames) { stretch(in, frames, out, frames); }

	/// @brief 入力 inFrames から出力 outFrames を作る (時間の伸縮)。in と out は別の配列
	void stretch(const float* in, std::uint32_t inFrames, float* out, std::uint32_t outFrames)
	{
		m_stretch.process(detail::StridedIn{in, m_channels}, static_cast<int>(inFrames),
		                  detail::StridedOut{out, m_channels}, static_cast<int>(outFrames));
	}

	void reset() { m_stretch.reset(); }

	/// @brief 入力を出してから、その音が出力に現れるまでのフレーム数
	[[nodiscard]] int latencyFrames() const noexcept { return m_stretch.inputLatency() + m_stretch.outputLatency(); }

private:
	signalsmith::stretch::SignalsmithStretch<float> m_stretch{kSeed};
	std::vector<float>                              m_scratch;
	std::uint32_t                                   m_channels = 2;
};

/// @brief 自前の 2 tap の遅延線のピッチシフト。読み位置を (1 - 倍率) の速さで回し、半周期ずらした 2 本を正弦と余弦で交差させる
class GrainPitchShifter
{
public:
	void init(float sampleRate, std::uint32_t channels, float grainMs)
	{
		m_channels = std::min<std::uint32_t>(channels, 2);
		m_window = std::max<std::size_t>(64, static_cast<std::size_t>(sampleRate * grainMs * 0.001f));
		m_line.init(m_window + 2, m_channels);
		m_pos = 0.0;
	}

	void setSemitones(float semitones) noexcept { m_step = 1.0 - std::pow(2.0, static_cast<double>(semitones) / 12.0); }

	/// @brief frames フレームを処理する。in と out は同じ配列でもよい (インターリーブ)
	void process(const float* in, float* out, std::uint32_t frames) noexcept
	{
		constexpr double kPi = 3.14159265358979323846;
		const auto window = static_cast<double>(m_window);
		for (std::uint32_t i = 0; i < frames; ++i)
		{
			const float* x = in + static_cast<std::size_t>(i) * m_channels;
			float* y = out + static_cast<std::size_t>(i) * m_channels;
			m_line.push(x);
			const double d0 = m_pos;
			double d1 = m_pos + window * 0.5;
			if (d1 >= window) { d1 -= window; }
			const auto g0 = static_cast<float>(std::sin(kPi * d0 / window));
			const auto g1 = static_cast<float>(std::cos(kPi * d0 / window));
			for (std::uint32_t c = 0; c < m_channels; ++c)
			{
				y[c] = m_line.readFractional(c, d0) * g0 + m_line.readFractional(c, d1) * std::fabs(g1);
			}
			m_pos += m_step;
			if (m_pos >= window) { m_pos -= window; }
			if (m_pos < 0.0) { m_pos += window; }
		}
	}

	[[nodiscard]] int latencyFrames() const noexcept { return static_cast<int>(m_window / 2); }

private:
	DelayLine     m_line;
	std::size_t   m_window = 1920;
	double        m_pos = 0.0;
	double        m_step = 0.0;
	std::uint32_t m_channels = 2;
};

/// @brief mix.json の pitchShift 1 つ分。算法で中身を選ぶ
class PitchShifter
{
public:
	void init(float sampleRate, std::uint32_t channels, const PitchShiftSettings& s)
	{
		m_settings = s;
		m_rate = sampleRate;
		m_channels = std::min<std::uint32_t>(channels, 2);
		if (s.algorithm == PitchShiftAlgorithm::Grain)
		{
			m_grain.init(sampleRate, channels, s.grainMs);
			m_grain.setSemitones(s.semitones);
		}
		else
		{
			m_smith.init(sampleRate, channels, s.blockMs);
			m_smith.setSemitones(s.semitones, s.tonalityLimitHz, sampleRate);
			m_tmp.assign(4096 * m_channels, 0.0f);
		}
	}

	/// @brief frames フレームを処理する。in と out は同じ配列でもよい (インターリーブ)
	void process(const float* in, float* out, std::uint32_t frames)
	{
		if (m_settings.algorithm == PitchShiftAlgorithm::Grain) { m_grain.process(in, out, frames); return; }
		for (std::uint32_t done = 0; done < frames;)
		{
			const auto n = std::min<std::uint32_t>(frames - done, static_cast<std::uint32_t>(m_tmp.size() / m_channels));
			const float* src = in + static_cast<std::size_t>(done) * m_channels;
			float* dst = out + static_cast<std::size_t>(done) * m_channels;
			std::copy_n(src, static_cast<std::size_t>(n) * m_channels, m_tmp.data());   // in と out が同じ配列でも壊れないように写す
			m_smith.process(m_tmp.data(), dst, n);
			done += n;
		}
	}

	[[nodiscard]] int latencyFrames() const noexcept
	{
		return (m_settings.algorithm == PitchShiftAlgorithm::Grain) ? m_grain.latencyFrames() : m_smith.latencyFrames();
	}

private:
	PitchShiftSettings   m_settings;
	SignalsmithStretcher m_smith;
	GrainPitchShifter    m_grain;
	std::vector<float>   m_tmp;
	float                m_rate = 48000.0f;
	std::uint32_t        m_channels = 2;
};

/// @brief 高さを保ったまま時間を伸ばす (スロー再生)。rate は 1 で等速、0.5 で半分の速さ、0 で止まる
class TimeStretcher
{
public:
	void init(float sampleRate, std::uint32_t channels, float blockMs)
	{
		m_channels = std::min<std::uint32_t>(channels, 2);
		m_smith.init(sampleRate, channels, blockMs);
		m_carry = 0.0;
	}

	/// @brief 出力 outFrames を作るのに入力をいくつ使うか (rate から決め、端数は持ち越す)。inAvail を超えない
	[[nodiscard]] std::uint32_t inputFramesFor(std::uint32_t outFrames, std::uint32_t inAvail, float rate) noexcept
	{
		const double want = static_cast<double>(outFrames) * std::clamp(static_cast<double>(rate), 0.0, 4.0) + m_carry;
		const auto whole = static_cast<std::uint32_t>(std::min(want, static_cast<double>(inAvail)));
		// 足りなかった分は持ち越さない (次に追い付こうとして一気に進まない)。持ち越すのは端数だけ
		m_carry = (static_cast<double>(whole) < std::floor(want)) ? 0.0 : want - static_cast<double>(whole);
		return whole;
	}

	/// @brief 入力 inFrames から出力 outFrames を作る。in と out は別の配列
	void process(const float* in, std::uint32_t inFrames, float* out, std::uint32_t outFrames)
	{
		m_smith.stretch(in, inFrames, out, outFrames);
	}

	[[nodiscard]] int latencyFrames() const noexcept { return m_smith.latencyFrames(); }

private:
	SignalsmithStretcher m_smith;
	double               m_carry = 0.0;
	std::uint32_t        m_channels = 2;
};

}  // namespace mitiru::audio::mix
