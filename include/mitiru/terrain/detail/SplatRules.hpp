#pragma once

/// @file SplatRules.hpp
/// @brief splat マップが無いときに、層の規則 (高さと傾き)から重みを作る。地形の読み込みと仕上げ

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include <mitiru/terrain/HeightfieldLoad.hpp>
#include <mitiru/terrain/OutdoorWorld.hpp>
#include <mitiru/terrain/detail/OutdoorWorldParse.hpp>

namespace mitiru::terrain::detail
{

[[nodiscard]] inline float smoothStep(float a, float b, float x) noexcept
{
	const float t = std::clamp((x - a) / std::max(b - a, 1e-6f), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

/// @brief [lo, hi] の内側を 1 とし、縁を blend の幅でぼかす
[[nodiscard]] inline float band(float v, float lo, float hi, float blend) noexcept
{
	return smoothStep(lo - blend, lo + blend, v) * (1.0f - smoothStep(hi - blend, hi + blend, v));
}

/// @brief 層の規則から重みを求める。規則の無い層は 0 とし、層 0 は残りを受け持つ
[[nodiscard]] inline float ruleWeight(const TerrainLayer& l, float height, float slopeDeg) noexcept
{
	if (!l.hasRule) { return 0.0f; }
	return band(height, l.minHeight, l.maxHeight, l.blend) * band(slopeDeg, l.minSlope, l.maxSlope, l.blend);
}

/// @brief 高さマップの標本ごとに重みを作り、splat[0] に書く。5 層以上では splat[1] にも書く
inline void buildSplatFromRules(OutdoorWorld& w)
{
	const Heightfield& h = w.terrain;
	const std::size_t n = std::min(w.layers.size(), kMaxTerrainLayers);
	const std::size_t images = (n > 4) ? 2 : 1;
	for (std::size_t s = 0; s < images; ++s)
	{
		w.splat[s].width = h.width();
		w.splat[s].height = h.depth();
		w.splat[s].rgba.assign(static_cast<std::size_t>(h.width()) * h.depth() * 4, 0);
	}
	float weight[kMaxTerrainLayers] = {};
	for (std::uint32_t iz = 0; iz < h.depth(); ++iz)
	{
		for (std::uint32_t ix = 0; ix < h.width(); ++ix)
		{
			const Vec3 p = h.vertex(ix, iz);
			const float slope = std::acos(std::clamp(h.smoothNormalAt(p.x, p.z).y, -1.0f, 1.0f)) * 57.29577951f;
			float sum = 0.0f;
			for (std::size_t k = 1; k < n; ++k) { weight[k] = ruleWeight(w.layers[k], p.y, slope); sum += weight[k]; }
			weight[0] = w.layers[0].hasRule ? ruleWeight(w.layers[0], p.y, slope) : std::max(0.0f, 1.0f - sum);
			sum += weight[0];
			if (!(sum > 1e-4f)) { weight[0] = 1.0f; sum = 1.0f; }
			const std::size_t px = static_cast<std::size_t>(iz) * h.width() + ix;
			for (std::size_t k = 0; k < n; ++k)
			{
				w.splat[k / 4].rgba[px * 4 + (k % 4)] = static_cast<std::uint8_t>(std::lround(weight[k] / sum * 255.0f));
			}
		}
	}
}

[[nodiscard]] inline bool loadTerrain(const TerrainSource& src, OutdoorWorld& w, std::string& error)
{
	auto hf = loadHeightfield(src.heightmap, src.placement, src.rawWidth);
	if (!hf.heightfield)
	{
		error = hf.error;
		return false;
	}
	w.terrain = std::move(*hf.heightfield);
	return true;
}

/// @brief ほかの節を読み終えてから、規則の splat、LOD の四分木、草の区画を作る
inline void finishWorld(OutdoorWorld& w)
{
	if (w.splatCount() == 0 && !w.layers.empty()) { buildSplatFromRules(w); }
	w.lodTree = LodQuadtree(w.terrain, w.lod);
	if (w.hasGrass)
	{
		w.grassField = GrassField(w.terrain, w.grassDensity.valid() ? &w.grassDensity : nullptr, w.grass);
	}
}

} // namespace mitiru::terrain::detail
