#pragma once

/// @file CollisionShape3D.hpp
/// @brief 3D衝突形状と衝突検出関数
///
/// 球・ボックス・カプセル・平面の3D衝突形状を定義し、
/// 各組み合わせの衝突判定関数を提供する。
///
/// @code
/// using namespace sgc::physics;
/// SphereShape a{0.5f};
/// SphereShape b{0.3f};
/// auto result = testSphereSphere({0,0,0}, a, {0.6f,0,0}, b);
/// if (result.collided) { /* 衝突処理 */ }
/// @endcode

#include <algorithm>
#include <cmath>

#include "sgc/math/Vec3.hpp"

namespace sgc::physics
{

/// @brief 3D衝突形状の種別
enum class ShapeType3D
{
	Sphere,   ///< 球
	Box,      ///< 軸平行ボックス
	Capsule,  ///< カプセル
	Plane     ///< 無限平面
};

/// @brief 球形状
struct SphereShape
{
	float radius = 0.5f;  ///< 半径
};

/// @brief ボックス形状（軸平行）
struct BoxShape
{
	Vec3f halfExtents{0.5f, 0.5f, 0.5f};  ///< 各軸の半分のサイズ
};

/// @brief カプセル形状
struct CapsuleShape
{
	float radius = 0.5f;  ///< 半径
	float height = 1.0f;  ///< 高さ（中心間距離）
};

/// @brief 平面形状
struct PlaneShape
{
	Vec3f normal{0.0f, 1.0f, 0.0f};  ///< 平面法線（正規化済み）
	float distance = 0.0f;            ///< 原点からの距離
};

/// @brief 3D衝突結果
struct CollisionResult3D
{
	bool collided = false;        ///< 衝突したか
	Vec3f contactPoint{};         ///< 接触点
	Vec3f contactNormal{};        ///< 接触法線（AからBへ向かう方向）
	float penetrationDepth = 0.0f; ///< めり込み深さ
};

/// @brief 球同士の衝突判定
/// @param posA 球Aの中心位置
/// @param a 球Aの形状
/// @param posB 球Bの中心位置
/// @param b 球Bの形状
/// @return 衝突結果
[[nodiscard]] inline CollisionResult3D testSphereSphere(
	const Vec3f& posA, const SphereShape& a,
	const Vec3f& posB, const SphereShape& b)
{
	const Vec3f diff = posB - posA;
	const float distSq = diff.lengthSquared();
	const float radiusSum = a.radius + b.radius;

	if (distSq > radiusSum * radiusSum)
	{
		return {};
	}

	const float dist = std::sqrt(distSq);
	CollisionResult3D result;
	result.collided = true;
	result.penetrationDepth = radiusSum - dist;

	if (dist > 0.0f)
	{
		result.contactNormal = diff / dist;
	}
	else
	{
		result.contactNormal = {0.0f, 1.0f, 0.0f};
	}

	result.contactPoint = posA + result.contactNormal * a.radius;
	return result;
}

/// @brief 球とボックスの衝突判定
/// @param spherePos 球の中心位置
/// @param sphere 球の形状
/// @param boxPos ボックスの中心位置
/// @param box ボックスの形状
/// @return 衝突結果
[[nodiscard]] inline CollisionResult3D testSphereBox(
	const Vec3f& spherePos, const SphereShape& sphere,
	const Vec3f& boxPos, const BoxShape& box)
{
	// 球の中心をボックスローカル座標に変換
	const Vec3f local = spherePos - boxPos;

	// 最近接点をクランプで求める
	const Vec3f closest{
		std::clamp(local.x, -box.halfExtents.x, box.halfExtents.x),
		std::clamp(local.y, -box.halfExtents.y, box.halfExtents.y),
		std::clamp(local.z, -box.halfExtents.z, box.halfExtents.z)
	};

	const Vec3f diff = local - closest;
	const float distSq = diff.lengthSquared();

	if (distSq > sphere.radius * sphere.radius)
	{
		return {};
	}

	const float dist = std::sqrt(distSq);
	CollisionResult3D result;
	result.collided = true;
	result.penetrationDepth = sphere.radius - dist;

	if (dist > 0.0f)
	{
		result.contactNormal = diff / dist;
	}
	else
	{
		result.contactNormal = {0.0f, 1.0f, 0.0f};
	}

	result.contactPoint = boxPos + closest;
	return result;
}

/// @brief 球と平面の衝突判定
/// @param spherePos 球の中心位置
/// @param sphere 球の形状
/// @param plane 平面の形状
/// @return 衝突結果
[[nodiscard]] inline CollisionResult3D testSpherePlane(
	const Vec3f& spherePos, const SphereShape& sphere,
	const PlaneShape& plane)
{
	const float signedDist = spherePos.dot(plane.normal) - plane.distance;

	if (signedDist > sphere.radius)
	{
		return {};
	}

	CollisionResult3D result;
	result.collided = true;
	result.contactNormal = plane.normal;
	result.penetrationDepth = sphere.radius - signedDist;
	result.contactPoint = spherePos - plane.normal * signedDist;
	return result;
}

/// @brief ボックス同士の衝突判定（AABB）
/// @param posA ボックスAの中心位置
/// @param a ボックスAの形状
/// @param posB ボックスBの中心位置
/// @param b ボックスBの形状
/// @return 衝突結果
[[nodiscard]] inline CollisionResult3D testBoxBox(
	const Vec3f& posA, const BoxShape& a,
	const Vec3f& posB, const BoxShape& b)
{
	const Vec3f diff = posB - posA;

	const float overlapX = (a.halfExtents.x + b.halfExtents.x) - std::abs(diff.x);
	if (overlapX <= 0.0f) return {};

	const float overlapY = (a.halfExtents.y + b.halfExtents.y) - std::abs(diff.y);
	if (overlapY <= 0.0f) return {};

	const float overlapZ = (a.halfExtents.z + b.halfExtents.z) - std::abs(diff.z);
	if (overlapZ <= 0.0f) return {};

	CollisionResult3D result;
	result.collided = true;

	// 最小のめり込み軸を選択
	if (overlapX <= overlapY && overlapX <= overlapZ)
	{
		result.penetrationDepth = overlapX;
		result.contactNormal = (diff.x >= 0.0f)
			? Vec3f{1.0f, 0.0f, 0.0f}
			: Vec3f{-1.0f, 0.0f, 0.0f};
	}
	else if (overlapY <= overlapZ)
	{
		result.penetrationDepth = overlapY;
		result.contactNormal = (diff.y >= 0.0f)
			? Vec3f{0.0f, 1.0f, 0.0f}
			: Vec3f{0.0f, -1.0f, 0.0f};
	}
	else
	{
		result.penetrationDepth = overlapZ;
		result.contactNormal = (diff.z >= 0.0f)
			? Vec3f{0.0f, 0.0f, 1.0f}
			: Vec3f{0.0f, 0.0f, -1.0f};
	}

	// 接触点は2つのAABBの重なり領域の中心
	result.contactPoint = (posA + posB) * 0.5f;
	return result;
}

} // namespace sgc::physics
