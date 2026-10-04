#pragma once

/// @file ProbeVolume.hpp
/// @brief 格子に並べた放射照度のプローブ (SH L2) と、プローブから見た面までの距離 (八面体の地図の平均と 2 乗の平均)
/// @details 描く側は 8 個のプローブを三線形の重みで混ぜ、距離の地図でチェビシェフの不等式を使って壁の向こうの
///          プローブを落とす (Majercik らの DDGI、2019 年と同じ漏れの防ぎ方)。sampleIrradiance は HLSL の
///          giIrradiance と同じ式で、焼く側の 2 回目以降の跳ね返りとテストが使う。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include <mitiru/render/gi/ProbeMath.hpp>

namespace mitiru::render::gi
{

/// 距離の地図の 1 辺の画素数 (内側) と、縁を 1 画素ずつ足した 1 枚の大きさ
inline constexpr std::uint32_t kVisibilityInterior = 8;
inline constexpr std::uint32_t kVisibilityTile = kVisibilityInterior + 2;
/// 1 プローブの SH を GPU の float4 何個に詰めるか (27 個の係数 + 有効かどうか)
inline constexpr std::uint32_t kProbeFloat4s = 7;

struct ProbeGridDesc
{
	Vec3 origin{};                      ///< 格子の (0,0,0) のプローブの位置
	Vec3 spacing{1.0f, 1.0f, 1.0f};     ///< 隣のプローブまでの距離
	std::uint32_t dims[3] = {1, 1, 1};  ///< 各軸のプローブの数

	[[nodiscard]] std::uint32_t count() const noexcept { return dims[0] * dims[1] * dims[2]; }
	[[nodiscard]] std::uint32_t index(std::uint32_t x, std::uint32_t y, std::uint32_t z) const noexcept
	{
		return x + dims[0] * (y + dims[1] * z);
	}
	[[nodiscard]] Vec3 position(std::uint32_t x, std::uint32_t y, std::uint32_t z) const noexcept
	{
		return origin + mul(spacing, Vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)});
	}
	[[nodiscard]] Vec3 position(std::uint32_t i) const noexcept
	{
		return position(i % dims[0], (i / dims[0]) % dims[1], i / (dims[0] * dims[1]));
	}
	[[nodiscard]] float minSpacing() const noexcept { return std::min(spacing.x, std::min(spacing.y, spacing.z)); }
};

struct ProbeVolume
{
	ProbeGridDesc grid;
	std::vector<ShL2Rgb> irradiance;   ///< プローブごと。余弦を畳み込んだ放射照度の係数
	std::vector<std::uint8_t> valid;   ///< 0 = 面の中に埋まったプローブ (混ぜない)
	std::vector<float> visibility;     ///< 地図の画素ごとに (平均, 2 乗の平均)。atlasWidth x atlasHeight x 2
	std::uint32_t tilesPerRow = 1;

	[[nodiscard]] std::uint32_t atlasWidth() const noexcept { return tilesPerRow * kVisibilityTile; }
	[[nodiscard]] std::uint32_t atlasHeight() const noexcept
	{
		const std::uint32_t rows = (grid.count() + tilesPerRow - 1) / tilesPerRow;
		return rows * kVisibilityTile;
	}

	/// @brief 格子の大きさに合わせて領域を取る。地図の並びは正方形に近くする
	void allocate(const ProbeGridDesc& g)
	{
		grid = g;
		const std::uint32_t n = g.count();
		tilesPerRow = std::max(1u, static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<double>(n)))));
		irradiance.assign(n, ShL2Rgb{});
		valid.assign(n, 1);
		visibility.assign(static_cast<std::size_t>(atlasWidth()) * atlasHeight() * 2, 0.0f);
	}

	/// @brief プローブ i の地図の左上 (縁を含む)
	void tileOrigin(std::uint32_t i, std::uint32_t& x, std::uint32_t& y) const noexcept
	{
		x = (i % tilesPerRow) * kVisibilityTile;
		y = (i / tilesPerRow) * kVisibilityTile;
	}

	/// @brief 地図の画素 (px, py) の (平均, 2 乗の平均)。範囲の外は端の画素を使う
	[[nodiscard]] std::array<float, 2> texel(int px, int py) const noexcept
	{
		px = std::clamp(px, 0, static_cast<int>(atlasWidth()) - 1);
		py = std::clamp(py, 0, static_cast<int>(atlasHeight()) - 1);
		const std::size_t k = (static_cast<std::size_t>(py) * atlasWidth() + static_cast<std::size_t>(px)) * 2;
		return {visibility[k], visibility[k + 1]};
	}

	/// @brief プローブ i から dir の向きの距離の (平均, 2 乗の平均)。GPU の双線形と同じ読み方
	[[nodiscard]] std::array<float, 2> sampleVisibility(std::uint32_t i, Vec3 dir) const noexcept
	{
		const auto uv = octEncode(dir);
		std::uint32_t tx = 0, ty = 0;
		tileOrigin(i, tx, ty);
		const float fx = static_cast<float>(tx) + 1.0f + (uv[0] * 0.5f + 0.5f) * kVisibilityInterior - 0.5f;
		const float fy = static_cast<float>(ty) + 1.0f + (uv[1] * 0.5f + 0.5f) * kVisibilityInterior - 0.5f;
		const int x0 = static_cast<int>(std::floor(fx));
		const int y0 = static_cast<int>(std::floor(fy));
		const float ax = fx - static_cast<float>(x0);
		const float ay = fy - static_cast<float>(y0);
		const auto a = texel(x0, y0), b = texel(x0 + 1, y0), c = texel(x0, y0 + 1), d = texel(x0 + 1, y0 + 1);
		std::array<float, 2> out{};
		for (int k = 0; k < 2; ++k)
		{
			const float top = a[k] + (b[k] - a[k]) * ax;
			const float bottom = c[k] + (d[k] - c[k]) * ax;
			out[k] = top + (bottom - top) * ay;
		}
		return out;
	}
};

/// @brief 描く側が面から浮かせる既定の距離 (格子の一番狭い間隔の 3 割)
[[nodiscard]] inline float defaultProbeBias(const ProbeGridDesc& grid) noexcept { return grid.minSpacing() * 0.3f; }

namespace detail
{

/// @brief プローブ 1 個の混ぜる重み (三線形を掛ける前)。wrap は面の向き、チェビシェフは壁の向こうを落とす
[[nodiscard]] inline float probeWeight(const ProbeVolume& vol, std::uint32_t idx, Vec3 probe, Vec3 p, Vec3 n, Vec3 biased)
{
	const float wrap = (dot(normalize(probe - p), n) + 1.0f) * 0.5f;
	float w = wrap * wrap + 0.2f;
	const Vec3 toPoint = biased - probe;
	const float r = length(toPoint);
	const auto m = vol.sampleVisibility(idx, r > 1e-4f ? toPoint * (1.0f / r) : Vec3{0.0f, 1.0f, 0.0f});
	if (r > m[0])
	{
		const float var = std::fabs(m[1] - m[0] * m[0]);
		const float d = r - m[0];
		const float ch = var / (var + d * d);
		w *= ch * ch * ch;
	}
	// 小さい重みをさらに潰し、遮られたプローブがわずかに残って光が漏れるのを防ぐ
	if (w < 0.2f) { w *= w * w * 25.0f; }
	return w;
}

} // namespace detail

/// @brief 点 p (法線 n、目への向き v) の放射照度。周りの有効なプローブが無ければ false
/// @param bias 面から浮かせる距離。法線と目の向きへ 2:8 で浮かせる (自分の面の影に入らないため)
[[nodiscard]] inline bool sampleIrradiance(const ProbeVolume& vol, Vec3 p, Vec3 n, Vec3 v, float bias, Vec3& out)
{
	const ProbeGridDesc& g = vol.grid;
	const Vec3 biased = p + (n * 0.2f + v * 0.8f) * bias;
	const Vec3 rel = biased - g.origin;
	const float gf[3] = {rel.x / g.spacing.x, rel.y / g.spacing.y, rel.z / g.spacing.z};
	int base[3];
	float frac[3];
	float inside[3];   // 格子の外の点は、壁の向こうかどうかを格子の端へ寄せた点で測る (HLSL の giIrradiance と同じ)
	for (int a = 0; a < 3; ++a)
	{
		const float hi = static_cast<float>(g.dims[a]) - 1.0f;
		const float c = std::clamp(gf[a], 0.0f, hi);
		base[a] = std::min(static_cast<int>(std::floor(c)), std::max(static_cast<int>(g.dims[a]) - 2, 0));
		frac[a] = std::clamp(c - static_cast<float>(base[a]), 0.0f, 1.0f);
		inside[a] = c;
	}
	const Vec3 measured = g.origin + Vec3{inside[0] * g.spacing.x, inside[1] * g.spacing.y, inside[2] * g.spacing.z};
	Vec3 sum{};
	float wsum = 0.0f;
	for (int corner = 0; corner < 8; ++corner)
	{
		std::uint32_t c[3];
		float tri = 1.0f;
		for (int a = 0; a < 3; ++a)
		{
			const int off = (corner >> a) & 1;
			c[a] = static_cast<std::uint32_t>(std::min(base[a] + off, static_cast<int>(g.dims[a]) - 1));
			tri *= off ? frac[a] : 1.0f - frac[a];
		}
		const std::uint32_t idx = g.index(c[0], c[1], c[2]);
		if (vol.valid[idx] == 0) { continue; }
		const float w = detail::probeWeight(vol, idx, g.position(c[0], c[1], c[2]), p, n, measured) * tri;
		sum = sum + vol.irradiance[idx].irradiance(n) * w;
		wsum += w;
	}
	if (!(wsum > 1e-6f)) { return false; }
	out = sum * (1.0f / wsum);
	return true;
}

} // namespace mitiru::render::gi
