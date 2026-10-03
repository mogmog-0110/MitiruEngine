#pragma once

/// @file ReflectionBake.hpp
/// @brief 箱で映す反射のプローブを CPU のレイで撮り、粗さごとの mip を GGX で畳み込む
/// @details 面の並びと向きは D3D の TextureCube と同じ (+X, -X, +Y, -Y, +Z, -Z)。撮った面の放射輝度は
///          直接光と、焼いた放射照度の格子から引いた間接光と、自発光の和。mip i は粗さ i / (段数 - 1) で、
///          IBL の prefilter と同じ割り当てなので、描く側は粗さ x (段数 - 1) の段を引けばよい。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include <mitiru/render/gi/BakeScene.hpp>
#include <mitiru/render/gi/GiBaker.hpp>
#include <mitiru/render/gi/LightingBakeFile.hpp>

namespace mitiru::render::gi
{

struct ReflectionProbeDesc
{
	Vec3 boxMin{};
	Vec3 boxMax{};
	Vec3 position{};
	float blend = 0.5f;
};

struct ReflectionBakeSettings
{
	std::uint32_t size = 64;        ///< mip 0 の 1 面の一辺
	std::uint32_t supersample = 2;  ///< 1 画素を supersample^2 本のレイで撮る
	std::uint32_t prefilterSamples = 64;
	std::uint32_t threads = 0;
};

/// @brief 浮動小数の cube の 1 段。面ごとに size x size の放射輝度
struct CubeLevel
{
	std::uint32_t size = 0;
	std::array<std::vector<Vec3>, 6> faces;

	void allocate(std::uint32_t s)
	{
		size = s;
		for (auto& f : faces) { f.assign(static_cast<std::size_t>(s) * s, Vec3{}); }
	}
};

/// @brief 面 face の (u, v) (どちらも -1..1) の向き
[[nodiscard]] inline Vec3 cubeDirection(int face, float u, float v) noexcept
{
	switch (face)
	{
	case 0: return normalize({1.0f, -v, -u});
	case 1: return normalize({-1.0f, -v, u});
	case 2: return normalize({u, 1.0f, v});
	case 3: return normalize({u, -1.0f, -v});
	case 4: return normalize({u, -v, 1.0f});
	default: return normalize({-u, -v, -1.0f});
	}
}

/// @brief 向き d が当たる面と (u, v)
inline void cubeFace(Vec3 d, int& face, float& u, float& v) noexcept
{
	const float ax = std::fabs(d.x), ay = std::fabs(d.y), az = std::fabs(d.z);
	if (ax >= ay && ax >= az)
	{
		face = d.x > 0.0f ? 0 : 1;
		u = (d.x > 0.0f ? -d.z : d.z) / ax;
		v = -d.y / ax;
	}
	else if (ay >= az)
	{
		face = d.y > 0.0f ? 2 : 3;
		u = d.x / ay;
		v = (d.y > 0.0f ? d.z : -d.z) / ay;
	}
	else
	{
		face = d.z > 0.0f ? 4 : 5;
		u = (d.z > 0.0f ? d.x : -d.x) / az;
		v = -d.y / az;
	}
}

/// @brief 面の中で双線形に引く (縁は面の端の画素を使う)
[[nodiscard]] inline Vec3 sampleCube(const CubeLevel& c, Vec3 d) noexcept
{
	int face = 0;
	float u = 0.0f, v = 0.0f;
	cubeFace(d, face, u, v);
	const float fx = (u * 0.5f + 0.5f) * static_cast<float>(c.size) - 0.5f;
	const float fy = (v * 0.5f + 0.5f) * static_cast<float>(c.size) - 0.5f;
	const int hi = static_cast<int>(c.size) - 1;
	const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, hi), y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, hi);
	const int x1 = std::min(x0 + 1, hi), y1 = std::min(y0 + 1, hi);
	const float ax = std::clamp(fx - static_cast<float>(x0), 0.0f, 1.0f), ay = std::clamp(fy - static_cast<float>(y0), 0.0f, 1.0f);
	const auto& f = c.faces[face];
	const auto at = [&](int x, int y) { return f[static_cast<std::size_t>(y) * c.size + static_cast<std::size_t>(x)]; };
	const Vec3 top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * ax;
	const Vec3 bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * ax;
	return top + (bottom - top) * ay;
}

namespace detail
{

/// @brief 撮る位置から向き d に見える放射輝度
[[nodiscard]] inline Vec3 shadeReflectionRay(const BakeScene& scene, const ProbeVolume* volume, Vec3 origin, Vec3 d)
{
	BakeHit hit;
	if (!scene.trace(origin, d, 1e6f, hit)) { return scene.sky.radiance(d); }
	if (hit.backface) { return {}; }
	const BakeMaterial& m = scene.materialOf(hit.triangle);
	const Vec3 p = origin + d * hit.t;
	Vec3 e = scene.directIrradiance(p, hit.normal);
	Vec3 indirect{};
	if (volume != nullptr && sampleIrradiance(*volume, p, hit.normal, d * -1.0f, defaultProbeBias(volume->grid), indirect))
	{
		e = e + indirect;
	}
	return mul(m.albedo, e) * (1.0f / kPi) + m.emissive;
}

/// @brief 2 x 2 の平均で 1 段縮める
[[nodiscard]] inline CubeLevel downsample(const CubeLevel& src)
{
	CubeLevel dst;
	dst.allocate(std::max(1u, src.size / 2));
	for (int f = 0; f < 6; ++f)
	{
		for (std::uint32_t y = 0; y < dst.size; ++y)
		{
			for (std::uint32_t x = 0; x < dst.size; ++x)
			{
				const auto at = [&](std::uint32_t sx, std::uint32_t sy) {
					return src.faces[f][static_cast<std::size_t>(std::min(sy, src.size - 1)) * src.size + std::min(sx, src.size - 1)];
				};
				dst.faces[f][static_cast<std::size_t>(y) * dst.size + x] =
					(at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1)) * 0.25f;
			}
		}
	}
	return dst;
}

[[nodiscard]] inline float radicalInverse(std::uint32_t bits) noexcept
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

/// @brief n = v = r とみなした GGX の重点標本で、向き n の周りを畳み込む。標本の立体角に合う段の縮小版から引く
[[nodiscard]] inline Vec3 prefilterTexel(const std::vector<CubeLevel>& chain, Vec3 n, float roughness, std::uint32_t samples)
{
	const float a = roughness * roughness;
	const Vec3 up = std::fabs(n.y) < 0.999f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
	const Vec3 tx = normalize(cross(up, n));
	const Vec3 ty = cross(n, tx);
	const float texelSolidAngle = 4.0f * kPi / (6.0f * static_cast<float>(chain[0].size * chain[0].size));
	Vec3 sum{};
	float wsum = 0.0f;
	for (std::uint32_t i = 0; i < samples; ++i)
	{
		const float e1 = (static_cast<float>(i) + 0.5f) / static_cast<float>(samples);
		const float e2 = radicalInverse(i);
		const float phi = 2.0f * kPi * e2;
		const float cosT = std::sqrt((1.0f - e1) / (1.0f + (a * a - 1.0f) * e1));
		const float sinT = std::sqrt(std::max(0.0f, 1.0f - cosT * cosT));
		const Vec3 h = tx * (std::cos(phi) * sinT) + ty * (std::sin(phi) * sinT) + n * cosT;
		const Vec3 l = h * (2.0f * dot(n, h)) - n;
		const float ndl = dot(n, l);
		if (ndl <= 0.0f) { continue; }
		const float d2 = (cosT * cosT) * (a * a - 1.0f) + 1.0f;
		const float pdf = (a * a) / std::max(kPi * d2 * d2, 1e-8f) * 0.25f;
		const float sampleSolidAngle = 1.0f / (static_cast<float>(samples) * std::max(pdf, 1e-8f));
		const float level = std::clamp(0.5f * std::log2(sampleSolidAngle / texelSolidAngle) + 1.0f, 0.0f,
		                               static_cast<float>(chain.size() - 1));
		sum = sum + sampleCube(chain[static_cast<std::size_t>(level + 0.5f)], l) * ndl;
		wsum += ndl;
	}
	return wsum > 0.0f ? sum * (1.0f / wsum) : sampleCube(chain[0], n);
}

[[nodiscard]] inline CubeLevel captureCube(const BakeScene& scene, const ProbeVolume* volume, const ReflectionProbeDesc& desc,
                                           const ReflectionBakeSettings& s)
{
	CubeLevel cube;
	cube.allocate(std::max(1u, s.size));
	const std::uint32_t ss = std::max(1u, s.supersample);
	const std::uint32_t rows = cube.size * 6;
	parallelFor(rows, s.threads, [&](std::uint32_t row) {
		const int face = static_cast<int>(row / cube.size);
		const std::uint32_t y = row % cube.size;
		for (std::uint32_t x = 0; x < cube.size; ++x)
		{
			Vec3 sum{};
			for (std::uint32_t sy = 0; sy < ss; ++sy)
			{
				for (std::uint32_t sx = 0; sx < ss; ++sx)
				{
					const float u = (static_cast<float>(x) + (static_cast<float>(sx) + 0.5f) / ss) / cube.size * 2.0f - 1.0f;
					const float v = (static_cast<float>(y) + (static_cast<float>(sy) + 0.5f) / ss) / cube.size * 2.0f - 1.0f;
					sum = sum + shadeReflectionRay(scene, volume, desc.position, cubeDirection(face, u, v));
				}
			}
			cube.faces[face][static_cast<std::size_t>(y) * cube.size + x] = sum * (1.0f / static_cast<float>(ss * ss));
		}
	});
	return cube;
}

} // namespace detail

/// @brief 反射のプローブ 1 個を焼く。volume は焼いた放射照度の格子 (無ければ null で、間接光を入れない)
[[nodiscard]] inline ReflectionProbeData bakeReflectionProbe(const BakeScene& scene, const ProbeVolume* volume,
                                                             const ReflectionProbeDesc& desc, const ReflectionBakeSettings& s = {})
{
	std::vector<CubeLevel> chain;
	chain.push_back(detail::captureCube(scene, volume, desc, s));
	while (chain.back().size > 1) { chain.push_back(detail::downsample(chain.back())); }

	ReflectionProbeData out;
	out.boxMin = desc.boxMin;
	out.boxMax = desc.boxMax;
	out.position = desc.position;
	out.blend = desc.blend;
	out.size = chain[0].size;
	out.mips = std::min<std::uint32_t>(kReflectionMips, static_cast<std::uint32_t>(chain.size()));
	out.texels.resize(ReflectionProbeData::texelCount(out.size, out.mips) * 4);
	std::size_t cursor = 0;
	for (int face = 0; face < 6; ++face)
	{
		for (std::uint32_t mip = 0; mip < out.mips; ++mip)
		{
			const std::uint32_t size = std::max(1u, out.size >> mip);
			const float roughness = out.mips > 1 ? static_cast<float>(mip) / static_cast<float>(out.mips - 1) : 0.0f;
			for (std::uint32_t y = 0; y < size; ++y)
			{
				for (std::uint32_t x = 0; x < size; ++x)
				{
					const float u = (static_cast<float>(x) + 0.5f) / size * 2.0f - 1.0f;
					const float v = (static_cast<float>(y) + 0.5f) / size * 2.0f - 1.0f;
					const Vec3 c = mip == 0 ? chain[0].faces[face][static_cast<std::size_t>(y) * size + x]
					                        : detail::prefilterTexel(chain, cubeDirection(face, u, v), roughness, s.prefilterSamples);
					out.texels[cursor++] = floatToHalf(c.x);
					out.texels[cursor++] = floatToHalf(c.y);
					out.texels[cursor++] = floatToHalf(c.z);
					out.texels[cursor++] = floatToHalf(1.0f);
				}
			}
		}
	}
	return out;
}

} // namespace mitiru::render::gi
