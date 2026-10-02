#pragma once

/// @file AudioMeter.hpp
/// @brief オーディオメータリング用の最小 POD
/// @details 再生中のチャンネルの「種別 + 実効レベル」を 1 件で表す。host が
///          IAudioEngine から列挙して SharedSnapshot に併記し、mitiru_mixer 窓が
///          per-channel VU として描画する。RMS 振幅ではなく、各 voice の
///          設定上の実効音量を報告する (miniaudio が低コストで取得できるのは設定音量まで)。
///          id / asset / pan / remainingSec (K1) は mixer 窓の「voice 一覧」用。
///          固定長配列なので、列挙のたびに heap を使わない (列挙は ~10Hz の tool 書き出し時のみ)。

#include <cstddef>
#include <cstring>

namespace mitiru::audio
{

/// @brief 1 チャンネル分のメーターの読み取り値
struct ChannelMeter
{
	const char* kind  = "se";   ///< 種別ラベル (文字列リテラル: "music" / "voice" / "se")
	float       level = 0.0f;   ///< 実効音量 [0.0, 1.0]
	float       pan   = 0.0f;   ///< 定位 [-1.0 (左), +1.0 (右)]。backend が持たなければ 0
	float       remainingSec = -1.0f;  ///< 終端までの残り秒。不明 (loop / stream / 非対応) は負
	char        id[32]    = {};  ///< 論理 sound id (game が hud.voice に渡した名前)。不明なら空
	char        asset[64] = {};  ///< 実ファイル名 (path の末尾要素)。不明なら空

	/// @brief 固定長欄へ null 終端で書く (長ければ末尾を切り詰める)。
	static void copyTo(char* dst, std::size_t cap, const char* src) noexcept
	{
		if (dst == nullptr || cap == 0) { return; }
		if (src == nullptr) { dst[0] = '\0'; return; }
		std::size_t n = 0;
		while (n + 1 < cap && src[n] != '\0') { dst[n] = src[n]; ++n; }
		dst[n] = '\0';
	}
};

} // namespace mitiru::audio
