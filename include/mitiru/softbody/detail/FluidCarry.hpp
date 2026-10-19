#pragma once

/// @file FluidCarry.hpp
/// @brief 芯の速度と向きの変化を粒へ伝える宣言。FluidBody.hpp から読む。

#include <algorithm>
#include <cmath>

namespace mitiru::softbody::detail
{

/// @brief 芯の速度の変化を面に沿う成分と外向きの成分に分けて粒へ足す
inline void fluidCarryVelocity(FluidBodyState& s, const FluidStepInput& in, bool landed) noexcept
{
	if (s.carryValid == 0 || (in.carry.along == 0.0f && in.carry.lift == 0.0f)) { return; }
	const Vec3 dv = in.coreVelocity - s.carryVelocity;
	const Vec3 up = normalizeOr(in.carry.surfaceUp);
	const Vec3 along = dv - up * dv.dot(up);
	Vec3 carried{};
	if (in.carry.along != 0.0f) { carried += along * in.carry.along; }
	if (!landed && in.carry.lift != 0.0f) { carried += (dv - along) * in.carry.lift; }
	for (int i = 0; i < s.count; ++i) { s.v[i] += carried; }
}

/// @brief 芯の向きの変化から、粒を pivot の周りへ動かす速度を作って足す
[[nodiscard]] inline bool fluidCarryTurn(FluidBodyState& s, const FluidStepInput& in, float tick,
                                         Vec3 (&turnV)[kFluidMaxParticles]) noexcept
{
	if (s.carryValid == 0 || in.carry.turn <= 0.0f) { return false; }
	const Vec3 a = normalizeOr(s.carryUp);
	const Vec3 b = normalizeOr(in.pose.up);
	const float angle = std::acos(std::clamp(a.dot(b), -1.0f, 1.0f)) * in.carry.turn;
	if (angle < 1e-6f) { return false; }
	Vec3 axis = a.cross(b);
	if (length(axis) < 1e-6f)
	{
		const Vec3 basis = std::fabs(a.x) < 0.9f ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{0.0f, 1.0f, 0.0f};
		axis = a.cross(basis);
	}
	axis = normalizeOr(axis);
	const float invTick = 1.0f / tick;
	for (int i = 0; i < s.count; ++i)
	{
		const Vec3 r = s.x[i] - in.carry.pivot;
		turnV[i] = (rotateAround(r, axis, angle) - r) * invTick;
		s.v[i] += turnV[i];
	}
	return true;
}

/// @brief 一時的に足した回転速度を粒の最終速度から除く
inline void fluidDropCarryTurn(FluidBodyState& s, const Vec3 (&turnV)[kFluidMaxParticles]) noexcept
{
	for (int i = 0; i < s.count; ++i) { s.v[i] -= turnV[i]; }
}

inline void fluidRememberCarry(FluidBodyState& s, const FluidStepInput& in) noexcept
{
	s.carryVelocity = in.coreVelocity;
	s.carryUp = normalizeOr(in.pose.up);
	s.carryValid = 1;
}

} // namespace mitiru::softbody::detail
