#pragma once

/// @file CameraRigLimit_impl.hpp
/// @brief updateCameraRig で視線を面の外側の範囲へ収める実装。CameraRig.hpp から読む。

#include <cmath>

#include <mitiru/action/CameraWallLimit.hpp>

namespace mitiru::action::camera_rig_detail
{

/// @brief 範囲から出た量（rad）。範囲内なら 0。
[[nodiscard]] inline float pitchExcess(float pitch, const PitchRange& r) noexcept
{
	return pitch > r.hi ? pitch - r.hi : (pitch < r.lo ? r.lo - pitch : 0.0f);
}

/// @brief yaw が弧から出た量（rad）。side は出た側（+1 か -1）。
[[nodiscard]] inline float yawExcess(const YawArc& arc, float yaw, float& side) noexcept
{
	const float off = wrapAngle(yaw - arc.center);
	side            = off >= 0.0f ? 1.0f : -1.0f;
	return maxf(0.0f, std::fabs(off) - arc.half);
}

/// @brief 外へ出てよい量。前のフレームの超過から step ずつ減らし、法線が現れたときに視線が 1 フレームで急に変わるのを防ぐ。
[[nodiscard]] inline float allowedExcess(float before, float step, bool eased) noexcept
{
	return eased ? maxf(0.0f, before - step) : 0.0f;
}

/// @brief yaw と pitch を、面 limitNormal から limitMinDeg 以上離れて目を置ける範囲へ収める。
/// @details orbit のあと、carryArm の前に呼ぶ。スティックの入力とゲームが直接書いた値を収め、腕を同じ量だけ運ぶ。
/// pitch を先に収め、その pitch での yaw の弧へ収める。minPitchDeg と maxPitchDeg より面の範囲を優先する。
inline void limitToSurface(CameraRigState& s, const CameraRigConfig& c, const CameraRigInput& in, float yawBefore,
                           float pitchBefore, float dt) noexcept
{
	if (!wall_limit_detail::usable(in.limitNormal)) { return; }
	const Vec3  n    = cameraRigToLocal(s, normalizeOr(in.limitNormal, Vec3{0.0f, 1.0f, 0.0f}));
	const bool  eased = c.limitMaxTurnDeg > 0.0f;   // dt が 0 のフレームは外れ量を減らさず、そのまま保つ
	const float step  = eased ? c.limitMaxTurnDeg * kDegToRad * dt : 0.0f;

	const PitchRange range = allowedPitchRange(n, in.limitMinDeg);
	const float      pLeft = allowedExcess(pitchExcess(pitchBefore, range), step, eased);
	if (pitchExcess(s.pitch, range) > pLeft) { s.pitch = s.pitch > range.hi ? range.hi + pLeft : range.lo - pLeft; }

	float       sideBefore = 1.0f;
	float       side       = 1.0f;
	const float yLeft      = allowedExcess(yawExcess(allowedYawArc(n, pitchBefore, in.limitMinDeg), yawBefore, sideBefore), step, eased);
	const YawArc arc       = allowedYawArc(n, s.pitch, in.limitMinDeg);
	if (yawExcess(arc, s.yaw, side) > yLeft) { s.yaw = wrapAngle(arc.center + side * (arc.half + yLeft)); }
}

} // namespace mitiru::action::camera_rig_detail
