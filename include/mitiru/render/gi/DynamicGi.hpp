#pragma once

/// @file DynamicGi.hpp
/// @brief 動く光の GI (DDGI) の CPU 側の決まり。段ごとのレイの数と格子、フレームごとのレイの回転、レイが当たる三角形の値、
///        焼いた光から作る格子の種
/// @details GPU の側は render/dx12/DX12DynamicGi.hpp と tools/ddgi_shaders/ddgi.hlsl。格子の並び (SH L2 の float4 x 7 と
///          距離の地図) は焼いた光 (ProbeVolume.hpp) と同じで、前方の描画と clod は同じ HLSL で引く。決めたことは ADR 0074。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include <mitiru/render/ColorSpace.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/QualityCaps.hpp>
#include <mitiru/render/gi/LightingBakeFile.hpp>
#include <mitiru/render/gi/ProbeVolume.hpp>

namespace mitiru::render::gi
{

/// @brief 段ごとの 1 プローブのレイの数と、焼いた格子を何倍に細かくするか
struct DynamicGiPreset
{
	std::uint32_t raysPerProbe = 0;   ///< 0 = 使わない
	std::uint32_t density = 1;
};

[[nodiscard]] constexpr DynamicGiPreset dynamicGiPreset(DynamicGiQuality q) noexcept
{
	switch (q)
	{
	case DynamicGiQuality::Off:    return {0, 1};
	case DynamicGiQuality::Low:    return {64, 1};
	case DynamicGiQuality::Medium: return {128, 1};
	case DynamicGiQuality::High:   return {192, 2};
	}
	return {128, 1};
}

/// @brief 履歴の混ぜ方と、面の中に埋まったプローブの見分け方 (host の既定。ゲームからは変えない)
struct DynamicGiTuning
{
	float hysteresis = 0.97f;          ///< 前の値を残す割合
	float fastHysteresis = 0.3f;       ///< DC の放射照度が大きく変わったプローブだけ、この割合まで履歴を軽くする
	float changeThreshold = 0.1f;      ///< 大きく変わったとみなす DC の変化の割合 (128 本のレイの揺れより大きく、光の点灯より小さい)
	float backfaceInvalid = 0.25f;     ///< 裏面に当たったレイの割合 (の履歴) がこれを超えたプローブは使わない (焼く側と同じ)
	float backfaceHistory = 0.9f;
	float visibilitySharpness = 50.0f; ///< 距離の地図の 1 画素がレイを集める余弦の指数 (焼く側と同じ)
	std::uint32_t maxLocalLights = 16; ///< 当たった面を照らす局所光の上限 (近い順ではなく積まれた順)
	/// 局所光の影のレイを光の手前のこの距離 (m) で止める。灯りの位置に描いた球や松明の形が、自分の光を塞がないため
	float lightShadowMargin = 0.25f;
};

/// @brief density 倍に細かくした格子。端のプローブは元の格子と同じ位置に来る
[[nodiscard]] inline ProbeGridDesc densifyGrid(const ProbeGridDesc& g, std::uint32_t density) noexcept
{
	density = std::max(density, 1u);
	ProbeGridDesc out = g;
	out.spacing = g.spacing * (1.0f / static_cast<float>(density));
	for (int a = 0; a < 3; ++a) { out.dims[a] = (g.dims[a] - 1) * density + 1; }
	return out;
}

/// @brief 距離の地図に入れる距離の上限 (焼く側の bakeProbeVolume と同じ)
[[nodiscard]] inline float visibilityMaxDistance(const ProbeGridDesc& g) noexcept { return 1.5f * length(g.spacing); }

/// @brief フレーム frame のレイの回転 (行優先の 3x3)。番号のハッシュから作る一様な回転 (Shoemake の方法) なので、
///        同じフレーム番号からは同じ回転が出て、撮影とリプレイの絵が実行ごとに変わらない
[[nodiscard]] inline std::array<float, 9> frameRayRotation(std::uint64_t frame) noexcept
{
	const auto key = static_cast<std::uint32_t>(frame ^ (frame >> 32));
	const float u1 = hash01(key * 3u + 0x68e31da4u);
	const float u2 = hash01(key * 3u + 1u + 0xb5297a4du) * 2.0f * kPi;
	const float u3 = hash01(key * 3u + 2u + 0x1b56c4e9u) * 2.0f * kPi;
	const float a = std::sqrt(1.0f - u1), b = std::sqrt(u1);
	const float x = a * std::sin(u2), y = a * std::cos(u2), z = b * std::sin(u3), w = b * std::cos(u3);
	return {1 - 2 * (y * y + z * z), 2 * (x * y - w * z),     2 * (x * z + w * y),
	        2 * (x * y + w * z),     1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
	        2 * (x * z - w * y),     2 * (y * z + w * x),     1 - 2 * (x * x + y * y)};
}

/// @brief レイが当たった三角形で読む値。object 空間の表の法線と線形の基本色を 16 bit の浮動小数で詰める (ddgi.hlsl の shadeHit)
struct RayTriangleAttrib
{
	std::uint32_t w[4] = {0, 0, 0, 0};
};

[[nodiscard]] inline RayTriangleAttrib packRayTriangle(Vec3 normal, Vec3 albedo) noexcept
{
	const auto h = [](float v) { return static_cast<std::uint32_t>(floatToHalf(v)); };
	RayTriangleAttrib a;
	a.w[0] = h(normal.x) | (h(normal.y) << 16);
	a.w[1] = h(normal.z) | (h(albedo.x) << 16);
	a.w[2] = h(albedo.y) | (h(albedo.z) << 16);
	return a;
}

/// @brief レイを当てる 1 つの形。位置は float x 3、三角形ごとに添字 3 個と RayTriangleAttrib 1 個
struct RayGeometry
{
	std::vector<float> positions;
	std::vector<std::uint32_t> indices;
	std::vector<RayTriangleAttrib> attribs;

	[[nodiscard]] std::uint32_t vertexCount() const noexcept { return static_cast<std::uint32_t>(positions.size() / 3); }
	[[nodiscard]] std::uint32_t triangleCount() const noexcept { return static_cast<std::uint32_t>(indices.size() / 3); }
};

namespace detail
{

[[nodiscard]] inline Vec3 toGi(const sgc::Vec3f& v) noexcept { return {v.x, v.y, v.z}; }

/// @brief 三角形の表の向き。頂点の並びより、描く側が付けた頂点の法線の向きに合わせる (並びの向きはモデルごとに違う)
[[nodiscard]] inline Vec3 triangleNormal(const Vertex3D& a, const Vertex3D& b, const Vertex3D& c) noexcept
{
	const Vec3 n = cross(toGi(b.position) - toGi(a.position), toGi(c.position) - toGi(a.position));
	const Vec3 authored = toGi(a.normal) + toGi(b.normal) + toGi(c.normal);
	if (!(dot(n, n) > 1e-24f)) { return normalize(authored); }
	const Vec3 unit = normalize(n);
	return dot(unit, authored) < 0.0f ? unit * -1.0f : unit;
}

[[nodiscard]] inline Vec3 linearVertexColor(const Vertex3D& v) noexcept
{
	return {srgbToLinear(v.color.r), srgbToLinear(v.color.g), srgbToLinear(v.color.b)};
}

} // namespace detail

/// @brief Mesh の三角形をレイを当てる形にする。基本色は 3 頂点の色 (sRGB) を線形にした平均で、描く時の材質の色は別に掛ける。
///        g の配列は空にしてから詰める (毎フレーム形の変わる Mesh でも確保し直さない)
inline void buildRayGeometry(const Mesh& mesh, RayGeometry& g)
{
	g.positions.clear();
	g.indices.clear();
	g.attribs.clear();
	const auto& v = mesh.vertices();
	for (const Vertex3D& p : v) { g.positions.insert(g.positions.end(), {p.position.x, p.position.y, p.position.z}); }
	const auto& ix = mesh.indices();
	if (ix.empty())
	{
		g.indices.resize(v.size() - v.size() % 3);
		for (std::size_t i = 0; i < g.indices.size(); ++i) { g.indices[i] = static_cast<std::uint32_t>(i); }
	}
	else { g.indices.assign(ix.begin(), ix.end() - static_cast<std::ptrdiff_t>(ix.size() % 3)); }
	for (std::size_t t = 0; t + 2 < g.indices.size(); t += 3)
	{
		const Vertex3D& a = v[g.indices[t]];
		const Vertex3D& b = v[g.indices[t + 1]];
		const Vertex3D& c = v[g.indices[t + 2]];
		const Vec3 albedo = (detail::linearVertexColor(a) + detail::linearVertexColor(b) + detail::linearVertexColor(c)) * (1.0f / 3.0f);
		g.attribs.push_back(packRayTriangle(detail::triangleNormal(a, b, c), albedo));
	}
}

/// @brief world 行列 (行優先 3x4 の行) の法線の行列 (左上 3x3 の逆の転置) の行。潰れた行列なら単位行列
[[nodiscard]] inline std::array<float, 9> normalMatrix(const float (&m)[3][4]) noexcept
{
	const float c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
	const float c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2];
	const float c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
	const float det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;
	if (!(std::fabs(det) > 1e-20f)) { return {1, 0, 0, 0, 1, 0, 0, 0, 1}; }
	const float k = 1.0f / det;
	// 逆の転置 = 余因子の行列 / det
	return {c00 * k, c01 * k, c02 * k,
	        (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * k, (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * k,
	        (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * k,
	        (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * k, (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * k,
	        (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * k};
}

/// @brief 焼いた光から作る DDGI の格子の最初の値
struct DynamicGiSeed
{
	ProbeVolume volume;
	bool hasVisibility = false;   ///< 距離の地図も焼いた値を使えるか (細かくした格子では使えない)
};

namespace detail
{

/// @brief 細かくした格子のプローブ i の SH を、元の格子の周りの有効なプローブから三線形で混ぜる
[[nodiscard]] inline ShL2Rgb resampleProbe(const ProbeVolume& from, Vec3 p, bool& any)
{
	const ProbeGridDesc& g = from.grid;
	const Vec3 rel = p - g.origin;
	const float f[3] = {rel.x / g.spacing.x, rel.y / g.spacing.y, rel.z / g.spacing.z};
	ShL2Rgb out;
	float wsum = 0.0f;
	for (int corner = 0; corner < 8; ++corner)
	{
		std::uint32_t c[3];
		float w = 1.0f;
		for (int a = 0; a < 3; ++a)
		{
			const float hi = static_cast<float>(g.dims[a]) - 1.0f;
			const float x = std::clamp(f[a], 0.0f, hi);
			const auto base = static_cast<std::uint32_t>(std::min(std::floor(x), std::max(hi - 1.0f, 0.0f)));
			const float t = x - static_cast<float>(base);
			const int off = (corner >> a) & 1;
			c[a] = std::min(base + static_cast<std::uint32_t>(off), g.dims[a] - 1);
			w *= off ? t : 1.0f - t;
		}
		const std::uint32_t idx = g.index(c[0], c[1], c[2]);
		if (from.valid[idx] == 0 || w <= 0.0f) { continue; }
		for (int k = 0; k < kShCoefficients; ++k) { out.c[k] = out.c[k] + from.irradiance[idx].c[k] * w; }
		wsum += w;
	}
	any = wsum > 0.0f;
	if (any)
	{
		for (auto& c : out.c) { c = c * (1.0f / wsum); }
	}
	return out;
}

} // namespace detail

[[nodiscard]] inline DynamicGiSeed seedDynamicGi(const ProbeVolume& baked, std::uint32_t density)
{
	DynamicGiSeed seed;
	if (density <= 1)
	{
		seed.volume = baked;
		seed.hasVisibility = true;
		return seed;
	}
	seed.volume.allocate(densifyGrid(baked.grid, density));
	const float maxDistance = visibilityMaxDistance(seed.volume.grid);
	for (std::size_t i = 0; i < seed.volume.visibility.size(); i += 2)
	{
		seed.volume.visibility[i] = maxDistance;
		seed.volume.visibility[i + 1] = maxDistance * maxDistance;
	}
	for (std::uint32_t i = 0; i < seed.volume.grid.count(); ++i)
	{
		bool any = false;
		seed.volume.irradiance[i] = detail::resampleProbe(baked, seed.volume.grid.position(i), any);
		seed.volume.valid[i] = any ? 1 : 0;
	}
	return seed;
}

} // namespace mitiru::render::gi
