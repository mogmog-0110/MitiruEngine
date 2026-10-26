#pragma once

/// @file BusDynamics.hpp
/// @brief バスのコンプレッサと、master の天井 (limiter)。どちらも mix.json で書いたときだけ掛かる
/// @details コンプレッサはステレオの大きい方の絶対値 (ピーク) を測る。測った値は、新しいピークで一気に上がり、release の時定数 (63%) で下がる。
///          計算した下げ幅は、attack の時定数 (63%) で追い付き、下がるときは測った値に付いていく。
///          サイドチェーンは持たない (他のバスの音で下げるのは mix.json の ducking = BusDucker)。
///          limiter は先読みせず、天井を超えるサンプルで即座に音量を下げ、release の時定数で戻す。先読みしないので音は遅れず、出力は天井を超えない。
///          掛かった音は歪むので、master に安全のためだけ掛ける。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace mitiru::audio::mix
{

struct CompressorSettings
{
	float thresholdDb = -12.0f;
	float ratio = 4.0f;          ///< 1 以上。1 は掛けない
	float attackSec = 0.01f;
	float releaseSec = 0.1f;
	float kneeDb = 0.0f;         ///< 0 はハードニー
	float makeupDb = 0.0f;
};

struct LimiterSettings
{
	float ceilingDb = 0.0f;
	float releaseSec = 0.1f;
};

/// @return 範囲を外れていれば理由。空なら使える
[[nodiscard]] inline const char* validateCompressor(const CompressorSettings& c) noexcept
{
	if (!(c.thresholdDb <= 0.0f && c.thresholdDb >= -80.0f)) { return "thresholdDb は -80 以上、0 以下"; }
	if (!(c.ratio >= 1.0f && c.ratio <= 100.0f)) { return "ratio は 1 以上、100 以下"; }
	if (!(c.attackSec >= 0.0f && c.attackSec <= 5.0f && c.releaseSec >= 0.0f && c.releaseSec <= 5.0f))
	{
		return "attackSec と releaseSec は 0 以上、5 以下";
	}
	if (!(c.kneeDb >= 0.0f && c.kneeDb <= 40.0f)) { return "kneeDb は 0 以上、40 以下"; }
	if (!(c.makeupDb >= 0.0f && c.makeupDb <= 24.0f)) { return "makeupDb は 0 以上、24 以下"; }
	return "";
}

[[nodiscard]] inline const char* validateLimiter(const LimiterSettings& l) noexcept
{
	if (!(l.ceilingDb <= 0.0f && l.ceilingDb >= -24.0f)) { return "ceilingDb は -24 以上、0 以下"; }
	if (!(l.releaseSec > 0.0f && l.releaseSec <= 5.0f)) { return "releaseSec は 0 より大きく、5 以下"; }
	return "";
}

/// @brief しきい値と比と幅から、入力のレベル (dB) に対する下げ幅 (dB、0 以上) を返す
[[nodiscard]] inline float compressorReductionDb(const CompressorSettings& c, float levelDb) noexcept
{
	const float over = levelDb - c.thresholdDb;
	const float slope = 1.0f - 1.0f / std::max(c.ratio, 1.0f);
	if (2.0f * over < -c.kneeDb) { return 0.0f; }
	if (c.kneeDb > 0.0f && 2.0f * std::fabs(over) <= c.kneeDb)
	{
		const float x = over + c.kneeDb * 0.5f;
		return slope * x * x / (2.0f * c.kneeDb);
	}
	return slope * over;
}

class Compressor
{
public:
	void init(float sampleRate, std::uint32_t channels) noexcept
	{
		m_rate = static_cast<double>(sampleRate);
		m_channels = channels;
		reset();
	}

	void setSettings(const CompressorSettings& s) noexcept
	{
		m_settings = s;
		m_attack = coefficient(s.attackSec);
		m_releaseDecay = (s.releaseSec > 0.0f) ? std::exp(-1.0 / (static_cast<double>(s.releaseSec) * m_rate)) : 0.0;
		m_makeup = static_cast<float>(std::pow(10.0, static_cast<double>(s.makeupDb) / 20.0));
	}

	void reset() noexcept { m_peak = 0.0f; m_reductionDb = 0.0f; }

	/// @brief frames フレームを処理する。in と out は同じ配列でもよい (インターリーブ)
	void process(const float* in, float* out, std::uint32_t frames) noexcept
	{
		const float attack = static_cast<float>(m_attack);
		const float decay = static_cast<float>(m_releaseDecay);
		for (std::uint32_t i = 0; i < frames; ++i)
		{
			const float* x = in + static_cast<std::size_t>(i) * m_channels;
			float level = 0.0f;
			for (std::uint32_t c = 0; c < m_channels; ++c) { level = std::max(level, std::fabs(x[c])); }
			m_peak = std::max(level, m_peak * decay);
			const float levelDb = (m_peak > 1e-9f) ? 20.0f * std::log10(m_peak) : -180.0f;
			const float goal = compressorReductionDb(m_settings, levelDb);
			m_reductionDb = (goal > m_reductionDb) ? m_reductionDb + (goal - m_reductionDb) * attack : goal;
			const float gain = m_makeup * std::pow(10.0f, -m_reductionDb / 20.0f);
			for (std::uint32_t c = 0; c < m_channels; ++c) { out[static_cast<std::size_t>(i) * m_channels + c] = x[c] * gain; }
		}
	}

	[[nodiscard]] float reductionDb() const noexcept { return m_reductionDb; }

private:
	/// 1 標本あたり目標との差を詰める割合。0 秒なら一気に詰める
	[[nodiscard]] double coefficient(float sec) const noexcept
	{
		return (sec > 0.0f) ? 1.0 - std::exp(-1.0 / (static_cast<double>(sec) * m_rate)) : 1.0;
	}

	CompressorSettings m_settings;
	double m_rate = 48000.0;
	double m_attack = 1.0;
	double m_releaseDecay = 0.0;
	float m_makeup = 1.0f;
	std::uint32_t m_channels = 2;
	float m_peak = 0.0f;
	float m_reductionDb = 0.0f;
};

class Limiter
{
public:
	void init(float sampleRate, std::uint32_t channels) noexcept
	{
		m_rate = static_cast<double>(sampleRate);
		m_channels = channels;
		m_gain = 1.0f;
	}

	void setSettings(const LimiterSettings& s) noexcept
	{
		m_ceiling = static_cast<float>(std::pow(10.0, static_cast<double>(s.ceilingDb) / 20.0));
		m_recover = static_cast<float>(1.0 - std::exp(-1.0 / (static_cast<double>(std::max(s.releaseSec, 1e-4f)) * m_rate)));
	}

	/// @brief frames フレームをその場で処理する (インターリーブ)
	void process(float* buf, std::uint32_t frames) noexcept
	{
		for (std::uint32_t i = 0; i < frames; ++i)
		{
			float* x = buf + static_cast<std::size_t>(i) * m_channels;
			float peak = 0.0f;
			for (std::uint32_t c = 0; c < m_channels; ++c) { peak = std::max(peak, std::fabs(x[c])); }
			const float need = (peak > m_ceiling) ? m_ceiling / peak : 1.0f;
			m_gain = std::min(need, m_gain + (1.0f - m_gain) * m_recover);
			for (std::uint32_t c = 0; c < m_channels; ++c) { x[c] = std::clamp(x[c] * m_gain, -m_ceiling, m_ceiling); }
		}
	}

	[[nodiscard]] float gain() const noexcept { return m_gain; }

private:
	double m_rate = 48000.0;
	std::uint32_t m_channels = 2;
	float m_ceiling = 1.0f;
	float m_recover = 0.0f;
	float m_gain = 1.0f;
};

}  // namespace mitiru::audio::mix
