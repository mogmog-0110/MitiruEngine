#pragma once

/// @file HeightfieldCollision.hpp
/// @brief 高さマップを三角形にして action::CollisionLevelBuilder に追加する
/// @details 三角形は heightAt と同じ対角 b-c で分け、表を上向きにする。
///          id はマス (ix, iz) ごとに行優先で 2 枚ずつ割り当てるため、terrainTriangleId でマスを特定できる。
///          1025 x 1025 の地形は約 200 万枚になるため、歩ける範囲だけ必要なときは region で絞る。

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

#include <mitiru/action/CollisionLevel.hpp>
#include <mitiru/terrain/Heightfield.hpp>

namespace mitiru::terrain
{

/// @brief 書き出すマス番号の範囲。両端を含み、既定では全体
struct CellRegion
{
	std::uint32_t ix0 = 0;
	std::uint32_t iz0 = 0;
	std::uint32_t ix1 = 0xFFFFFFFFu;
	std::uint32_t iz1 = 0xFFFFFFFFu;
};

/// @brief マス (ix, iz) の k 枚目の三角形の id。k は 0 が対角の手前、1 が奥。全体を書き出したときの番号
[[nodiscard]] inline std::uint32_t terrainTriangleId(const Heightfield& h, std::uint32_t ix, std::uint32_t iz,
                                                      std::uint32_t k) noexcept
{
	return (iz * (h.width() - 1) + ix) * 2 + k;
}

/// @brief 範囲内の頂点と添字を書く。3 つの添字で 1 枚を表し、上から見て反時計回り
inline void heightfieldTriangles(const Heightfield& h, const CellRegion& region, std::vector<Vec3>& vertices,
                                 std::vector<std::uint32_t>& indices)
{
	vertices.clear();
	indices.clear();
	if (!h.valid() || region.ix0 > h.width() - 2 || region.iz0 > h.depth() - 2) { return; }
	const std::uint32_t x0 = region.ix0;
	const std::uint32_t z0 = region.iz0;
	const std::uint32_t x1 = std::min(region.ix1, h.width() - 2);
	const std::uint32_t z1 = std::min(region.iz1, h.depth() - 2);
	if (x1 < x0 || z1 < z0) { return; }
	const std::uint32_t vw = x1 - x0 + 2;
	vertices.reserve(static_cast<std::size_t>(vw) * (z1 - z0 + 2));
	for (std::uint32_t iz = z0; iz <= z1 + 1; ++iz)
	{
		for (std::uint32_t ix = x0; ix <= x1 + 1; ++ix) { vertices.push_back(h.vertex(ix, iz)); }
	}
	indices.reserve(static_cast<std::size_t>(x1 - x0 + 1) * (z1 - z0 + 1) * 6);
	for (std::uint32_t iz = 0; iz <= z1 - z0; ++iz)
	{
		for (std::uint32_t ix = 0; ix <= x1 - x0; ++ix)
		{
			const std::uint32_t a = iz * vw + ix;
			const std::uint32_t b = a + 1;
			const std::uint32_t c = a + vw;
			const std::uint32_t d = c + 1;
			indices.insert(indices.end(), {a, c, b, b, c, d});
		}
	}
}

/// @brief 範囲内の三角形を動かない地形として追加する。全体を追加したときの id は terrainTriangleId と一致する
inline void addHeightfield(action::CollisionLevelBuilder& builder, const Heightfield& h, std::uint32_t layer,
                           const CellRegion& region = {})
{
	std::vector<Vec3> vertices;
	std::vector<std::uint32_t> indices;
	heightfieldTriangles(h, region, vertices, indices);
	builder.addTriangles(std::span<const Vec3>(vertices), std::span<const std::uint32_t>(indices), layer);
}

} // namespace mitiru::terrain
