#pragma once

/// @file FluidSurface.hpp
/// @brief 流れる体の粒から滑らかな面の網を作る (CPU、毎フレーム)。
/// @details 面は Zhu と Bridson (2005) の距離。近くの粒の重み付き平均位置からの距離から半径を引く (粒の疎密で穴が開かない)。
///          その前に粒の位置を近くの平均へ寄せ (Yu と Turk 2013)、粒の凸凹を面に出さない。
///          距離は粗い格子 (meshVoxelSize の 2 倍) で作ってぼかし、面の近くの粗いセルだけを 2 × 2 × 2 に割って
///          Surface Nets で網にする。細かい格子点の値は粗い格子の 3 線形補間に手足の形 (細いので細かい格子で測る) を
///          滑らかに足したもので、位置だけで決まるので隣のセルから測っても同じビットになる。
///          法線は距離の勾配。前のフレームの位置は、体の所は「頂点 − 近くの粒の動きの重み付き平均」、手足の所は
///          前の手足の同じ所。網の並びは毎フレーム変わるので、前のフレームの位置は頂点ごとに持たせる (TAA と動きのぼけ用)。
///          確保は呼び手の配列 (FluidMesh・FluidSurfaceScratch) だけで、毎フレームの確保はしない。

#include <algorithm>
#include <cstdint>

#include <mitiru/softbody/FluidBody.hpp>

namespace mitiru::softbody
{

inline constexpr int kFluidMeshMaxVertices = 32768;
inline constexpr int kFluidMeshMaxIndices = 6 * kFluidMeshMaxVertices;
inline constexpr int kFluidFieldMaxNodes = 128 * 1024;
inline constexpr int kFluidFieldMaxDim = 160;
inline constexpr int kFluidMaxSlots = 16384;
inline constexpr int kFluidMaxSurfaceShapes = kFluidMaxLimbShapes + kFluidMaxShapes;

/// @brief 面の網。添字は 3 つで 1 つの三角形 (右手で回した向きが外)
struct FluidMesh
{
	int vertexCount = 0;
	int indexCount = 0;
	int truncated = 0;                ///< 上限で入りきらなかったら 1 (面に穴が開く)
	int rejected = 0;                 ///< 粒や形の位置が NaN か大きすぎて、網を作らなかったら 1
	Vec3 boundsMin{}, boundsMax{};
	Vec3 positions[kFluidMeshMaxVertices]{};
	Vec3 normals[kFluidMeshMaxVertices]{};
	Vec3 previousPositions[kFluidMeshMaxVertices]{};   ///< 前のフレームにその点があった位置
	std::uint32_t indices[kFluidMeshMaxIndices]{};
};

/// @brief 面に足す形 1 つ。box は距離を測る範囲 (その外では形は無いものとする)
struct FluidSurfaceShape
{
	FluidShape now{};
	FluidShape before{};
	std::uint8_t hasBefore = 0;
	std::uint8_t pad[3]{};
	Vec3 boxLo{}, boxHi{};
};

struct FluidSurfaceScratch
{
	Vec3 smoothed[kFluidMaxParticles]{};
	Vec3 motion[kFluidMaxParticles]{};
	FluidNeighborCache cache{};
	FluidNeighbors pairs{};
	int dim[3]{};
	Vec3 origin{};
	float voxel = 0.0f;               ///< 粗い格子の間隔
	float field[kFluidFieldMaxNodes][8]{};     ///< 重み、重み × 寄せた位置 (3)、重み × 粒の動き (3)
	float body[kFluidFieldMaxNodes]{};         ///< 粒の面の距離 (ぼかした後)
	float phi[kFluidFieldMaxNodes]{};          ///< 手足を足した距離 (粗い格子点)
	std::uint8_t nodeMark[kFluidFieldMaxNodes]{};   ///< 格子点の印 (fluidMarkNodes)
	float shapeNear[kFluidFieldMaxNodes]{};    ///< 手足の距離 (粗い格子点。手足から遠い所は 1000)
	float tmp[kFluidFieldMaxNodes]{};
	std::uint8_t rowActive[2][kFluidFieldMaxDim * kFluidFieldMaxDim]{};
	FluidSurfaceShape shapes[kFluidMaxSurfaceShapes]{};
	int shapeCount = 0;
	int cellSlot[kFluidFieldMaxNodes]{};       ///< 面の近くの粗いセルの番号 + 1 (0 は面から遠い)
	int slotCell[kFluidMaxSlots]{};
	int slotVertex[kFluidMaxSlots][8]{};
	float slotPhi[kFluidMaxSlots][27]{};
	std::uint32_t slotShapes[kFluidMaxSlots]{};   ///< セルの値を変えうる手足の印 (ビット i が形 i)
	int slotXyz[kFluidMaxSlots][3]{};
	int slotCount = 0;
	int slabCount[kFluidFieldMaxDim]{};          ///< z の層ごとの面の近くのセルの数
	int slabFirst[kFluidFieldMaxDim]{};
	std::uint32_t slotNeg[kFluidMaxSlots]{};      ///< 27 の細かい格子点のうち内のもの
	std::uint8_t slotMixed[kFluidMaxSlots]{};     ///< 面をまたぐ細かいセル
	int slotFirst[kFluidMaxSlots]{};              ///< 最初の頂点の番号
	int slotQuadCount[kFluidMaxSlots]{};
	int slotFirstIndex[kFluidMaxSlots]{};
	const FluidParallel* parallel = nullptr;   ///< 頂点の、粗いセルの中の位置 (0〜1)
};

} // namespace mitiru::softbody

#include <mitiru/softbody/detail/FluidGuard.hpp>
#include <mitiru/softbody/detail/FluidField.hpp>
#include <mitiru/softbody/detail/FluidSurfaceNets.hpp>

namespace mitiru::softbody
{

/// @brief state の粒と手足から面の網を out に作る。shapes はゲームが面にだけ足す形 (ワールド、無くてよい)。
///        parallel があれば格子のセルを分けて回す (結果のビットは同じ)
inline void buildFluidMesh(const FluidBodyState& state, const FluidParams& k, const FluidShape* shapes, int shapeCount,
                           FluidSurfaceScratch& w, FluidMesh& out, const FluidParallel* parallel = nullptr) noexcept
{
	out.vertexCount = 0;
	out.indexCount = 0;
	out.truncated = 0;
	out.rejected = 0;
	if (state.count == 0) { return; }
	if (!detail::fluidInputOk(state, shapes, shapeCount))
	{
		out.rejected = 1;
		detail::fluidDropField(w);
		return;
	}
	w.parallel = parallel;
	detail::fluidSmoothPositions(state, k, w);
	detail::fluidGatherShapes(state, shapes, shapeCount, w);
	detail::fluidPlaceField(state.count, k, w);
	if (!detail::fluidGridOk(w))
	{
		out.rejected = 1;
		detail::fluidDropField(w);
		return;
	}
	detail::fluidSplat(state.count, k, w);
	detail::fluidDistance(k, w);
	detail::fluidSurfaceNets(k, w, out);
}

/// @brief 直前の buildFluidMesh の面の距離 (外が正、粗い格子の 3 線形補間)。格子の外は 0.3
[[nodiscard]] inline float fluidSurfaceDistance(const FluidSurfaceScratch& w, const Vec3& p) noexcept
{
	const Vec3 g = (p - w.origin) / w.voxel;
	const float lim[3] = {static_cast<float>(w.dim[0] - 1), static_cast<float>(w.dim[1] - 1), static_cast<float>(w.dim[2] - 1)};
	if (!(g.x >= 0.0f && g.y >= 0.0f && g.z >= 0.0f && g.x <= lim[0] && g.y <= lim[1] && g.z <= lim[2])) { return 0.3f; }
	const int cell[3] = {std::min(static_cast<int>(g.x), w.dim[0] - 2), std::min(static_cast<int>(g.y), w.dim[1] - 2),
	                     std::min(static_cast<int>(g.z), w.dim[2] - 2)};
	float c[8];
	detail::fluidCorners(w, w.phi, 1, detail::fluidNode(w, cell[0], cell[1], cell[2]), c);
	return detail::fluidCornerLerp(c, g - Vec3{static_cast<float>(cell[0]), static_cast<float>(cell[1]), static_cast<float>(cell[2])});
}

/// @brief from から dir (単位) へ進み、面を内から外へ出る点を返す (目や口を面に貼るとき)。maxDist までに出なければ maxDist 先の点
[[nodiscard]] inline Vec3 fluidSurfaceAlong(const FluidSurfaceScratch& w, const Vec3& from, const Vec3& dir, float maxDist) noexcept
{
	const float stepLength = 0.25f * w.voxel;
	float prev = fluidSurfaceDistance(w, from);
	for (float d = stepLength; d <= maxDist; d += stepLength)
	{
		const float now = fluidSurfaceDistance(w, from + dir * d);
		if (prev < 0.0f && now >= 0.0f) { return from + dir * (d - stepLength * now / (now - prev)); }
		prev = now;
	}
	return from + dir * maxDist;
}

} // namespace mitiru::softbody
