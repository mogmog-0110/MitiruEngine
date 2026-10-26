#pragma once

/// @file ColorFinishMath.hpp
/// @brief 色の仕上げ (LUT・ビネット・フィルムグレイン) の式。DX12 の HLSL と Dawn の WGSL が同じ式を引き、
///        試験は GPU の読み戻しをこの CPU 実装と比べる
/// @details 入力は AA とアップスケールを済ませた sRGB 符号化済みの LDR (0..1)。順は LUT、ビネット、グレイン。
///          LUT は 8 隅を整数座標で読んで自前でトライリニアに補間する。ハードウェアの補間は精度が GPU で違い、
///          2D に展開した画像では隣の段へにじむので使わない。グレインの乱数は整数のハッシュで、シードは決定論のフレーム番号。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mitiru::render
{

inline constexpr int kMinLutSize = 2;
inline constexpr int kMaxLutSize = 64;

/// 色の仕上げ 1 組ぶんの GPU 定数 (48 byte)。DX12 の CbColorFinish と Dawn の uniform が同じ並び
struct ColorFinishCb
{
	float lutSize = 0.0f;            ///< LUT の一辺 N (展開した画像は幅 N*N、高さ N)。0 = LUT なし
	float lutStrength = 1.0f;        ///< 0..1。元の色と LUT を通した色の混ぜ具合
	float vignetteStrength = 0.0f;   ///< 0..1。0 = なし
	float vignetteRadius = 0.5f;     ///< 0..1。中心から隅までを 1 として、ここから暗くし始める
	float grainStrength = 0.0f;      ///< 0..0.25。色の値で見たときの粒の振れ幅。0 = なし
	std::uint32_t grainCell = 1;     ///< 粒の 1 辺 (出力の画素)。大きい画面で粒が細かすぎるときに上げる
	std::uint32_t seed = 0;          ///< 決定論のフレーム番号
	float pad0 = 0.0f;
	float outWidth = 1.0f;
	float outHeight = 1.0f;
	float pad1 = 0.0f;
	float pad2 = 0.0f;
};

static_assert(sizeof(ColorFinishCb) == 48, "GPU の定数の並びと合わせる");

[[nodiscard]] constexpr bool colorFinishActive(const ColorFinishCb& cb) noexcept
{
	return cb.lutSize >= static_cast<float>(kMinLutSize) || cb.vignetteStrength > 0.0f || cb.grainStrength > 0.0f;
}

/// (x, y, seed) の 32 bit ハッシュ。HLSL と WGSL の符号なし整数のラップアラウンドと同じ結果になるよう、乗算と xor と shift だけで書く
[[nodiscard]] constexpr std::uint32_t grainHash(std::uint32_t x, std::uint32_t y, std::uint32_t seed) noexcept
{
	std::uint32_t h = (x * 0x8da6b343u) ^ (y * 0xd8163841u) ^ (seed * 0xcb1ab31fu);
	h ^= h >> 16;
	h *= 0x7feb352du;
	h ^= h >> 15;
	h *= 0x846ca68bu;
	h ^= h >> 16;
	return h;
}

/// 画素 (x, y) の粒の値 -1..1 (24 bit なので float に落としても丸まらない)
[[nodiscard]] inline float grainNoise(std::uint32_t x, std::uint32_t y, std::uint32_t seed, std::uint32_t cell) noexcept
{
	const std::uint32_t c = std::max(cell, 1u);
	const std::uint32_t h = grainHash(x / c, y / c, seed);
	return static_cast<float>(h >> 8) * (2.0f / 16777216.0f) - 1.0f;
}

using Rgb = std::array<float, 3>;

/// LUT の 1 マス。rgba は幅 N*N・高さ N の RGBA8。画素 (b*N + r, g) が入力 (r, g, b) の出力
[[nodiscard]] inline Rgb lutTexel(const std::vector<std::uint8_t>& rgba, int n, int r, int g, int b) noexcept
{
	const std::size_t i = (static_cast<std::size_t>(g) * static_cast<std::size_t>(n) * static_cast<std::size_t>(n) +
	                       static_cast<std::size_t>(b) * static_cast<std::size_t>(n) + static_cast<std::size_t>(r)) * 4u;
	return {static_cast<float>(rgba[i]) / 255.0f, static_cast<float>(rgba[i + 1]) / 255.0f,
	        static_cast<float>(rgba[i + 2]) / 255.0f};
}

/// トライリニア補間で LUT を引く
[[nodiscard]] inline Rgb sampleLut(const std::vector<std::uint8_t>& rgba, int n, const Rgb& c) noexcept
{
	const float maxIndex = static_cast<float>(n - 1);
	std::array<int, 3> lo{};
	std::array<int, 3> hi{};
	std::array<float, 3> f{};
	for (int k = 0; k < 3; ++k)
	{
		const float s = std::clamp(c[k], 0.0f, 1.0f) * maxIndex;
		const float fl = std::floor(s);
		lo[k] = static_cast<int>(fl);
		hi[k] = std::min(lo[k] + 1, n - 1);
		f[k] = s - fl;
	}
	Rgb out{0.0f, 0.0f, 0.0f};
	for (int corner = 0; corner < 8; ++corner)
	{
		const bool hr = (corner & 1) != 0;
		const bool hg = (corner & 2) != 0;
		const bool hb = (corner & 4) != 0;
		const float w = (hr ? f[0] : 1.0f - f[0]) * (hg ? f[1] : 1.0f - f[1]) * (hb ? f[2] : 1.0f - f[2]);
		const Rgb t = lutTexel(rgba, n, hr ? hi[0] : lo[0], hg ? hi[1] : lo[1], hb ? hi[2] : lo[2]);
		for (int k = 0; k < 3; ++k) { out[k] += t[k] * w; }
	}
	return out;
}

[[nodiscard]] inline float smoothstep01(float edge0, float edge1, float x) noexcept
{
	const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

/// 画素 (x, y) の仕上げ後の色。lut は cb.lutSize が 0 のとき null でよい
[[nodiscard]] inline Rgb colorFinishPixel(const ColorFinishCb& cb, const std::vector<std::uint8_t>* lut, std::uint32_t x,
                                          std::uint32_t y, Rgb c) noexcept
{
	if (cb.lutSize >= static_cast<float>(kMinLutSize) && lut != nullptr)
	{
		const Rgb g = sampleLut(*lut, static_cast<int>(cb.lutSize), c);
		for (int k = 0; k < 3; ++k) { c[k] += (g[k] - c[k]) * cb.lutStrength; }
	}
	if (cb.vignetteStrength > 0.0f)
	{
		const float cx = cb.outWidth * 0.5f;
		const float cy = cb.outHeight * 0.5f;
		const float dx = static_cast<float>(x) + 0.5f - cx;
		const float dy = static_cast<float>(y) + 0.5f - cy;
		const float r = std::sqrt(dx * dx + dy * dy) / std::sqrt(cx * cx + cy * cy);
		const float dark = 1.0f - cb.vignetteStrength * smoothstep01(cb.vignetteRadius, 1.0f, r);
		for (float& v : c) { v *= dark; }
	}
	if (cb.grainStrength > 0.0f)
	{
		const float luma = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
		const float midtone = 0.5f + 0.5f * (4.0f * luma * (1.0f - luma));
		const float delta = grainNoise(x, y, cb.seed, cb.grainCell) * cb.grainStrength * midtone;
		for (float& v : c) { v += delta; }
	}
	for (float& v : c) { v = std::clamp(v, 0.0f, 1.0f); }
	return c;
}

}  // namespace mitiru::render
