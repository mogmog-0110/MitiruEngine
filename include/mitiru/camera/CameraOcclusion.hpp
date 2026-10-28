#pragma once

/// @file CameraOcclusion.hpp
/// @brief 仮想カメラの目が壁に埋まらないよう、注視の根元 (anchor) から目へ球を 1 回掃引して手前に引く。
/// @details 状態を持たない純関数。CameraRig のような遅れて伸びる戻りは付けない (固定カメラは壁の中に置かない前提で、
///          動く追従カメラはもともと CameraRig が壁を避ける)。anchor が壁の中だと目は anchor まで引かれる。

#include <mitiru/action/CollisionWorld.hpp>
#include <mitiru/camera/VirtualCameraStack.hpp>

namespace mitiru::camera
{

struct OcclusionParams
{
	float         probeRadius = 0.25f;   ///< 目の球の半径
	float         margin = 0.05f;        ///< 当たった位置からさらに手前へ引く量
	std::uint32_t mask = action::kAllLayers;
};

[[nodiscard]] inline Vec3 pullEyeFromWalls(const action::CollisionWorld& world, const Vec3& anchor, const Vec3& eye,
                                           const OcclusionParams& p = {}) noexcept
{
	const Vec3  delta = eye - anchor;
	const float len = action::length(delta);
	if (len < 1e-4f) { return eye; }
	const action::SweepHit h = world.sphereCast(anchor, action::maxf(0.0f, p.probeRadius), delta, p.mask);
	if (!h.hit) { return eye; }
	const float keep = action::clampf(h.fraction - action::maxf(0.0f, p.margin) / len, 0.0f, 1.0f);
	return anchor + delta * keep;
}

/// @brief view の目を壁の手前へ引いた視点 (注視点はそのまま)。引かなければ view をそのまま返す
[[nodiscard]] inline CameraView avoidWalls(const CameraView& view, const action::CollisionWorld& world, const Vec3& anchor,
                                           const OcclusionParams& p = {}) noexcept
{
	const Vec3 eye = pullEyeFromWalls(world, anchor, view.eye, p);
	if (eye == view.eye) { return view; }
	CameraView out = view;
	out.eye = eye;
	out.rollDeg = action::cameraRollDeg(out.eye, out.target, out.up);
	return out;
}

}  // namespace mitiru::camera
