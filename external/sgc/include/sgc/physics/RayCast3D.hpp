#pragma once

/// @file RayCast3D.hpp
/// @brief 3Dレイキャスト（光線と形状の交差判定）
///
/// レイと球・AABB・平面の交差判定を提供する。
/// ヒット位置・法線・距離を返す。
///
/// @code
/// using namespace sgc::physics;
/// Ray3D ray{{0,0,0}, {0,0,-1}};
/// auto hit = raycastSphere(ray, {0,0,-5}, 1.0f);
/// if (hit.hit) { /* ヒット処理 */ }
/// @endcode

#include <cmath>

#include "sgc/math/Vec3.hpp"

namespace sgc::physics
{

/// @brief 3Dレイ（光線）
struct Ray3D
{
	Vec3f origin;     ///< レイの原点
	Vec3f direction;  ///< レイの方向（正規化済み）
};

/// @brief レイキャストのヒット結果
struct RayHit3D
{
	bool hit = false;      ///< ヒットしたか
	float distance = 0.0f; ///< 原点からの距離
	Vec3f point{};         ///< ヒット位置
	Vec3f normal{};        ///< ヒット面の法線
};

/// @brief レイと球の交差判定
/// @param ray レイ
/// @param center 球の中心
/// @param radius 球の半径
/// @return ヒット結果
[[nodiscard]] inline RayHit3D raycastSphere(
	const Ray3D& ray, const Vec3f& center, float radius)
{
	const Vec3f oc = ray.origin - center;
	const float b = oc.dot(ray.direction);
	const float c = oc.dot(oc) - radius * radius;

	const float discriminant = b * b - c;
	if (discriminant < 0.0f)
	{
		return {};
	}

	const float sqrtDisc = std::sqrt(discriminant);
	float t = -b - sqrtDisc;

	// レイの後方にある場合、もう一つの交点を試す
	if (t < 0.0f)
	{
		t = -b + sqrtDisc;
		if (t < 0.0f)
		{
			return {};
		}
	}

	RayHit3D result;
	result.hit = true;
	result.distance = t;
	result.point = ray.origin + ray.direction * t;
	result.normal = (result.point - center).normalized();
	return result;
}

/// @brief レイとAABBの交差判定
/// @param ray レイ
/// @param boxMin AABBの最小座標
/// @param boxMax AABBの最大座標
/// @return ヒット結果
[[nodiscard]] inline RayHit3D raycastBox(
	const Ray3D& ray, const Vec3f& boxMin, const Vec3f& boxMax)
{
	float tMin = 0.0f;
	float tMax = 1e30f;
	int hitAxis = 0;
	bool hitMinSide = true;

	const float dirs[3] = {ray.direction.x, ray.direction.y, ray.direction.z};
	const float origins[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
	const float mins[3] = {boxMin.x, boxMin.y, boxMin.z};
	const float maxs[3] = {boxMax.x, boxMax.y, boxMax.z};

	for (int i = 0; i < 3; ++i)
	{
		if (std::abs(dirs[i]) < 1e-8f)
		{
			// レイが軸に平行
			if (origins[i] < mins[i] || origins[i] > maxs[i])
			{
				return {};
			}
		}
		else
		{
			const float invD = 1.0f / dirs[i];
			float t1 = (mins[i] - origins[i]) * invD;
			float t2 = (maxs[i] - origins[i]) * invD;

			bool enterMin = true;
			if (t1 > t2)
			{
				const float tmp = t1;
				t1 = t2;
				t2 = tmp;
				enterMin = false;
			}

			if (t1 > tMin)
			{
				tMin = t1;
				hitAxis = i;
				hitMinSide = enterMin;
			}
			if (t2 < tMax)
			{
				tMax = t2;
			}

			if (tMin > tMax || tMax < 0.0f)
			{
				return {};
			}
		}
	}

	if (tMin < 0.0f)
	{
		return {};
	}

	RayHit3D result;
	result.hit = true;
	result.distance = tMin;
	result.point = ray.origin + ray.direction * tMin;

	// ヒット面の法線を設定
	result.normal = {};
	float* normalArr[3] = {&result.normal.x, &result.normal.y, &result.normal.z};
	*normalArr[hitAxis] = hitMinSide ? -1.0f : 1.0f;

	return result;
}

/// @brief レイと平面の交差判定
/// @param ray レイ
/// @param planeNormal 平面の法線（正規化済み）
/// @param planeDistance 原点からの平面の距離
/// @return ヒット結果
[[nodiscard]] inline RayHit3D raycastPlane(
	const Ray3D& ray, const Vec3f& planeNormal, float planeDistance)
{
	const float denom = ray.direction.dot(planeNormal);

	if (std::abs(denom) < 1e-8f)
	{
		return {};  // レイが平面に平行
	}

	const float t = (planeDistance - ray.origin.dot(planeNormal)) / denom;

	if (t < 0.0f)
	{
		return {};  // レイの後方
	}

	RayHit3D result;
	result.hit = true;
	result.distance = t;
	result.point = ray.origin + ray.direction * t;
	result.normal = planeNormal;
	return result;
}

} // namespace sgc::physics
