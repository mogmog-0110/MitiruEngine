#pragma once

/// @file CameraThinWall.hpp
/// @brief 薄い壁の向こうへ目を置くための到達距離の宣言。OccluderFade 用。
/// @details レイを 2 本使い、壁の表面と裏面の間で厚さを測る。球の重なりでは、球が中に入る厚い壁と薄い壁を見分けられないため。厚さのない面は裏面がないので越えない。越える壁は 1 枚だけとし、その先に壁があれば手前で止める。

#include <cstdint>

#include <mitiru/action/CollisionWorld.hpp>

namespace mitiru::action
{

/// @brief origin から単位ベクトル dir の向きへ、目の球を length まで伸ばすときの到達距離 (m)
/// @param maxThickness 越えてよい壁の厚さ (m、面の法線方向)。0 以下では越えず、壁の手前から margin だけ引いて止める
[[nodiscard]] inline float reachThroughThinWall(const CollisionWorld& world, const Vec3& origin, const Vec3& dir,
                                                float length, float radius, float margin, std::uint32_t mask,
                                                float maxThickness) noexcept
{
	const float r = maxf(0.0f, radius);
	const float m = maxf(0.0f, margin);
	const SweepHit h = world.sphereCast(origin, r, dir * length, mask);
	if (!h.hit) { return length; }
	const float front = clampf(h.fraction * length - m, 0.0f, length);
	if (!(maxThickness > 0.0f)) { return front; }

	// 面にほぼ平行な向きでは、厚さと通過位置が決まらない
	constexpr float kMinCos = 0.1f;
	constexpr float kInside = 1e-3f;
	const RayHit    entry   = world.raycast(origin, dir, length, mask);
	if (!entry.hit) { return front; }
	const float entryCos = absf(dot(dir, entry.normal));
	if (entryCos < kMinCos) { return front; }
	const RayHit leave = world.raycast(origin + dir * (entry.distance + kInside), dir, maxThickness / entryCos, mask);
	if (!leave.hit) { return front; }
	const float leaveCos = absf(dot(dir, leave.normal));
	if (leaveCos < kMinCos) { return front; }

	const float exit = entry.distance + kInside + leave.distance + (r + m) / leaveCos;
	if (exit > length) { return front; }
	const Vec3 exitPoint = origin + dir * exit;
	Contact    touch[1];
	if (world.overlapSphere(exitPoint, r, touch, 1, mask) > 0) { return front; }
	const float    rest = length - exit;
	const SweepHit next = world.sphereCast(exitPoint, r, dir * rest, mask);
	return next.hit ? exit + clampf(next.fraction * rest - m, 0.0f, rest) : length;
}

} // namespace mitiru::action
