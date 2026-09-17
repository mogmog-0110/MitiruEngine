#pragma once

/// @file IblBrdfLut.hpp
/// @brief split-sum 近似の環境 BRDF 表 (Karis 2013)。横軸 NdotV、縦軸 roughness、値は (A, B) で、
///        鏡面 IBL = prefiltered(R, roughness) * (F0 * A + B)。GPU 側の実装に依存せず CPU で 1 回だけ
///        積分して 2 チャンネルのテクスチャに焼く (64x64 x 128 サンプルで数十 ms)。

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace mitiru::render
{

struct BrdfLutData
{
	int                size = 0;   ///< 一辺 (正方形)
	std::vector<float> rg;         ///< size*size*2。x = NdotV、y = roughness (どちらも [0,1]、画素中心)

	[[nodiscard]] bool valid() const noexcept { return size > 0 && rg.size() == static_cast<std::size_t>(size) * size * 2; }
	[[nodiscard]] std::pair<float, float> at(int x, int y) const noexcept
	{
		const std::size_t i = (static_cast<std::size_t>(y) * size + x) * 2;
		return {rg[i], rg[i + 1]};
	}
};

namespace detail
{
/// van der Corput 基数反転列 (Hammersley 点列の第 2 成分)。
inline float radicalInverseVdC(std::uint32_t bits) noexcept
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

/// Schlick-GGX の幾何項 (IBL 用の k = a^2/2。直接光の (r+1)^2/8 とは別)。
inline float geometrySchlickGGXIbl(float nDotV, float roughness) noexcept
{
	const float a = roughness * roughness;
	const float k = a / 2.0f;
	return nDotV / (nDotV * (1.0f - k) + k);
}
}  // namespace detail

/// @brief (NdotV, roughness) 1 点の (A, B) を GGX 重点サンプリングで積分する。
inline std::pair<float, float> integrateBrdf(float nDotV, float roughness, int sampleCount = 128) noexcept
{
	nDotV = nDotV < 1e-4f ? 1e-4f : (nDotV > 1.0f ? 1.0f : nDotV);
	roughness = roughness < 0.0f ? 0.0f : (roughness > 1.0f ? 1.0f : roughness);
	const float vx = std::sqrt(1.0f - nDotV * nDotV);  // V を xz 平面に置く (N = +z)
	const float vz = nDotV;
	const float a = roughness * roughness;
	constexpr float kPi = 3.14159265358979323846f;

	float sumA = 0.0f, sumB = 0.0f;
	for (int i = 0; i < sampleCount; ++i)
	{
		const float xi1 = (static_cast<float>(i) + 0.5f) / static_cast<float>(sampleCount);
		const float xi2 = detail::radicalInverseVdC(static_cast<std::uint32_t>(i));
		const float phi = 2.0f * kPi * xi1;
		const float cosTheta = std::sqrt((1.0f - xi2) / (1.0f + (a * a - 1.0f) * xi2));
		const float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
		const float hx = sinTheta * std::cos(phi), hy = sinTheta * std::sin(phi), hz = cosTheta;

		const float vDotH = vx * hx + vz * hz;
		const float lz    = 2.0f * vDotH * hz - vz;   // L = 2(V.H)H - V の z 成分 = NdotL
		(void)hy;
		if (lz <= 0.0f) { continue; }

		const float nDotH = hz > 0.0f ? hz : 0.0f;
		const float vh    = vDotH > 0.0f ? vDotH : 0.0f;
		const float g     = detail::geometrySchlickGGXIbl(nDotV, roughness) * detail::geometrySchlickGGXIbl(lz, roughness);
		const float gVis  = (g * vh) / (nDotH * nDotV + 1e-6f);
		const float fc    = std::pow(1.0f - vh, 5.0f);
		sumA += (1.0f - fc) * gVis;
		sumB += fc * gVis;
	}
	return {sumA / static_cast<float>(sampleCount), sumB / static_cast<float>(sampleCount)};
}

/// @brief 表を焼く。size は 16 以上、sampleCount は 32 以上を推奨 (既定 64 / 128)。
inline BrdfLutData computeBrdfLut(int size = 64, int sampleCount = 128)
{
	BrdfLutData lut;
	if (size <= 0) { return lut; }
	lut.size = size;
	lut.rg.assign(static_cast<std::size_t>(size) * size * 2, 0.0f);
	for (int y = 0; y < size; ++y)
	{
		const float roughness = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
		for (int x = 0; x < size; ++x)
		{
			const float nDotV = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
			const auto [a, b] = integrateBrdf(nDotV, roughness, sampleCount);
			const std::size_t i = (static_cast<std::size_t>(y) * size + x) * 2;
			lut.rg[i]     = a;
			lut.rg[i + 1] = b;
		}
	}
	return lut;
}

}  // namespace mitiru::render
