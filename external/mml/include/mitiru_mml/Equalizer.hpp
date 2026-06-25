#pragma once
/// @file Equalizer.hpp
/// @brief 3バンド EQ（ローシェルフ + ミッドピーク + ハイシェルフ）。ミックスの帯域バランスを整える。
/// @details Biquad を直列に通す。低域を持ち上げて温かく、耳に痛い中高域を削る、といった
///          マスター段の補正に使う。16bit モノラル PcmBuffer を入力し、適用済みを返す。
///
/// @code
/// mitiru_mml::Equalizer eq;
/// eq.setLowGain(3.0f).setMidGain(-2.0f).setHighGain(1.5f);
/// auto shaped = eq.process(pcm, 44100);
/// @endcode

#include <mitiru_mml/Biquad.hpp>
#include <mitiru_mml/MmlTypes.hpp>

#include <algorithm>
#include <cstdint>

namespace mitiru_mml
{

/// @brief 3バンドイコライザ（モノラル）
class Equalizer
{
public:
	Equalizer& setLowGain(float db)   { m_lowDb = db; return *this; }        ///< ローシェルフ(dB)
	Equalizer& setMidGain(float db)   { m_midDb = db; return *this; }        ///< ミッドピーク(dB)
	Equalizer& setHighGain(float db)  { m_highDb = db; return *this; }       ///< ハイシェルフ(dB)
	Equalizer& setLowFreq(float hz)   { m_lowHz = hz; return *this; }
	Equalizer& setMidFreq(float hz)   { m_midHz = hz; return *this; }
	Equalizer& setHighFreq(float hz)  { m_highHz = hz; return *this; }
	Equalizer& setMidQ(float q)       { m_midQ = (q > 0.0f) ? q : 0.7f; return *this; }

	/// @brief PCM に 3 バンド EQ を適用して返す（入力は変更しない）。
	[[nodiscard]] PcmBuffer process(const PcmBuffer& in, std::uint32_t sampleRate) const
	{
		if (in.empty()) { return in; }
		const float sr = static_cast<float>(sampleRate);

		Biquad low;
		Biquad mid;
		Biquad high;
		low.set(BiquadType::LowShelf,  sr, m_lowHz,  0.707f, m_lowDb);
		mid.set(BiquadType::Peaking,   sr, m_midHz,  m_midQ, m_midDb);
		high.set(BiquadType::HighShelf, sr, m_highHz, 0.707f, m_highDb);

		PcmBuffer out(in.size());
		for (std::size_t n = 0; n < in.size(); ++n)
		{
			float s = static_cast<float>(in[n]) / 32768.0f;
			s = low.process(s);
			s = mid.process(s);
			s = high.process(s);
			out[n] = static_cast<std::int16_t>(std::clamp(s, -1.0f, 1.0f) * 32767.0f);
		}
		return out;
	}

private:
	float m_lowDb  = 0.0f, m_midDb = 0.0f, m_highDb = 0.0f;
	float m_lowHz  = 200.0f;   ///< ローシェルフのコーナー
	float m_midHz  = 1000.0f;  ///< ミッドピークの中心
	float m_highHz = 4000.0f;  ///< ハイシェルフのコーナー
	float m_midQ   = 0.9f;
};

} // namespace mitiru_mml
