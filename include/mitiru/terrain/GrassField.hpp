#pragma once

/// @file GrassField.hpp
/// @brief 草の区画を目からの距離で選び、区画ごとの本数 (LOD) を決める。草の 1 本 1 本は GPU が置く
/// @details 区画は地形の角を基準とする patchSize m の格子。段 lod では先頭から baseCount >> lod 本を描く。
/// GPU は本の番号のハッシュで位置を決めるため、本数が減っても残る草の位置は変わらない。
/// 密度マップが 0 の区画は読み込み時に記録して選ばない。草は当たり判定に含めない。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <mitiru/terrain/Heightfield.hpp>
#include <mitiru/terrain/Scatter.hpp>
#include <mitiru/terrain/TerrainLod.hpp>

namespace mitiru::terrain
{

struct GrassSettings
{
	float         bladesPerM2 = 24.0f;
	float         patchSize = 8.0f;     ///< 区画の 1 辺 (m)
	float         lodStart = 12.0f;     ///< ここまでは全本数
	float         lodStep = 12.0f;      ///< ここから lodStep m ごとに本数を半分にする
	std::uint32_t maxLod = 4;
	float         fadeStart = 40.0f;    ///< ここから草を縮め始める
	float         fadeEnd = 60.0f;      ///< ここで消える (これより遠い区画は選ばない)
};

/// @brief GPU へそのまま渡す区画 1 つ。
struct GrassPatch
{
	float         x0 = 0.0f;
	float         z0 = 0.0f;
	float         minY = 0.0f;
	float         maxY = 0.0f;
	std::uint32_t index = 0;       ///< 区画の通し番号 (本の位置のハッシュの入力)
	std::uint32_t lod = 0;
	std::uint32_t bladeCount = 0;  ///< この区画で描く本数
	std::uint32_t pad = 0;
};

class GrassField
{
public:
	GrassField() = default;

	/// @brief 区画ごとの高さの範囲と、密度が 0 でないかを先に調べる。
	GrassField(const Heightfield& h, const DensityMap* density, const GrassSettings& s) : m_settings(s), m_field(&h)
	{
		if (!h.valid() || !(s.patchSize > 0.0f)) { return; }
		m_countX = static_cast<std::uint32_t>(std::ceil(h.placement().sizeX / s.patchSize));
		m_countZ = static_cast<std::uint32_t>(std::ceil(h.placement().sizeZ / s.patchSize));
		m_patches.resize(static_cast<std::size_t>(m_countX) * m_countZ);
		for (std::uint32_t pz = 0; pz < m_countZ; ++pz)
		{
			for (std::uint32_t px = 0; px < m_countX; ++px) { initPatch(px, pz, density); }
		}
	}

	[[nodiscard]] const GrassSettings& settings() const noexcept { return m_settings; }

	/// @brief 段 0 の区画の本数。
	[[nodiscard]] std::uint32_t baseCount() const noexcept
	{
		return static_cast<std::uint32_t>(std::lround(m_settings.bladesPerM2 * m_settings.patchSize * m_settings.patchSize));
	}

	[[nodiscard]] std::uint32_t lodAt(float d) const noexcept
	{
		if (d <= m_settings.lodStart || !(m_settings.lodStep > 0.0f)) { return 0; }
		const auto lod = static_cast<std::uint32_t>((d - m_settings.lodStart) / m_settings.lodStep) + 1u;
		return std::min(lod, m_settings.maxLod);
	}

	/// @brief 目から fadeEnd 以内にあり、visible(LodBox) が true の区画を out へ書く。
	template <class Visible>
	void select(const Vec3& eye, Visible&& visible, std::vector<GrassPatch>& out) const
	{
		out.clear();
		if (m_patches.empty()) { return; }
		const float s = m_settings.patchSize;
		const float reach = m_settings.fadeEnd + s * 0.7072f;
		const auto lo = [&](float v, float origin) { return static_cast<std::int64_t>(std::floor((v - reach - origin) / s)); };
		const std::int64_t px0 = std::max<std::int64_t>(0, lo(eye.x, m_field->minX()));
		const std::int64_t pz0 = std::max<std::int64_t>(0, lo(eye.z, m_field->minZ()));
		const std::int64_t px1 = std::min<std::int64_t>(m_countX - 1, lo(eye.x + 2.0f * reach, m_field->minX()));
		const std::int64_t pz1 = std::min<std::int64_t>(m_countZ - 1, lo(eye.z + 2.0f * reach, m_field->minZ()));
		for (std::int64_t pz = pz0; pz <= pz1; ++pz)
		{
			for (std::int64_t px = px0; px <= px1; ++px)
			{
				considerPatch(static_cast<std::uint32_t>(px), static_cast<std::uint32_t>(pz), eye, visible, out);
			}
		}
	}

private:
	struct PatchInfo
	{
		float minY = 0.0f;
		float maxY = 0.0f;
		bool  empty = true;
	};

	void initPatch(std::uint32_t px, std::uint32_t pz, const DensityMap* density)
	{
		const float s = m_settings.patchSize;
		const float x0 = m_field->minX() + static_cast<float>(px) * s;
		const float z0 = m_field->minZ() + static_cast<float>(pz) * s;
		PatchInfo& info = m_patches[static_cast<std::size_t>(pz) * m_countX + px];
		const auto ix0 = static_cast<std::uint32_t>((x0 - m_field->minX()) / m_field->cellX());
		const auto iz0 = static_cast<std::uint32_t>((z0 - m_field->minZ()) / m_field->cellZ());
		const auto ix1 = static_cast<std::uint32_t>(std::ceil((x0 + s - m_field->minX()) / m_field->cellX()));
		const auto iz1 = static_cast<std::uint32_t>(std::ceil((z0 + s - m_field->minZ()) / m_field->cellZ()));
		const auto [lo, hi] = m_field->heightRange(ix0, iz0, ix1, iz1);
		info.minY = lo;
		info.maxY = hi;
		info.empty = density != nullptr && density->valid() && maxDensity(*density, x0, z0, s) <= 0.0f;
	}

	[[nodiscard]] float maxDensity(const DensityMap& d, float x0, float z0, float s) const noexcept
	{
		const auto toPx = [&](float v, float origin, float size, std::uint32_t n) {
			return std::clamp(static_cast<std::int64_t>((v - origin) / size * static_cast<float>(n - 1)), std::int64_t{0},
			                  static_cast<std::int64_t>(n - 1));
		};
		const auto& p = m_field->placement();
		std::uint8_t best = 0;
		for (auto z = toPx(z0, p.originZ, p.sizeZ, d.height); z <= toPx(z0 + s, p.originZ, p.sizeZ, d.height) + 1; ++z)
		{
			for (auto x = toPx(x0, p.originX, p.sizeX, d.width); x <= toPx(x0 + s, p.originX, p.sizeX, d.width) + 1; ++x)
			{
				if (x >= d.width || z >= d.height) { continue; }
				best = std::max(best, d.values[static_cast<std::size_t>(z) * d.width + static_cast<std::size_t>(x)]);
			}
		}
		return best / 255.0f;
	}

	template <class Visible>
	void considerPatch(std::uint32_t px, std::uint32_t pz, const Vec3& eye, Visible& visible, std::vector<GrassPatch>& out) const
	{
		const std::size_t i = static_cast<std::size_t>(pz) * m_countX + px;
		const PatchInfo& info = m_patches[i];
		if (info.empty || baseCount() == 0) { return; }
		const float s = m_settings.patchSize;
		const float x0 = m_field->minX() + static_cast<float>(px) * s;
		const float z0 = m_field->minZ() + static_cast<float>(pz) * s;
		// 消えるかは高さを含む距離で決め、段は水平距離で決める。上から見下ろした区画も距離が遠ければ描かない。
		const float dx = std::max({x0 - eye.x, 0.0f, eye.x - (x0 + s)});
		const float dy = std::max({info.minY - eye.y, 0.0f, eye.y - (info.maxY + 2.0f)});
		const float dz = std::max({z0 - eye.z, 0.0f, eye.z - (z0 + s)});
		if (dx * dx + dy * dy + dz * dz > m_settings.fadeEnd * m_settings.fadeEnd) { return; }
		if (!visible(LodBox{x0, info.minY, z0, x0 + s, info.maxY + 2.0f, z0 + s})) { return; }
		const float cx = x0 + s * 0.5f - eye.x;
		const float cz = z0 + s * 0.5f - eye.z;
		GrassPatch g;
		g.x0 = x0;
		g.z0 = z0;
		g.minY = info.minY;
		g.maxY = info.maxY;
		g.index = static_cast<std::uint32_t>(i);
		g.lod = lodAt(std::sqrt(cx * cx + cz * cz));
		g.bladeCount = std::max<std::uint32_t>(1u, baseCount() >> g.lod);
		out.push_back(g);
	}

	GrassSettings          m_settings{};
	const Heightfield*     m_field = nullptr;
	std::uint32_t          m_countX = 0;
	std::uint32_t          m_countZ = 0;
	std::vector<PatchInfo> m_patches;
};

} // namespace mitiru::terrain
