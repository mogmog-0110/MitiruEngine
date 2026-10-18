#pragma once

/// @file FluidLimbs.hpp
/// @brief 歩くときだけ体から生える手足 (FluidBody.hpp から読む)。
/// @details 手足は粒を持たず、面の距離に滑らかに足す形 (FluidShape) にする。生えた量はばねで動かし、
///          生えるときは 1 回伸びすぎてから落ち着き、引っ込むときは行き過ぎずに溶ける。
///          根元は器ではなく粒の塊に付けるので、塊が器より遅れても手足が宙に浮かない。

#include <cmath>

namespace mitiru::softbody::detail
{

/// @brief 生やす量の目標。床の上を歩いている間だけ 1 で、最高速では足をたたむ (tuckShare まで縮める)
[[nodiscard]] inline float fluidLimbTarget(const FluidBodyState& s, const FluidParams& k, float speed) noexcept
{
	const bool puddle = s.t - s.landT < k.landContainerDelay + k.limbPuddleTime;
	const bool braking = s.t - s.brakeT < k.brakeHold + k.limbBrakeHold;
	const bool walking = s.mode == FluidMode::Ground && speed > k.limbSpeedMin && !fluidAnyPod(s) && !puddle && !braking;
	if (!walking) { return 0.0f; }
	if (k.dashWheel != 0) { return 1.0f; }
	const float u = clamp01((speed - k.tuckSpeed) / k.tuckBlendSpeed);
	return 1.0f - (1.0f - k.tuckShare) * u;
}

/// @brief 生えた量をばねで目標へ動かす
inline void fluidLimbSpring(FluidBodyState& s, const FluidParams& k, float target, float dt) noexcept
{
	const bool retract = target < s.limbGrow;
	const float w = 2.0f * kFluidPi * (retract ? k.limbRetractHz : k.limbGrowHz);
	const float zeta = retract ? k.limbRetractZeta : k.limbGrowZeta;
	const float a = w * w * (target - s.limbGrow) - 2.0f * zeta * w * s.limbGrowVelocity;
	s.limbGrowVelocity += a * dt;
	s.limbGrow = std::fmax(0.0f, s.limbGrow + s.limbGrowVelocity * dt);
	if (s.limbGrow == 0.0f && s.limbGrowVelocity < 0.0f) { s.limbGrowVelocity = 0.0f; }
}

inline void fluidPushLimb(FluidBodyState& s, const FluidShape& shape) noexcept
{
	if (s.limbCount < kFluidMaxLimbShapes) { s.limbs[s.limbCount++] = shape; }
}

[[nodiscard]] inline FluidShape fluidCapsule(const Vec3& a, const Vec3& b, float ra, float rb) noexcept
{
	FluidShape c;
	c.kind = FluidShape::Kind::Capsule;
	c.a = a;
	c.b = b;
	c.ra = ra;
	c.rb = rb;
	return c;
}

/// @brief 輪に回す足の、位相 th での足先 (足元の座標系)
[[nodiscard]] inline Vec3 fluidWheelFoot(const FluidParams& k, float x, float g, float th) noexcept
{
	return {x, k.footHalfY * g + k.wheelRadius * (1.0f - std::cos(th)) * 0.6f, k.wheelRadius * std::sin(th)};
}

/// @brief 脚の筒と足先の楕円体。輪のときは前のコマまでの弧を 3 本の短い筒でなぞる
inline void fluidLegShapes(FluidBodyState& s, const FluidParams& k, const FluidFrame& f, const FluidPose& pose, int side,
                           const Vec3& offset, float phase, float cadence, bool wheel) noexcept
{
	const float g = s.limbGrow;
	const float sign = side == 0 ? -1.0f : 1.0f;
	const FluidFoot& foot = pose.feet[side];
	const float fx = sign * k.legSpacing * 0.5f;
	const float th = kFluidPi * (phase + static_cast<float>(side));
	const Vec3 ground = wheel ? fluidWheelFoot(k, fx, g, th) : Vec3{fx, k.footHalfY * g + foot.y, foot.z * k.strideShare};
	const Vec3 footW = f.groundRoot + toWorld(f.ground, ground);
	const Vec3 rootW = f.bodyRoot + toWorld(f.body, Vec3{sign * k.legRootX, k.bodyCenterY - k.bodyHalf.y + k.legRootUp, 0.0f}) + offset;
	const float legR = k.legRadius * std::fmin(g, 1.2f);
	fluidPushLimb(s, fluidCapsule(rootW, footW, legR * 1.25f, legR));
	FluidShape toe;
	toe.kind = FluidShape::Kind::Ellipsoid;
	toe.axes = rotateBasis(f.ground, f.ground.c0, -foot.toeDeg * k.toeShare * kFluidDegToRad);
	toe.half = Vec3{k.footHalfX, k.footHalfY, k.footHalfZ} * std::fmin(g, 1.25f);
	toe.a = footW + f.ground.c2 * (k.footForward * g);
	fluidPushLimb(s, toe);
	if (!wheel) { return; }
	const float smear = kFluidPi * cadence / k.smearFps;
	for (int j = 0; j < 3; ++j)
	{
		const Vec3 pa = fluidWheelFoot(k, fx, g, th - smear * static_cast<float>(j) / 3.0f);
		const Vec3 pb = fluidWheelFoot(k, fx, g, th - smear * static_cast<float>(j + 1) / 3.0f);
		const float r = toe.half.y * (1.0f - 0.25f * static_cast<float>(j + 1));
		fluidPushLimb(s, fluidCapsule(f.groundRoot + toWorld(f.ground, pa), f.groundRoot + toWorld(f.ground, pb), r, r));
	}
}

/// @brief 腕の筒。体の横から、上げと前後の振りの向きへ伸ばし、歩きの位相でばたつかせる
inline void fluidArmShape(FluidBodyState& s, const FluidParams& k, const FluidFrame& f, const FluidPose& pose, int side,
                          const Vec3& offset, float phase) noexcept
{
	const float g = s.limbGrow;
	const float sign = side == 0 ? -1.0f : 1.0f;
	const float raise = pose.arms[side].raiseDeg + k.armFlail * std::sin(kFluidPi * phase * 2.0f + static_cast<float>(side) * 1.3f) * g;
	const float r = (raise + k.armRaiseExtra) * kFluidDegToRad, sw = pose.arms[side].swingDeg * kFluidDegToRad;
	const Vec3 dir{sign * std::cos(r) * std::cos(sw), std::sin(r), std::cos(r) * std::sin(sw)};
	const Vec3 root = f.bodyRoot + toWorld(f.body, Vec3{sign * (f.halfX - k.armInset), k.bodyCenterY - k.bodyHalf.y + k.armRootUp, 0.0f}) + offset;
	const Vec3 tip = root + toWorld(f.body, dir) * (k.armLength * g);
	fluidPushLimb(s, fluidCapsule(root, tip, k.armRootRadius * std::fmin(g, 1.2f), k.armTipRadius * std::fmin(g, 1.2f)));
}

/// @brief 手足の形を作り直す (ワールド)。offset は器の中心から粒の重心へのずれ
inline void fluidBuildLimbs(FluidBodyState& s, const FluidParams& k, const FluidFrame& f, const FluidPose& pose, float phase,
                            float cadence, float speed, const Vec3& offset) noexcept
{
	s.limbCount = 0;
	if (s.limbGrow < 0.02f) { return; }
	const bool wheel = k.dashWheel != 0 && speed > k.wheelSpeed;
	for (int side = 0; side < 2; ++side)
	{
		fluidLegShapes(s, k, f, pose, side, offset, phase, cadence, wheel);
		fluidArmShape(s, k, f, pose, side, offset, phase);
	}
}

} // namespace mitiru::softbody::detail
