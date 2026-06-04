#pragma once

/// @file AudioMeter.hpp
/// @brief オーディオメータリングの最小 POD
/// @details 再生中チャンネルの「種別 + 実効レベル」を 1 件で表す。host が
///          IAudioEngine から列挙して SharedSnapshot に併記し、mitiru_mixer 窓が
///          per-channel VU として描く (ADR 0014)。RMS 振幅ではなく各 voice の
///          設定実効音量を報告する (miniaudio が安価に出せるのは設定音量まで)。

namespace mitiru::audio
{

/// @brief 1 チャンネル分のメーター読み
struct ChannelMeter
{
	const char* kind  = "se";   ///< 種別ラベル (文字列リテラル: "music" / "se")
	float       level = 0.0f;    ///< 実効音量 [0.0, 1.0]
};

} // namespace mitiru::audio
