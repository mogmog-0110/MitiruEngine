#pragma once

/// @file CameraWallLimit.hpp
/// @brief 面の法線から、目を面の外側に置ける yaw と pitch の範囲を求める純関数。
/// @details 目は注視点から back 方向にあるため、面から minDeg 以上離す条件は dot(back, n) >= sin(minDeg) となる。1 枚の面だけを扱い、角では呼ぶ側が法線を混ぜる。リグ座標系は y が上、z が北で、yaw は +z から x へ向かう。上方向を指定しないリグではワールド座標系と同じ。CameraRigInput::limitNormal は updateCameraRig が変換する。

#include <cmath>

#include <mitiru/action/ActionMath.hpp>

namespace mitiru::action
{

/// @brief center から左右に half までの yaw の弧。
struct YawArc
{
	float center = 0.0f;
	float half   = kPi;     ///< kPi で全周
	bool  empty  = false;   ///< その pitch では、どの yaw でも面から minDeg 離れられない。このとき half は 0 (center が最善)
};

/// @brief pitch の範囲。単位は rad で、正は見下ろす向き。
struct PitchRange
{
	float lo = -0.5f * kPi;
	float hi = 0.5f * kPi;
};

namespace wall_limit_detail
{

constexpr float kTinyHorizontal = 1e-6f;

[[nodiscard]] inline bool usable(const Vec3& n) noexcept
{
	const float l2 = lengthSq(n);
	return std::isfinite(l2) && l2 > 1e-12f;
}

[[nodiscard]] inline float minRad(float minDeg) noexcept
{
	return clampf(std::isfinite(minDeg) ? minDeg : 0.0f, 0.0f, 90.0f) * kDegToRad;
}

} // namespace wall_limit_detail

/// @brief 面から minDeg 以上離れて目を置ける pitch の範囲。normal が 0 なら制限しない。
/// @details どの yaw でも、範囲外では面から離れられない。天井では上向き側、床では見下ろす側に寄る。
[[nodiscard]] inline PitchRange allowedPitchRange(const Vec3& normal, float minDeg) noexcept
{
	namespace wd = wall_limit_detail;
	PitchRange r;
	if (!wd::usable(normal)) { return r; }
	const Vec3  n    = normalizeOr(normal, Vec3{0.0f, 1.0f, 0.0f});
	const float tilt = std::atan2(n.y, std::sqrt(n.x * n.x + n.z * n.z));  // 法線の仰角
	const float span = 0.5f * kPi - wd::minRad(minDeg);
	r.lo = maxf(r.lo, tilt - span);
	r.hi = minf(r.hi, tilt + span);
	return r;
}

/// @brief pitch を決めたとき、面から minDeg 以上離れて目を置ける yaw の弧。
/// @details pitch が allowedPitchRange の外なら empty になる。normal が 0 なら全周になる。
[[nodiscard]] inline YawArc allowedYawArc(const Vec3& normal, float pitch, float minDeg) noexcept
{
	namespace wd = wall_limit_detail;
	if (!wd::usable(normal)) { return {}; }
	const Vec3  n = normalizeOr(normal, Vec3{0.0f, 1.0f, 0.0f});
	const float h = std::sqrt(n.x * n.x + n.z * n.z);
	const float s = std::sin(wd::minRad(minDeg));
	const float num = s - n.y * std::sin(pitch);
	const float den = h * std::cos(pitch);
	YawArc arc;
	if (den < wd::kTinyHorizontal)
	{
		// 水平成分が無いため、yaw を変えても結果は変わらない。
		arc.half  = num <= 0.0f ? kPi : 0.0f;
		arc.empty = num > 0.0f;
		return arc;
	}
	const float q = num / den;
	arc.center    = std::atan2(-n.x, -n.z);
	arc.half      = std::acos(clampf(q, -1.0f, 1.0f));
	arc.empty     = q > 1.0f;
	return arc;
}

} // namespace mitiru::action
