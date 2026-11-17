#pragma once

/// @file CameraRigSearch_impl.hpp
/// @brief 見通し探索の持ち上げに、上昇と下降の速度制限と、体の次の位置の先読みを加える。
///        CameraRigArm_impl.hpp の placeArm の手前から読む。
/// @details 持ち上げの割合は、望む向き back から体の上 lift へ回す角の割合 (0..1)。前のフレームの割合は CameraRigState::searchShare に持つ (腕の向きの遅れを含めて速さを測ると、制限が数倍きつくなるため)。割合は望む向きに対するので、スティックで回しても保たれる。制限が働いていなかったフレームのあとは、腕の向きから読み戻して引き継ぐ。

#include <cmath>

namespace mitiru::action::camera_rig_detail
{

/// @brief armAim と同じ回転軸と角度
struct LiftSweep
{
	Vec3  axis;
	float angle;
};

[[nodiscard]] inline LiftSweep liftSweep(const ArmBasis& b, const Vec3& lift) noexcept
{
	return {normalizeOr(cross(b.back, lift), b.right), std::acos(clampf(dot(b.back, lift), -1.0f, 1.0f))};
}

[[nodiscard]] inline float liftShareOf(const ArmBasis& b, const LiftSweep& sweep, const Vec3& armDir) noexcept
{
	if (sweep.angle < 1e-4f) { return 0.0f; }
	const float turned = std::atan2(dot(armDir, cross(sweep.axis, b.back)), dot(armDir, b.back));
	return clampf(turned / sweep.angle, 0.0f, 1.0f);
}

/// @brief origin から見て、腕を伸ばせる最初の持ち上げの割合。すべて塞がれていれば 0
[[nodiscard]] inline float searchShare(const CameraRigConfig& c, const CollisionWorld& world, const Vec3& origin,
                                       const ArmBasis& b, const LiftSweep& sweep, float length, bool pass) noexcept
{
	const float enough = length * clampf(c.blockedShare, 0.0f, 1.0f);
	if (room(c, world, origin, b.back, length, pass) >= enough) { return 0.0f; }
	for (std::uint32_t k = 1; k <= c.searchSteps; ++k)
	{
		const float share = static_cast<float>(k) / static_cast<float>(c.searchSteps);
		const Vec3  dir   = rotateAbout(b.back, sweep.axis, sweep.angle * share);
		if (room(c, world, origin, dir, length, pass) >= enough) { return share; }
	}
	return 0.0f;
}

/// @brief ahead だけ先の位置から探索できるか。壁の中か、壁を抜ける動きなら探索しない
[[nodiscard]] inline bool aheadIsClear(const CameraRigConfig& c, const CollisionWorld& world, const Vec3& origin,
                                       const Vec3& ahead) noexcept
{
	const float r = maxf(0.0f, c.probeRadius);
	Contact     cs[1];
	return world.overlapSphere(origin + ahead, r, cs, 1, c.collisionMask) == 0
	    && !world.sphereCast(origin, r, ahead, c.collisionMask).hit;
}

[[nodiscard]] inline bool pacedSearch(const CameraRigConfig& c, const Vec3& lookAhead) noexcept
{
	const float l2 = lengthSq(lookAhead);
	return c.searchSteps != 0 && (c.searchRiseRate > 0.0f || c.searchFallRate > 0.0f || (std::isfinite(l2) && l2 > 0.0f));
}

/// @brief searchRiseRate と searchFallRate (1/秒) で動きを制限し、現在位置と lookAhead の先で、より塞がれているほうに合わせた腕の目標方向
/// @details 目標の割合は、現在位置と先の位置の大きいほう。前のフレームの割合から、上へ rise * dt、下へ fall * dt の範囲に収める。0 以下の速度は、その向きの動きを制限しない。腕の長さは実際の腕の向きにある壁で決まるため、ゆっくり起こしても目は壁に入らない。
[[nodiscard]] inline Vec3 pacedAim(CameraRigState& s, const CameraRigConfig& c, const CollisionWorld& world,
                                   const Vec3& origin, const ArmBasis& b, const Vec3& lift, float length,
                                   const Vec3& lookAhead, bool pass, float dt) noexcept
{
	const LiftSweep sweep  = liftSweep(b, lift);
	float           target = searchShare(c, world, origin, b, sweep, length, pass);
	const float     l2     = lengthSq(lookAhead);
	if (target < 1.0f && std::isfinite(l2) && l2 > 0.0f && aheadIsClear(c, world, origin, lookAhead))
	{
		target = maxf(target, searchShare(c, world, origin + lookAhead, b, sweep, length, pass));
	}
	const float now   = s.searchActive != 0 ? static_cast<float>(s.searchShare) * (1.0f / 65535.0f)
	                                        : liftShareOf(b, sweep, s.armDir);
	const float low   = c.searchFallRate > 0.0f ? now - c.searchFallRate * dt : 0.0f;
	const float high  = c.searchRiseRate > 0.0f ? now + c.searchRiseRate * dt : 1.0f;
	const float share = clampf(clampf(target, low, high), 0.0f, 1.0f);
	s.searchShare     = static_cast<std::uint16_t>(share * 65535.0f + 0.5f);
	s.searchActive    = 1;
	return share > 0.0f ? rotateAbout(b.back, sweep.axis, sweep.angle * share) : b.back;
}

} // namespace mitiru::action::camera_rig_detail
