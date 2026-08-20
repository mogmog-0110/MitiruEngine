#pragma once
/// @file LoFiQuantize.hpp
/// @brief パレット量子化 + 4×4 Bayer オーダードディザの参照実装（CPU）。
/// @details DirectX5 / 256色・16bit DirectDraw 期の "網点まじりの粗い画質" をエミュする
///          ローファイ・ポストFX の中核アルゴリズム。GPU 版 (LoFiShader.hpp の HLSL)
///          と**同一の式**を CPU でも提供し、決定的にユニットテストできるようにする。
///          原理は PICO-8 / demake 系と同じ: 各チャンネルを少ビット (例 R5 G6 B5) へ量子化し、
///          量子化誤差を 4×4 Bayer 行列で空間的に拡散して中間色を網点に変える。
///
/// @code
/// auto [r,g,b] = mitiru::render::lofi::quantizeDither(128,128,128, x,y, 5,6,5, 1.0f);
/// @endcode

#include <array>
#include <cstdint>

namespace mitiru::render::lofi
{

/// @brief 4×4 Bayer (ordered dithering) 行列。値域 0..15。
inline constexpr std::array<std::array<int, 4>, 4> kBayer4x4 = {{
	{ 0,  8,  2, 10},
	{12,  4, 14,  6},
	{ 3, 11,  1,  9},
	{15,  7, 13,  5},
}};

/// @brief Bayer しきい値を [-0.5, +0.5) に正規化して返す。
[[nodiscard]] inline float bayerNorm(int x, int y) noexcept
{
	const int b = kBayer4x4[static_cast<std::size_t>(y & 3)][static_cast<std::size_t>(x & 3)];
	return (static_cast<float>(b) + 0.5f) / 16.0f - 0.5f;
}

namespace detail
{
[[nodiscard]] inline float clamp01(float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

/// @brief 1 チャンネルを bits ビットへ量子化（Bayer ディザ込み）。入出力 [0,1]。
[[nodiscard]] inline float quantizeChannel(float v, int bits, float bn, float strength) noexcept
{
	if (bits <= 0 || bits >= 8) return v;             // 0/8bit は素通し（量子化なし）
	const int levels = (1 << bits) - 1;               // 例 5bit → 31 段
	const float step = 1.0f / static_cast<float>(levels);
	const float dithered = clamp01(v + bn * step * strength);
	// round(dithered * levels) / levels
	const int q = static_cast<int>(dithered * static_cast<float>(levels) + 0.5f);
	return static_cast<float>(q) * step;
}
} // namespace detail

/// @brief 1 ピクセル (r,g,b 各 0..255) を量子化＋ディザして 0..255 で返す。
/// @param x,y ピクセル座標（Bayer 行列の位相に使う）
/// @param bitsR,bitsG,bitsB チャンネル毎の量子化ビット数（既定 5/6/5 = RGB565、0 or 8 で無効）
/// @param strength ディザ強度（0=ディザ無し、1=標準＝量子化 1 段ぶん）
[[nodiscard]] inline std::array<std::uint8_t, 3> quantizeDither(
	std::uint8_t r, std::uint8_t g, std::uint8_t b,
	int x, int y, int bitsR = 5, int bitsG = 6, int bitsB = 5, float strength = 1.0f) noexcept
{
	const float bn = bayerNorm(x, y);
	const float qr = detail::quantizeChannel(static_cast<float>(r) / 255.0f, bitsR, bn, strength);
	const float qg = detail::quantizeChannel(static_cast<float>(g) / 255.0f, bitsG, bn, strength);
	const float qb = detail::quantizeChannel(static_cast<float>(b) / 255.0f, bitsB, bn, strength);
	return {
		static_cast<std::uint8_t>(detail::clamp01(qr) * 255.0f + 0.5f),
		static_cast<std::uint8_t>(detail::clamp01(qg) * 255.0f + 0.5f),
		static_cast<std::uint8_t>(detail::clamp01(qb) * 255.0f + 0.5f),
	};
}

// ── 映像出力段 (VI) 相当のフィルタ ────────────────────────────────────
// 低ビットのフレームバッファへディザして書いた絵から、走査出力の途中で網点を解き、
// 縁の跳ねを潰す。結果は網点の見える絵ではなく、にじんで柔らかい絵になる。

/// @brief de-dither: 上位 5bit で 8 近傍と比べ、差を ±1 に丸めて足し戻す。
/// @param center  中心画素の 1 チャンネル (0..255)
/// @param around  8 近傍の同チャンネル (0..255)。順序は結果に影響しない
/// @return 0..255。中心の下位 3bit は捨て、近傍との関係で埋め直す
[[nodiscard]] inline int deDitherChannel(int center,
                                         const std::array<int, 8>& around) noexcept
{
	const int c5 = center >> 3;
	int accum = 0;
	for (const int n : around)
	{
		const int d = (n >> 3) - c5;
		accum += (d < -1) ? -1 : ((d > 1) ? 1 : d);
	}
	const int v = (center & 0xF8) + accum;
	return (v < 0) ? 0 : ((v > 255) ? 255 : v);
}

/// @brief divot: 横 3 タップの中央値。中心が外れ値のときだけ値が動く。
[[nodiscard]] inline int median3(int a, int b, int c) noexcept
{
	const int lo = (a < c) ? a : c;
	const int hi = (a < c) ? c : a;
	return (b < lo) ? lo : ((b > hi) ? hi : b);
}

} // namespace mitiru::render::lofi
