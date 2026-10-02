#pragma once

/// @file MusicLowPassParams.hpp
/// @brief BGM の low-pass の係数を、miniaudio (MusicLowPassBus) と Web Audio (WebAudioEngine) で揃える
/// @details 両方とも 2 次の Butterworth (Q = 1/√2)。Web Audio の BiquadFilterNode は lowpass の Q を
///          線形ではなく dB で受けるので、同じ数字を渡すとピークが生じる。ここで両方の単位に対応する値を求める。

#include <algorithm>
#include <cmath>

namespace mitiru::audio
{

struct MusicLowPassParams
{
	bool bypass = true;          ///< true なら BGM をフィルタに通さず直結する
	float frequencyHz = 0.0f;    ///< 10Hz 以上、ナイキストの手前まで
	float qLinear = 0.0f;        ///< miniaudio の ma_lpf2 が使う Q
	float qWebAudioDb = 0.0f;    ///< BiquadFilterNode (lowpass) の Q。20 log10(qLinear)
};

/// @param cutoffHz 0 以下なら外す
[[nodiscard]] inline MusicLowPassParams musicLowPassParams(float cutoffHz, float sampleRate) noexcept
{
	MusicLowPassParams p;
	if (cutoffHz <= 0.0f || sampleRate <= 0.0f) { return p; }
	constexpr float kButterworthQ = 0.70710678f;
	// ナイキストちょうどでは双一次変換の係数が退化するので、手前に制限する
	p.bypass = false;
	p.frequencyHz = std::clamp(cutoffHz, 10.0f, sampleRate * 0.49f);
	p.qLinear = kButterworthQ;
	p.qWebAudioDb = 20.0f * std::log10(kButterworthQ);
	return p;
}

} // namespace mitiru::audio
