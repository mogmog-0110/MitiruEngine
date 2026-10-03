#pragma once

/// @file ClothMesh.hpp
/// @brief 格子の布の頂点位置から、両面を描く Vertex3D と添字を作る (registerMesh3D に毎フレーム渡す)。
/// @details 格子は columns × rows、添字は row * columns + column、row 0 が上。表の法線は
///          「下の行へ向かう差 × 右の列へ向かう差」の向き。裏は同じ位置に逆の法線と逆回りの三角形を置く。
///          確保は呼び手の配列だけで、毎フレームの確保はしない。

#include <cstdint>
#include <span>

#include <sgc/math/Vec2.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/Vertex3D.hpp>

namespace mitiru::physics3d
{

[[nodiscard]] constexpr int clothMeshVertexCount(int columns, int rows) noexcept { return 2 * columns * rows; }
[[nodiscard]] constexpr int clothMeshIndexCount(int columns, int rows) noexcept { return 12 * (columns - 1) * (rows - 1); }

/// @brief 添字を書く (形が変わらないので 1 回でよい)
inline void buildClothIndices(int columns, int rows, std::span<std::uint32_t> out) noexcept
{
	if (static_cast<int>(out.size()) < clothMeshIndexCount(columns, rows)) { return; }
	const auto back = static_cast<std::uint32_t>(columns * rows);
	std::size_t k = 0;
	for (int y = 0; y + 1 < rows; ++y)
	{
		for (int x = 0; x + 1 < columns; ++x)
		{
			const auto i00 = static_cast<std::uint32_t>(y * columns + x);
			const auto i10 = i00 + 1;
			const auto i01 = i00 + static_cast<std::uint32_t>(columns);
			const auto i11 = i01 + 1;
			const std::uint32_t front[6] = {i00, i01, i10, i10, i01, i11};
			for (std::uint32_t v : front) { out[k++] = v; }
			const std::uint32_t rear[6] = {back + i00, back + i10, back + i01, back + i10, back + i11, back + i01};
			for (std::uint32_t v : rear) { out[k++] = v; }
		}
	}
}

/// @brief 頂点を書く。positions は columns × rows 個のワールド座標
inline void buildClothVertices(int columns, int rows, std::span<const sgc::Vec3f> positions, const sgc::Colorf& color,
                               std::span<render::Vertex3D> out) noexcept
{
	const int n = columns * rows;
	if (static_cast<int>(positions.size()) < n || static_cast<int>(out.size()) < 2 * n) { return; }
	const auto at = [&](int x, int y) { return positions[static_cast<std::size_t>(y * columns + x)]; };
	for (int y = 0; y < rows; ++y)
	{
		for (int x = 0; x < columns; ++x)
		{
			const sgc::Vec3f dx = at(x + 1 < columns ? x + 1 : x, y) - at(x > 0 ? x - 1 : x, y);
			const sgc::Vec3f dy = at(x, y + 1 < rows ? y + 1 : y) - at(x, y > 0 ? y - 1 : y);
			sgc::Vec3f nrm = dy.cross(dx);
			const float len = nrm.length();
			nrm = len > 1e-12f ? nrm / len : sgc::Vec3f{0, 0, 1};
			const auto i = static_cast<std::size_t>(y * columns + x);
			const sgc::Vec2f uv{static_cast<float>(x) / static_cast<float>(columns - 1),
			                    static_cast<float>(y) / static_cast<float>(rows - 1)};
			out[i] = render::Vertex3D(positions[i], nrm, uv, color);
			out[i + static_cast<std::size_t>(n)] = render::Vertex3D(positions[i], -nrm, uv, color);
		}
	}
}

} // namespace mitiru::physics3d
