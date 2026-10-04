#pragma once

/// @file SoftLock.hpp
/// @brief 攻撃の出だしで、前の円錐の中からいちばん狙いやすい相手を選び、そちらへ向き直る (ソフトロック)。
/// @details ロックオンのように相手を覚えず、振るたびに選び直す。向きと距離は上向き (既定は +Y) に垂直な面で測り、
///          高さの差は見ない。点数は「距離 / 射程 + angleWeight × 角度 / 半角」で、小さい方を選ぶ。
///          点数が同じなら添字の小さい方を選ぶので、同じ入力からは同じ相手が選ばれる。

#include <cmath>

#include <mitiru/action/ActionMath.hpp>

namespace mitiru::action
{

struct SoftLockParams
{
	float range        = 5.0f;   ///< 届く距離 (m)。これより遠い相手は選ばない
	float halfAngleDeg = 60.0f;  ///< 前の円錐の半角。これより横の相手は選ばない
	float angleWeight  = 1.0f;   ///< 距離に比べて、向きのずれをどれだけ重く見るか
	float maxTurnDeg   = 180.0f; ///< 1 回で向き直る角度の上限
	Vec3  up{0.0f, 1.0f, 0.0f};
};

struct SoftLockResult
{
	int   target = -1;   ///< 選んだ相手の添字。円錐の中に誰もいなければ -1 (向きはそのまま)
	Vec3  forward{};     ///< 向き直った後の前 (up に垂直な単位ベクトル)
	float yaw = 0.0f;    ///< forward の yaw (+Z が 0、+X が π/2。CameraRig と同じ)
};

namespace detail
{
	/// @brief up に垂直な面へ落として正規化する。落とすと長さが無いときは fallback
	[[nodiscard]] inline Vec3 flatten(const Vec3& v, const Vec3& up, const Vec3& fallback) noexcept
	{
		return normalizeOr(projectOnPlane(v, up), fallback);
	}

	/// @brief 単位ベクトル a から b までの角度 (0..π)
	[[nodiscard]] inline float angleBetween(const Vec3& a, const Vec3& b) noexcept
	{
		return std::acos(clampf(dot(a, b), -1.0f, 1.0f));
	}

	/// @brief from を up まわりに to の方へ、最大 maxRad だけ回す (どちらも up に垂直な単位ベクトル)
	[[nodiscard]] inline Vec3 rotateToward(const Vec3& from, const Vec3& to, const Vec3& up, float maxRad) noexcept
	{
		const float angle = angleBetween(from, to);
		if (angle <= maxRad) { return to; }
		const float sign = dot(cross(from, to), up) >= 0.0f ? 1.0f : -1.0f;
		const Vec3 side = cross(up, from) * sign;   // from から to の側へ 90 度
		return normalizeOr(from * std::cos(maxRad) + side * std::sin(maxRad), from);
	}
}  // namespace detail

/// @brief origin から forward を向いた攻撃者にとって、targets[0..count) のどれを狙うかと、向き直った後の前
/// @param forward 攻撃者の今の前。up の成分は無視する
[[nodiscard]] inline SoftLockResult softLock(const Vec3& origin, const Vec3& forward, const Vec3* targets, int count,
                                             const SoftLockParams& p = {}) noexcept
{
	const Vec3 up = normalizeOr(p.up, Vec3{0.0f, 1.0f, 0.0f});
	const Vec3 front = detail::flatten(forward, up, anyPerpendicular(up));
	const float halfAngle = p.halfAngleDeg * kDegToRad;
	SoftLockResult r{-1, front, std::atan2(front.x, front.z)};
	float best = 0.0f;
	for (int i = 0; i < count && targets != nullptr; ++i)
	{
		const Vec3 toTarget = projectOnPlane(targets[i] - origin, up);
		const float dist = length(toTarget);
		if (dist > p.range) { continue; }
		const float angle = dist > 1e-4f ? detail::angleBetween(front, toTarget * (1.0f / dist)) : 0.0f;
		if (angle > halfAngle) { continue; }
		const float score = dist / maxf(p.range, 1e-4f) + p.angleWeight * angle / maxf(halfAngle, 1e-4f);
		if (r.target < 0 || score < best) { best = score; r.target = i; }
	}
	if (r.target < 0) { return r; }
	const Vec3 want = detail::flatten(targets[r.target] - origin, up, front);
	r.forward = detail::rotateToward(front, want, up, p.maxTurnDeg * kDegToRad);
	r.yaw = std::atan2(r.forward.x, r.forward.z);
	return r;
}

}  // namespace mitiru::action
