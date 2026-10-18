#pragma once

/// @file FluidGuard.hpp
/// @brief 面を作る前に、入力と格子が配列の外を指さないか確かめる (FluidSurface.hpp から読む)。

#include <algorithm>
#include <cmath>

namespace mitiru::softbody::detail
{

/// 座標の絶対値の上限。格子の添字が int に収まる大きさで、遊びの世界には十分に広い
inline constexpr float kFluidCoordLimit = 1.0e5f;

/// 比較が偽になる NaN も弾くため、範囲に入っていることを肯定の形で書く
[[nodiscard]] inline bool fluidCoordOk(float v) noexcept { return std::fabs(v) <= kFluidCoordLimit; }

[[nodiscard]] inline bool fluidCoordOk(const Vec3& p) noexcept { return fluidCoordOk(p.x) && fluidCoordOk(p.y) && fluidCoordOk(p.z); }

[[nodiscard]] inline bool fluidShapeOk(const FluidShape& s) noexcept
{
	return fluidCoordOk(s.a) && fluidCoordOk(s.b) && fluidCoordOk(s.half) && fluidCoordOk(s.ra) && fluidCoordOk(s.rb);
}

[[nodiscard]] inline bool fluidInputOk(const FluidBodyState& s, const FluidShape* extra, int extraCount) noexcept
{
	if (s.count < 0 || s.count > kFluidMaxParticles || s.limbCount < 0 || s.limbCount > kFluidMaxLimbShapes) { return false; }
	if (extraCount < 0 || (extraCount > 0 && extra == nullptr)) { return false; }
	for (int i = 0; i < s.count; ++i)
	{
		if (!fluidCoordOk(s.x[i]) || !fluidCoordOk(s.xStepStart[i])) { return false; }
	}
	for (int i = 0; i < s.limbCount; ++i)
	{
		if (!fluidShapeOk(s.limbs[i])) { return false; }
	}
	const int usedExtra = std::min(extraCount, kFluidMaxSurfaceShapes);
	for (int i = 0; i < usedExtra; ++i)
	{
		if (!fluidShapeOk(extra[i])) { return false; }
	}
	return true;
}

/// 格子が持てる大きさに収まっているか。収まらないまま使うと fluidSplatSlab などが配列の外へ書く
[[nodiscard]] inline bool fluidGridOk(const FluidSurfaceScratch& w) noexcept
{
	if (!std::isfinite(w.voxel) || !(w.voxel > 0.0f) || !fluidCoordOk(w.origin)) { return false; }
	long long nodes = 1;
	for (const int d : w.dim)
	{
		if (d < 2 || d > kFluidFieldMaxDim) { return false; }
		nodes *= d;
	}
	return nodes <= kFluidFieldMaxNodes;
}

/// 面の距離の問い合わせ (fluidSurfaceDistance) が、どこも「外」と答える空の格子にする
inline void fluidDropField(FluidSurfaceScratch& w) noexcept
{
	w.dim[0] = w.dim[1] = w.dim[2] = 0;
	w.origin = Vec3{};
	w.voxel = 1.0f;
	w.shapeCount = 0;
	w.slotCount = 0;
}

} // namespace mitiru::softbody::detail
