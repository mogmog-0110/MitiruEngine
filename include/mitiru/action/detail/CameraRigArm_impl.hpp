#pragma once

/// @file CameraRigArm_impl.hpp
/// @brief updateCameraRig の腕 (向きのばね、見通しの探索、壁での伸び縮み)。CameraRig_impl.hpp から読む。

#include <cmath>

namespace mitiru::action::camera_rig_detail
{

/// @brief リグの右・画面の上・後ろ (注視点から目への向き)。右は yaw から作るので、上と視線が重なっても決まる
struct ArmBasis
{
	Vec3 right;
	Vec3 up;
	Vec3 back;
};

/// @brief 今のリグの上と北の座標系で、yaw と pitch から作る基底
[[nodiscard]] inline ArmBasis armBasis(const CameraRigState& s, float yaw, float pitch) noexcept
{
	const Vec3 f = cameraRigToWorld(s, cameraRigForward(yaw, pitch));
	const Vec3 r = cameraRigToWorld(s, Vec3{-std::cos(yaw), 0.0f, std::sin(yaw)});
	return {r, cross(r, f), -f};
}

[[nodiscard]] inline ArmBasis armBasis(const CameraRigState& s) noexcept { return armBasis(s, s.yaw, s.pitch); }

[[nodiscard]] inline Vec3 changeBasis(const ArmBasis& from, const ArmBasis& to, const Vec3& v) noexcept
{
	return to.right * dot(v, from.right) + to.up * dot(v, from.up) + to.back * dot(v, from.back);
}

/// @brief 前のフレームから yaw と pitch が変わった分 (スティック、ロックオン、ゲームが直に書いた分) は、
///        ばねを通さずに腕ごと運ぶ。操作に遅れを足さない。上が回った分は運ばず、ばねがならす
inline void carryArm(CameraRigState& s) noexcept
{
	const ArmBasis before = armBasis(s, s.armYaw, s.armPitch);
	const ArmBasis after  = armBasis(s);
	s.armDir   = normalizeOr(changeBasis(before, after, s.armDir), after.back);
	s.armSpin  = changeBasis(before, after, s.armSpin);
	s.armYaw   = s.yaw;
	s.armPitch = s.pitch;
}

/// @brief 重なっていれば押し出す (最大 2 回)
[[nodiscard]] inline Vec3 depenetrate(const CollisionWorld& world, Vec3 p, float r, std::uint32_t mask) noexcept
{
	for (int it = 0; it < 2; ++it)
	{
		Contact   cs[4];
		const int n = world.overlapSphere(p, r, cs, 4, mask);
		if (n <= 0) { break; }
		p += cs[0].normal * (cs[0].depth + 1e-3f);
	}
	return p;
}

/// @brief 腕は縮むときはすぐ、伸びるときは recoverDelay 待ってから指数で伸ばす (速さの上限つき)
inline void updateArm(CameraRigState& s, const CameraRigConfig& c, float allowed, float dt) noexcept
{
	if (allowed < s.arm)
	{
		s.arm       = allowed;
		s.clearTime = 0.0f;
		return;
	}
	if (allowed <= s.arm) { return; }
	s.clearTime = minf(s.clearTime + dt, 1000.0f);
	if (s.clearTime < c.recoverDelay) { return; }
	float next = s.arm + (allowed - s.arm) * expDecay(c.recoverRate, dt);
	next       = minf(next, s.arm + maxf(0.0f, c.recoverMaxSpeed) * dt);
	s.arm      = allowed - next < 1e-4f ? allowed : next;
}

/// @brief origin から dir へ目の球を伸ばせる長さ (m)
[[nodiscard]] inline float room(const CameraRigConfig& c, const CollisionWorld& world, const Vec3& origin,
                                const Vec3& dir, float length) noexcept
{
	const SweepHit h = world.sphereCast(origin, maxf(0.0f, c.probeRadius), dir * length, c.collisionMask);
	return h.hit ? clampf(h.fraction * length - maxf(0.0f, c.collisionMargin), 0.0f, length) : length;
}

/// @brief 腕の目標の向き。望む向き want が塞がれていれば、体の上 lift の側へ段に分けて回し、最初に見通しのある向き
[[nodiscard]] inline Vec3 armAim(const CameraRigConfig& c, const CollisionWorld& world, const Vec3& origin,
                                 const ArmBasis& b, const Vec3& lift, float length) noexcept
{
	const float enough = length * clampf(c.blockedShare, 0.0f, 1.0f);
	if (c.searchSteps == 0 || room(c, world, origin, b.back, length) >= enough) { return b.back; }
	const Vec3  axis  = normalizeOr(cross(b.back, lift), b.right);  // 真下を見ているときは右まわりに起こす
	const float angle = std::acos(clampf(dot(b.back, lift), -1.0f, 1.0f));
	for (std::uint32_t k = 1; k <= c.searchSteps; ++k)
	{
		const Vec3 dir = rotateAbout(b.back, axis, angle * static_cast<float>(k) / static_cast<float>(c.searchSteps));
		if (room(c, world, origin, dir, length) >= enough) { return dir; }
	}
	return b.back;
}

/// @brief 腕の向きを aim へ臨界減衰のばね (Game Programming Gems 4 の SmoothDamp を回転に当てたもの) で回す
inline void turnArm(CameraRigState& s, const CameraRigConfig& c, const Vec3& aim, float dt) noexcept
{
	if (c.armTurnTime <= 0.0f)
	{
		s.armDir  = aim;
		s.armSpin = Vec3{};
		return;
	}
	const float angle = std::acos(clampf(dot(s.armDir, aim), -1.0f, 1.0f));
	const Vec3  error = normalizeOr(cross(s.armDir, aim), anyPerpendicular(s.armDir)) * angle;
	const float omega = 2.0f / c.armTurnTime;
	const float x     = omega * dt;
	const float decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
	const Vec3  temp  = (s.armSpin - error * omega) * dt;
	const float limit = maxf(0.0f, c.armMaxTurnDeg) * kDegToRad;
	const Vec3  step  = clampLength(projectOnPlane(error + (temp - error) * decay, s.armDir), limit * dt);
	s.armSpin = clampLength(projectOnPlane((s.armSpin - temp * omega) * decay, s.armDir), limit);
	const float turn = length(step);
	if (turn > 1e-7f) { s.armDir = normalizeOr(rotateAbout(s.armDir, step * (1.0f / turn), turn), aim); }
}

/// @brief placeEye に渡す、腕を伸ばしたい形
struct EyeGoal
{
	Vec3  follow;  ///< キャラの足元
	Vec3  lift;    ///< 体の上 (ならす前)
	Vec3  focus;   ///< 注視点 (腕の根元)
	float length;  ///< 伸ばしたい腕の長さ
};

/// @brief 体の中から根元へ球を掃引して腕の根元を決め、腕の向きを回して壁の手前で止めた目の位置を返す
/// @return 腕の根元 (体から見通せる注視点)。目は根元 + armDir * arm
[[nodiscard]] inline Vec3 placeArm(CameraRigState& s, const CameraRigConfig& c, const CollisionWorld& world,
                                   const EyeGoal& g, float dt) noexcept
{
	const float r      = maxf(0.0f, c.probeRadius);
	const Vec3  anchor = depenetrate(world, g.follow + g.lift * (r + 0.05f), r, c.collisionMask);
	Vec3        origin = g.focus;
	const Vec3  seg    = g.focus - anchor;
	const float segLen = length(seg);
	if (segLen > 1e-4f)
	{
		const SweepHit h = world.sphereCast(anchor, r, seg, c.collisionMask);
		if (h.hit) { origin = anchor + seg * (maxf(0.0f, h.fraction * segLen - maxf(0.0f, c.collisionMargin)) / segLen); }
	}
	turnArm(s, c, armAim(c, world, origin, armBasis(s), g.lift, g.length), dt);
	updateArm(s, c, room(c, world, origin, s.armDir, g.length), dt);
	return origin;
}

} // namespace mitiru::action::camera_rig_detail
