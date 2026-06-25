#pragma once
/// @file Chorus.hpp
/// @brief コーラス（LFO で揺らした短いディレイ）。単音を厚く、合奏のように広げる。
/// @details 16bit モノラル PcmBuffer を入力し、コーラス適用済みを返す（入力は変更しない）。
///          可変ディレイ点を線形補間で読み、原音と混ぜる。チップチューンの硬い単音を
///          太く温かくするのに効く。
///
/// @code
/// mitiru_mml::Chorus c;
/// c.setRate(1.2f).setDepthMs(6.0f).setMix(0.45f);
/// auto wet = c.process(dryPcm, 44100);
/// @endcode

#include <mitiru_mml/MmlTypes.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mitiru_mml
{

/// @brief コーラス（モノラル可変ディレイ）
class Chorus
{
public:
	Chorus& setRate(float hz)     { m_rateHz = (hz > 0.0f) ? hz : 0.0f; return *this; }   ///< LFO 速度(Hz)
	Chorus& setDepthMs(float ms)  { m_depthMs = (ms > 0.0f) ? ms : 0.0f; return *this; }  ///< 揺れ幅(ms)
	Chorus& setBaseMs(float ms)   { m_baseMs = (ms > 0.0f) ? ms : 0.0f; return *this; }   ///< 中心ディレイ(ms)
	Chorus& setMix(float v)       { m_mix = std::clamp(v, 0.0f, 1.0f); return *this; }    ///< wet 比率

	/// @brief dry PCM を入力し、コーラス適用済み PCM を返す（入力は変更しない）。
	[[nodiscard]] PcmBuffer process(const PcmBuffer& in, std::uint32_t sampleRate) const
	{
		if (in.empty()) { return in; }
		const float sr = static_cast<float>(sampleRate);

		// 遅延ラインは「中心 + 揺れ幅 + 補間 1 サンプル」分を確保する。
		const float maxDelaySamp = (m_baseMs + m_depthMs) * 0.001f * sr;
		const std::size_t lineLen = static_cast<std::size_t>(maxDelaySamp) + 4;
		std::vector<float> line(lineLen, 0.0f);
		std::size_t w = 0;

		const float baseSamp  = m_baseMs * 0.001f * sr;
		const float depthSamp = m_depthMs * 0.001f * sr;
		constexpr float kTwoPi = 6.28318530717958647692f;

		PcmBuffer out(in.size());
		for (std::size_t n = 0; n < in.size(); ++n)
		{
			const float x = static_cast<float>(in[n]) / 32768.0f;
			line[w] = x;

			// LFO で可変の読み出し遅延（サンプル）を決める。
			const float lfo   = 0.5f + 0.5f * std::sin(kTwoPi * m_rateHz * static_cast<float>(n) / sr);
			const float delay = baseSamp + depthSamp * lfo;

			// w から delay サンプルだけ過去を線形補間で読む。
			const float readPos = static_cast<float>(w) - delay;
			float rp = readPos;
			while (rp < 0.0f) { rp += static_cast<float>(lineLen); }
			const std::size_t i0 = static_cast<std::size_t>(rp) % lineLen;
			const std::size_t i1 = (i0 + 1) % lineLen;
			const float frac = rp - std::floor(rp);
			const float wet  = line[i0] * (1.0f - frac) + line[i1] * frac;

			const float y = x * (1.0f - m_mix) + wet * m_mix;
			out[n] = static_cast<std::int16_t>(std::clamp(y, -1.0f, 1.0f) * 32767.0f);

			if (++w >= lineLen) { w = 0; }
		}
		return out;
	}

private:
	float m_rateHz  = 1.2f;
	float m_depthMs = 5.0f;
	float m_baseMs  = 18.0f;
	float m_mix     = 0.4f;
};

} // namespace mitiru_mml
