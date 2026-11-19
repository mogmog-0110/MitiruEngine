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

	/// @brief 1 サンプルずつ処理するコーラスの状態。process() と ModernSong の送りバスで同じ式を使う。
	/// @details 入力は int16 を 32768 で割った float。戻り値は mix を掛けた wet 成分だけで、dry は含まない。
	class Stream
	{
	public:
		Stream(const Chorus& settings, std::uint32_t sampleRate)
			: m_sr(static_cast<float>(sampleRate))
			, m_rateHz(settings.m_rateHz)
			, m_mix(settings.m_mix)
		{
			// 遅延ラインは中心、揺れ幅、補間用の 1 サンプルを収める。
			const float maxDelaySamp = (settings.m_baseMs + settings.m_depthMs) * 0.001f * m_sr;
			m_line.assign(static_cast<std::size_t>(maxDelaySamp) + 4, 0.0f);
			m_baseSamp  = settings.m_baseMs * 0.001f * m_sr;
			m_depthSamp = settings.m_depthMs * 0.001f * m_sr;
		}

		[[nodiscard]] float wet(float x)
		{
			const std::size_t lineLen = m_line.size();
			m_line[m_w] = x;

			// LFO で可変の読み出し遅延（サンプル）を決める。
			const float lfo   = 0.5f + 0.5f * std::sin(kTwoPi * m_rateHz * static_cast<float>(m_n) / m_sr);
			const float delay = m_baseSamp + m_depthSamp * lfo;

			// m_w から delay サンプル前を線形補間で読む。
			const float readPos = static_cast<float>(m_w) - delay;
			float rp = readPos;
			while (rp < 0.0f) { rp += static_cast<float>(lineLen); }
			const std::size_t i0 = static_cast<std::size_t>(rp) % lineLen;
			const std::size_t i1 = (i0 + 1) % lineLen;
			const float frac = rp - std::floor(rp);
			const float wetSample = m_line[i0] * (1.0f - frac) + m_line[i1] * frac;

			if (++m_w >= lineLen) { m_w = 0; }
			++m_n;
			return wetSample * m_mix;
		}

	private:
		static constexpr float kTwoPi = 6.28318530717958647692f;

		std::vector<float> m_line;
		std::size_t m_w = 0;
		std::size_t m_n = 0;
		float m_sr;
		float m_rateHz;
		float m_mix;
		float m_baseSamp = 0.0f;
		float m_depthSamp = 0.0f;
	};

	/// @brief dry PCM を入力し、コーラス適用済み PCM を返す（入力は変更しない）。
	[[nodiscard]] PcmBuffer process(const PcmBuffer& in, std::uint32_t sampleRate) const
	{
		if (in.empty()) { return in; }
		Stream stream(*this, sampleRate);

		PcmBuffer out(in.size());
		for (std::size_t n = 0; n < in.size(); ++n)
		{
			const float x = static_cast<float>(in[n]) / 32768.0f;
			const float y = x * (1.0f - m_mix) + stream.wet(x);
			out[n] = static_cast<std::int16_t>(std::clamp(y, -1.0f, 1.0f) * 32767.0f);
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
