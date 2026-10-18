#pragma once

/// @file CameraRig_impl.hpp
/// @brief updateCameraRig の部品。CameraRig.hpp から読む。

#include <cmath>

namespace mitiru::action
{

namespace camera_rig_detail
{

/// @brief 注視点を持ち上げる向き。pivotUp、無ければ up、どちらも 0 ならワールドの +Y
[[nodiscard]] inline Vec3 liftUp(const CameraRigInput& in) noexcept
{
	const Vec3 world{0.0f, 1.0f, 0.0f};
	if (lengthSq(in.pivotUp) > 1e-12f) { return normalizeOr(in.pivotUp, world); }
	return lengthSq(in.up) > 1e-12f ? normalizeOr(in.up, world) : world;
}

} // namespace camera_rig_detail

/// @brief 状態を作り直し、腕を伸ばしきった位置に置く。yaw は視線の向き (0 で北)
/// @details 上は in.up (0 ならワールドの +Y)、北は +Z を上に直交させたもの (上が +Z に沿うときは任意の直交方向)。
inline void resetCameraRig(CameraRigState& s, const CameraRigConfig& c, const CameraRigInput& in, float yaw) noexcept
{
	s = CameraRigState{};
	if (lengthSq(in.up) > 1e-12f)
	{
		s.up    = normalizeOr(in.up, Vec3{0.0f, 1.0f, 0.0f});
		s.north = normalizeOr(projectOnPlane(Vec3{0.0f, 0.0f, 1.0f}, s.up), anyPerpendicular(s.up));
	}
	s.yaw   = yaw;
	s.pitch = c.defaultPitchDeg * kDegToRad;
	s.focus.reset(in.follow + camera_rig_detail::liftUp(in) * c.pivotHeight);
	s.arm         = c.distance;
	s.armDir      = -cameraRigForward(s);
	s.armYaw      = s.yaw;
	s.armPitch    = s.pitch;
	s.lock        = in.lockOn != 0 ? 1.0f : 0.0f;
	s.initialized = 1;
}

namespace camera_rig_detail
{

[[nodiscard]] inline float wrapAngle(float a) noexcept
{
	a = std::fmod(a + kPi, 2.0f * kPi);
	if (a < 0.0f) { a += 2.0f * kPi; }
	return a - kPi;
}

inline void snap(CameraRigState& s, const CameraRigConfig& c, const CameraRigInput& in) noexcept
{
	resetCameraRig(s, c, in, 0.0f);
	const Vec3 to = cameraRigToLocal(s, in.lockTarget - in.follow);
	if (in.lockOn != 0 && (to.x * to.x + to.z * to.z) > 1e-8f)
	{
		s.yaw    = std::atan2(to.x, to.z);
		s.armYaw = s.yaw;
		s.armDir = -cameraRigForward(s);
	}
}

/// @brief v を単位軸 axis まわりに angle だけ回す (Rodrigues)
[[nodiscard]] inline Vec3 rotateAbout(const Vec3& v, const Vec3& axis, float angle) noexcept
{
	const float c = std::cos(angle);
	const float s = std::sin(angle);
	return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1.0f - c));
}

/// @brief リグの上を in.up へ寄せ、北を同じ回転で運ぶ
/// @details 北を毎回 up から作り直すと、上が視線の向きを通るときに右と左が入れ替わる。運べば視線は座標系ごと回る。
///          上が真逆へ変わるときは回す軸が決まらないので、今の視線の水平成分を軸にして視線を保ったまま回す。
inline void followUp(CameraRigState& s, const CameraRigConfig& c, const CameraRigInput& in, float dt) noexcept
{
	const Vec3 goal = lengthSq(in.up) > 1e-12f ? normalizeOr(in.up, s.up) : Vec3{0.0f, 1.0f, 0.0f};
	if (goal == s.up) { return; }
	const float angle = std::acos(clampf(dot(s.up, goal), -1.0f, 1.0f));
	float       step  = c.upFollowRate > 0.0f ? angle * expDecay(c.upFollowRate, dt) : angle;
	if (angle - step < 1e-4f) { step = angle; }
	const Vec3 heading = cameraRigToWorld(s, Vec3{std::sin(s.yaw), 0.0f, std::cos(s.yaw)});
	const Vec3 axis    = normalizeOr(cross(s.up, goal), normalizeOr(heading, s.north));
	const Vec3 north   = rotateAbout(s.north, axis, step);
	s.up    = step == angle ? goal : normalizeOr(rotateAbout(s.up, axis, step), goal);
	s.north = normalizeOr(projectOnPlane(north, s.up), anyPerpendicular(s.up));
}

/// @brief 出力の視点。画面の上は yaw の右と視線の外積で作り、傾きを足して camera3D の rollDeg に直す
/// @details 上を視線の面へ投影する作り方は、上と視線が重なると投影が 0 に近づき、1 フレームで半回転する。
[[nodiscard]] inline CameraView makeView(const CameraRigState& s, const CameraRigInput& in, const Vec3& eye,
                                         const Vec3& look, float fovDeg) noexcept
{
	CameraView v{eye, look, fovDeg};
	const Vec3 f = normalizeOr(look - eye, cameraRigForward(s));
	// camera3D は真上か真下を向くと基準の上を +Z に替えるので、ワールドの +Y を返してよいのはそれ以外のときだけ
	const bool upright = s.up == Vec3{0.0f, 1.0f, 0.0f} && absf(f.y) <= 0.999f;
	if (upright && in.rollDeg == 0.0f) { return v; }
	const Vec3 projected = normalizeOr(projectOnPlane(s.up, f), anyPerpendicular(f));
	Vec3       u         = normalizeOr(cross(cameraRigRight(s), f), projected);
	if (in.rollDeg != 0.0f) { u = rotateAbout(u, f, in.rollDeg * kDegToRad); }
	v.up      = u;
	v.rollDeg = cameraRollDeg(eye, look, u);
	return v;
}

/// @brief 右スティックで回し、ロックオン中は相手の方へ回り込む
inline void orbit(CameraRigState& s, const CameraRigConfig& c, const CameraRigInput& in, float dt) noexcept
{
	const float free = 1.0f - s.lock;
	s.yaw -= clampf(in.orbitX, -1.0f, 1.0f) * c.yawSpeedDeg * kDegToRad * dt * free;
	s.pitch -= clampf(in.orbitY, -1.0f, 1.0f) * c.pitchSpeedDeg * kDegToRad * dt;
	const Vec3 to = cameraRigToLocal(s, in.lockTarget - in.follow);
	if (s.lock > 0.0f && (to.x * to.x + to.z * to.z) > 1e-8f)
	{
		const float k = expDecay(c.lockYawRate, dt) * s.lock;
		s.yaw += wrapAngle(std::atan2(to.x, to.z) - s.yaw) * k;
		s.pitch += (c.lockPitchDeg * kDegToRad - s.pitch) * k;
	}
	s.yaw   = wrapAngle(s.yaw);
	s.pitch = clampf(s.pitch, c.minPitchDeg * kDegToRad, c.maxPitchDeg * kDegToRad);
}

/// @brief ロックオン中の腕の長さ。2 人を画面の高さの lockScreenRatio に収め、手前の自分も画面に残す
[[nodiscard]] inline float lockDistance(const CameraRigConfig& c, const Vec3& pivot, const Vec3& target,
                                        const Vec3& mid) noexcept
{
	const float halfTan = std::tan(clampf(c.fovDeg, 1.0f, 170.0f) * 0.5f * kDegToRad);
	const float fit     = length(target - pivot) * 0.5f / maxf(1e-3f, c.lockScreenRatio * halfTan);
	const float keep    = length(mid - pivot) + c.minDistance;
	return clampf(maxf(c.distance, maxf(fit, keep)), c.minDistance, maxf(c.minDistance, c.lockMaxDistance));
}

} // namespace camera_rig_detail
} // namespace mitiru::action

#include <mitiru/action/detail/CameraRigArm_impl.hpp>
