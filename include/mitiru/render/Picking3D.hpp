#pragma once

/// @file Picking3D.hpp
/// @brief 3D のピッキング補助。画面座標 → ワールドの光線、ワールド → 画面座標。
///
/// `Screen::camera3D(eye, target, fovDeg[, rollDeg])` と同じ規約 (右手系 lookAt、縦 FOV、アスペクト = 幅/高さ) を
/// GPU にも Screen にも触れない純粋関数で持つ。game は `update` の中 (Screen が無い所) でマウスの当たり判定や
/// HTML の吹き出し位置を決めるので、カメラの式をゲーム側へ写さずに済むよう、正本をここに置く。
/// `render::Camera3D` の行列との一致は `tests/mitiru/TestPicking3D.cpp` が検査する。使い方は docs/PICKING_3D.md。

#include <cmath>

#include <sgc/math/Vec3.hpp>

namespace mitiru::pick3d
{

/// @brief `Screen::camera3D` に渡すのと同じカメラの指定。screenW/H は論理スクリーンの大きさ
///        (`in.mouseX()` / `mouseY()` と同じ座標系。既定 1280x720)。
struct CameraView
{
	sgc::Vec3f eye{0.0f, 0.0f, 5.0f};
	sgc::Vec3f target{0.0f, 0.0f, 0.0f};
	float      fovDeg  = 55.0f;   ///< 縦の視野角 (度)
	float      rollDeg = 0.0f;    ///< 視線軸まわりの傾き (度)
	float      screenW = 1280.0f;
	float      screenH = 720.0f;
	float      nearClip = 0.1f;   ///< これより手前 (と背後) は worldToScreen が false を返す
};

struct Ray
{
	sgc::Vec3f origin;
	sgc::Vec3f dir;   ///< 正規化済み
};

namespace detail
{

struct Basis
{
	sgc::Vec3f forward, right, up;
	float      tanHalfFov = 0.0f;
	float      aspect     = 1.0f;
	bool       valid      = false;
};

/// 右手系 lookAt の基底。up は `Screen::camera3D(…, rollDeg)` と同じく、真上/真下を向いたときだけ
/// 基準を +z へ逃がし、視線軸まわりに rollDeg 回す (up' = up·cosθ + (f×up)·sinθ)。
inline Basis basisOf(const CameraView& view) noexcept
{
	constexpr float kDeg = 3.14159265358979f / 180.0f;
	Basis b;
	sgc::Vec3f f{view.target.x - view.eye.x, view.target.y - view.eye.y, view.target.z - view.eye.z};
	const float fl = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
	if (fl < 1e-6f || view.screenW <= 0.0f || view.screenH <= 0.0f) { return b; }
	f = {f.x / fl, f.y / fl, f.z / fl};

	const sgc::Vec3f base = (f.y > 0.999f || f.y < -0.999f)
		? sgc::Vec3f{0.0f, 0.0f, 1.0f} : sgc::Vec3f{0.0f, 1.0f, 0.0f};
	const sgc::Vec3f fxu = f.cross(base);
	const float c = std::cos(view.rollDeg * kDeg), s = std::sin(view.rollDeg * kDeg);
	const sgc::Vec3f rolledUp{base.x * c + fxu.x * s, base.y * c + fxu.y * s, base.z * c + fxu.z * s};

	b.forward    = f;
	b.right      = f.cross(rolledUp).normalized();
	b.up         = b.right.cross(f);
	b.tanHalfFov = std::tan(view.fovDeg * kDeg * 0.5f);
	b.aspect     = view.screenW / view.screenH;
	b.valid      = b.tanHalfFov > 1e-6f;
	return b;
}

}  // namespace detail

/// @brief 画面座標 (左上原点、論理スクリーン座標) を通る光線。origin はカメラの位置。
///        カメラの指定が縮退している (eye == target など) ときは dir = {0,0,0} を返す。
[[nodiscard]] inline Ray screenToRay(const CameraView& view, float screenX, float screenY) noexcept
{
	const detail::Basis b = detail::basisOf(view);
	if (!b.valid) { return Ray{view.eye, {0.0f, 0.0f, 0.0f}}; }
	const float nx = (2.0f * screenX / view.screenW - 1.0f) * b.tanHalfFov * b.aspect;
	const float ny = (1.0f - 2.0f * screenY / view.screenH) * b.tanHalfFov;
	const sgc::Vec3f dir{
		b.forward.x + b.right.x * nx + b.up.x * ny,
		b.forward.y + b.right.y * nx + b.up.y * ny,
		b.forward.z + b.right.z * nx + b.up.z * ny};
	return Ray{view.eye, dir.normalized()};
}

/// @brief ワールド座標を画面座標へ。カメラの手前 (nearClip より先) に無ければ false。
///        画面の外でも true を返す (吹き出しを画面端へ寄せる等は呼び出し側で決める)。
[[nodiscard]] inline bool worldToScreen(const CameraView& view, const sgc::Vec3f& world,
                                        float& screenX, float& screenY) noexcept
{
	const detail::Basis b = detail::basisOf(view);
	if (!b.valid) { return false; }
	const sgc::Vec3f v{world.x - view.eye.x, world.y - view.eye.y, world.z - view.eye.z};
	const float depth = v.dot(b.forward);
	if (depth <= view.nearClip) { return false; }
	const float nx = v.dot(b.right) / (depth * b.tanHalfFov * b.aspect);
	const float ny = v.dot(b.up) / (depth * b.tanHalfFov);
	screenX = (nx + 1.0f) * 0.5f * view.screenW;
	screenY = (1.0f - ny) * 0.5f * view.screenH;
	return true;
}

/// @brief 光線と軸に沿った箱 (中心 + 半寸法) の交差。当たれば手前側の距離 t (>= 0) を返す。
[[nodiscard]] inline bool rayAabb(const Ray& ray, const sgc::Vec3f& center, const sgc::Vec3f& halfExtents,
                                  float& tOut) noexcept
{
	const float origin[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
	const float dir[3]    = {ray.dir.x, ray.dir.y, ray.dir.z};
	const float lo[3]     = {center.x - halfExtents.x, center.y - halfExtents.y, center.z - halfExtents.z};
	const float hi[3]     = {center.x + halfExtents.x, center.y + halfExtents.y, center.z + halfExtents.z};
	float tMin = 0.0f, tMax = 1.0e30f;
	for (int i = 0; i < 3; ++i)
	{
		if (std::fabs(dir[i]) < 1e-8f)
		{
			if (origin[i] < lo[i] || origin[i] > hi[i]) { return false; }
			continue;
		}
		float t1 = (lo[i] - origin[i]) / dir[i];
		float t2 = (hi[i] - origin[i]) / dir[i];
		if (t1 > t2) { const float tmp = t1; t1 = t2; t2 = tmp; }
		if (t1 > tMin) { tMin = t1; }
		if (t2 < tMax) { tMax = t2; }
		if (tMin > tMax) { return false; }
	}
	tOut = tMin;
	return true;
}

/// @brief 光線と水平面 (y = planeY) の交点。床の上へ物を落とす位置を決めるのに使う。
[[nodiscard]] inline bool rayPlaneY(const Ray& ray, float planeY, sgc::Vec3f& hitOut) noexcept
{
	if (std::fabs(ray.dir.y) < 1e-8f) { return false; }
	const float t = (planeY - ray.origin.y) / ray.dir.y;
	if (t < 0.0f) { return false; }
	hitOut = {ray.origin.x + ray.dir.x * t, planeY, ray.origin.z + ray.dir.z * t};
	return true;
}

}  // namespace mitiru::pick3d
