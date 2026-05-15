#pragma once

/// @file CollisionDetection3D.hpp
/// @brief 3D衝突検出関数群
///
/// 球、AABB、OBB、カプセル、レイとの衝突判定を提供する。
/// 簡易GJKアルゴリズムによる凸形状同士の判定もサポート。
///
/// @code
/// auto contact = mitiru::physics3d::testSphereSphere(sphereA, sphereB);
/// if (contact.hasContact) { /* 衝突処理 */ }
///
/// auto hit = mitiru::physics3d::raycastSphere(ray, sphere);
/// if (hit.has_value()) { /* ヒット処理 */ }
/// @endcode

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

#include "sgc/math/Vec3.hpp"
#include "mitiru/physics/Collider3D.hpp"

namespace mitiru::physics3d
{

/// @brief レイキャストヒット結果
struct RayHit3D
{
	sgc::Vec3f point{};     ///< ヒット座標
	sgc::Vec3f normal{};    ///< ヒット面の法線
	float distance{0.0f};   ///< レイ始点からの距離
};

// ── 球 vs 球 ──────────────────────────────────────────────

/// @brief 球と球の衝突判定
/// @param a 球A
/// @param b 球B
/// @return 接触情報
[[nodiscard]] inline ContactInfo3D testSphereSphere(
	const SphereCollider& a, const SphereCollider& b) noexcept
{
	const sgc::Vec3f diff = b.center - a.center;
	const float distSq = diff.lengthSquared();
	const float radiusSum = a.radius + b.radius;

	if (distSq >= radiusSum * radiusSum)
	{
		return {};
	}

	ContactInfo3D info;
	info.hasContact = true;

	const float dist = std::sqrt(distSq);

	if (dist > 1e-6f)
	{
		info.normal = diff / dist;
	}
	else
	{
		info.normal = sgc::Vec3f::unitY();
	}

	info.depth = radiusSum - dist;
	info.point = a.center + info.normal * (a.radius - info.depth * 0.5f);

	return info;
}

// ── 球 vs AABB ────────────────────────────────────────────

/// @brief AABBの最近接点を求める
/// @param point 対象点
/// @param aabb AABB
/// @return AABB上の最近接点
[[nodiscard]] inline constexpr sgc::Vec3f closestPointOnAABB(
	const sgc::Vec3f& point, const AABBCollider3D& aabb) noexcept
{
	return point.clamped(aabb.min, aabb.max);
}

/// @brief 球とAABBの衝突判定
/// @param sphere 球
/// @param aabb AABB
/// @return 接触情報
[[nodiscard]] inline ContactInfo3D testSphereAABB(
	const SphereCollider& sphere, const AABBCollider3D& aabb) noexcept
{
	const sgc::Vec3f closest = closestPointOnAABB(sphere.center, aabb);
	const sgc::Vec3f diff = sphere.center - closest;
	const float distSq = diff.lengthSquared();

	if (distSq >= sphere.radius * sphere.radius)
	{
		return {};
	}

	ContactInfo3D info;
	info.hasContact = true;

	const float dist = std::sqrt(distSq);

	if (dist > 1e-6f)
	{
		info.normal = diff / dist;
	}
	else
	{
		// 球の中心がAABB内部にある場合、最小押し出し軸を使う
		const sgc::Vec3f aabbCenter = aabb.center();
		const sgc::Vec3f halfExt = aabb.halfExtents();
		const sgc::Vec3f delta = sphere.center - aabbCenter;

		float minOverlap = halfExt.x - std::abs(delta.x);
		info.normal = sgc::Vec3f{delta.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f};

		const float overlapY = halfExt.y - std::abs(delta.y);
		if (overlapY < minOverlap)
		{
			minOverlap = overlapY;
			info.normal = sgc::Vec3f{0.0f, delta.y >= 0.0f ? 1.0f : -1.0f, 0.0f};
		}

		const float overlapZ = halfExt.z - std::abs(delta.z);
		if (overlapZ < minOverlap)
		{
			info.normal = sgc::Vec3f{0.0f, 0.0f, delta.z >= 0.0f ? 1.0f : -1.0f};
		}
	}

	info.depth = sphere.radius - dist;
	info.point = closest;

	return info;
}

// ── AABB vs AABB ──────────────────────────────────────────

/// @brief AABB同士の衝突判定
/// @param a AABB A
/// @param b AABB B
/// @return 接触情報
[[nodiscard]] inline ContactInfo3D testAABBAABB(
	const AABBCollider3D& a, const AABBCollider3D& b) noexcept
{
	// 各軸で分離判定
	if (a.max.x < b.min.x || a.min.x > b.max.x) return {};
	if (a.max.y < b.min.y || a.min.y > b.max.y) return {};
	if (a.max.z < b.min.z || a.min.z > b.max.z) return {};

	// 各軸の重なりを計算
	const float overlapX = std::min(a.max.x, b.max.x) - std::max(a.min.x, b.min.x);
	const float overlapY = std::min(a.max.y, b.max.y) - std::max(a.min.y, b.min.y);
	const float overlapZ = std::min(a.max.z, b.max.z) - std::max(a.min.z, b.min.z);

	ContactInfo3D info;
	info.hasContact = true;

	const sgc::Vec3f centerA = a.center();
	const sgc::Vec3f centerB = b.center();

	// 最小重なり軸を衝突法線とする
	if (overlapX <= overlapY && overlapX <= overlapZ)
	{
		info.depth = overlapX;
		info.normal = sgc::Vec3f{(centerB.x > centerA.x) ? 1.0f : -1.0f, 0.0f, 0.0f};
	}
	else if (overlapY <= overlapZ)
	{
		info.depth = overlapY;
		info.normal = sgc::Vec3f{0.0f, (centerB.y > centerA.y) ? 1.0f : -1.0f, 0.0f};
	}
	else
	{
		info.depth = overlapZ;
		info.normal = sgc::Vec3f{0.0f, 0.0f, (centerB.z > centerA.z) ? 1.0f : -1.0f};
	}

	info.point = sgc::Vec3f{
		(std::max(a.min.x, b.min.x) + std::min(a.max.x, b.max.x)) * 0.5f,
		(std::max(a.min.y, b.min.y) + std::min(a.max.y, b.max.y)) * 0.5f,
		(std::max(a.min.z, b.min.z) + std::min(a.max.z, b.max.z)) * 0.5f
	};

	return info;
}

// ── Ray vs Sphere ─────────────────────────────────────────

/// @brief レイと球の衝突判定
/// @param ray レイ
/// @param sphere 球
/// @param maxDist 最大検出距離
/// @return ヒット結果（ヒットなしの場合はnullopt）
[[nodiscard]] inline std::optional<RayHit3D> raycastSphere(
	const Ray3D& ray, const SphereCollider& sphere, float maxDist = 1e6f) noexcept
{
	const sgc::Vec3f dir = ray.direction.normalized();
	const sgc::Vec3f oc = ray.origin - sphere.center;

	const float a = dir.dot(dir);
	const float b = 2.0f * oc.dot(dir);
	const float c = oc.dot(oc) - sphere.radius * sphere.radius;
	const float discriminant = b * b - 4.0f * a * c;

	if (discriminant < 0.0f) return std::nullopt;

	const float sqrtD = std::sqrt(discriminant);
	float t = (-b - sqrtD) / (2.0f * a);

	if (t < 0.0f)
	{
		t = (-b + sqrtD) / (2.0f * a);
	}

	if (t < 0.0f || t > maxDist) return std::nullopt;

	const sgc::Vec3f hitPoint = ray.origin + dir * t;
	const sgc::Vec3f hitNormal = (hitPoint - sphere.center).normalized();

	return RayHit3D{hitPoint, hitNormal, t};
}

// ── Ray vs AABB ───────────────────────────────────────────

/// @brief レイとAABBの衝突判定（slabメソッド）
/// @param ray レイ
/// @param aabb AABB
/// @param maxDist 最大検出距離
/// @return ヒット結果（ヒットなしの場合はnullopt）
[[nodiscard]] inline std::optional<RayHit3D> raycastAABB(
	const Ray3D& ray, const AABBCollider3D& aabb, float maxDist = 1e6f) noexcept
{
	const sgc::Vec3f dir = ray.direction.normalized();

	float tMin = 0.0f;
	float tMax = maxDist;
	sgc::Vec3f normal{};

	// X軸
	{
		const float invD = (std::abs(dir.x) > 1e-8f) ? (1.0f / dir.x) : 1e8f;
		float t0 = (aabb.min.x - ray.origin.x) * invD;
		float t1 = (aabb.max.x - ray.origin.x) * invD;
		sgc::Vec3f n0 = sgc::Vec3f{-1.0f, 0.0f, 0.0f};
		sgc::Vec3f n1 = sgc::Vec3f{1.0f, 0.0f, 0.0f};

		if (invD < 0.0f)
		{
			std::swap(t0, t1);
			std::swap(n0, n1);
		}

		if (t0 > tMin) { tMin = t0; normal = n0; }
		if (t1 < tMax) { tMax = t1; }
		if (tMin > tMax) return std::nullopt;
	}

	// Y軸
	{
		const float invD = (std::abs(dir.y) > 1e-8f) ? (1.0f / dir.y) : 1e8f;
		float t0 = (aabb.min.y - ray.origin.y) * invD;
		float t1 = (aabb.max.y - ray.origin.y) * invD;
		sgc::Vec3f n0 = sgc::Vec3f{0.0f, -1.0f, 0.0f};
		sgc::Vec3f n1 = sgc::Vec3f{0.0f, 1.0f, 0.0f};

		if (invD < 0.0f)
		{
			std::swap(t0, t1);
			std::swap(n0, n1);
		}

		if (t0 > tMin) { tMin = t0; normal = n0; }
		if (t1 < tMax) { tMax = t1; }
		if (tMin > tMax) return std::nullopt;
	}

	// Z軸
	{
		const float invD = (std::abs(dir.z) > 1e-8f) ? (1.0f / dir.z) : 1e8f;
		float t0 = (aabb.min.z - ray.origin.z) * invD;
		float t1 = (aabb.max.z - ray.origin.z) * invD;
		sgc::Vec3f n0 = sgc::Vec3f{0.0f, 0.0f, -1.0f};
		sgc::Vec3f n1 = sgc::Vec3f{0.0f, 0.0f, 1.0f};

		if (invD < 0.0f)
		{
			std::swap(t0, t1);
			std::swap(n0, n1);
		}

		if (t0 > tMin) { tMin = t0; normal = n0; }
		if (t1 < tMax) { tMax = t1; }
		if (tMin > tMax) return std::nullopt;
	}

	if (tMin < 0.0f) return std::nullopt;

	const sgc::Vec3f hitPoint = ray.origin + dir * tMin;
	return RayHit3D{hitPoint, normal, tMin};
}

// ── GJK（簡易版）────────────────────────────────────────────

/// @brief 凸形状のサポート関数インタフェース
///
/// GJKアルゴリズムで使用する。方向ベクトルに対して
/// 凸形状の最遠点を返す。
struct ConvexShape
{
	std::vector<sgc::Vec3f> vertices;

	/// @brief 方向に対するサポート点を返す
	/// @param direction サポート方向
	/// @return 最遠頂点
	[[nodiscard]] sgc::Vec3f support(const sgc::Vec3f& direction) const noexcept
	{
		if (vertices.empty()) return {};

		float maxDot = vertices[0].dot(direction);
		sgc::Vec3f result = vertices[0];

		for (std::size_t i = 1; i < vertices.size(); ++i)
		{
			const float d = vertices[i].dot(direction);
			if (d > maxDot)
			{
				maxDot = d;
				result = vertices[i];
			}
		}

		return result;
	}
};

/// @brief ミンコフスキー差のサポート点を計算する
/// @param a 凸形状A
/// @param b 凸形状B
/// @param direction サポート方向
/// @return ミンコフスキー差上のサポート点
[[nodiscard]] inline sgc::Vec3f gjkSupport(
	const ConvexShape& a, const ConvexShape& b, const sgc::Vec3f& direction) noexcept
{
	return a.support(direction) - b.support(-direction);
}

/// @brief 三重積 (A x B) x C を計算する
[[nodiscard]] inline constexpr sgc::Vec3f tripleProduct(
	const sgc::Vec3f& a, const sgc::Vec3f& b, const sgc::Vec3f& c) noexcept
{
	return b * a.dot(c) - a * b.dot(c);
}

/// @brief GJKアルゴリズムによる凸形状同士の交差判定
/// @param a 凸形状A
/// @param b 凸形状B
/// @return 交差している場合true
[[nodiscard]] inline bool gjkIntersect(const ConvexShape& a, const ConvexShape& b) noexcept
{
	if (a.vertices.empty() || b.vertices.empty()) return false;

	sgc::Vec3f direction = sgc::Vec3f{1.0f, 0.0f, 0.0f};

	// 初期サポート点
	sgc::Vec3f simplex[4];
	simplex[0] = gjkSupport(a, b, direction);
	int simplexSize = 1;

	direction = -simplex[0];

	if (direction.lengthSquared() < 1e-10f) return true;

	constexpr int maxIterations = 64;

	for (int iter = 0; iter < maxIterations; ++iter)
	{
		const sgc::Vec3f newPoint = gjkSupport(a, b, direction);

		// 新しい点が探索方向に対して原点を超えていなければ交差なし
		if (newPoint.dot(direction) < 0.0f)
		{
			return false;
		}

		simplex[simplexSize] = newPoint;
		++simplexSize;

		if (simplexSize == 2)
		{
			// 線分ケース
			const sgc::Vec3f ab = simplex[0] - simplex[1];
			const sgc::Vec3f ao = -simplex[1];

			if (ab.dot(ao) > 0.0f)
			{
				direction = tripleProduct(ab, ao, ab);
				if (direction.lengthSquared() < 1e-10f) return true;
			}
			else
			{
				simplex[0] = simplex[1];
				simplexSize = 1;
				direction = ao;
			}
		}
		else if (simplexSize == 3)
		{
			// 三角形ケース
			const sgc::Vec3f ab = simplex[1] - simplex[2];
			const sgc::Vec3f ac = simplex[0] - simplex[2];
			const sgc::Vec3f ao = -simplex[2];
			const sgc::Vec3f abcNormal = ab.cross(ac);

			if (abcNormal.cross(ac).dot(ao) > 0.0f)
			{
				if (ac.dot(ao) > 0.0f)
				{
					simplex[1] = simplex[2];
					simplexSize = 2;
					direction = tripleProduct(ac, ao, ac);
					if (direction.lengthSquared() < 1e-10f) return true;
				}
				else
				{
					simplex[0] = simplex[1];
					simplex[1] = simplex[2];
					simplexSize = 2;
					const sgc::Vec3f abNew = simplex[0] - simplex[1];
					const sgc::Vec3f aoNew = -simplex[1];
					if (abNew.dot(aoNew) > 0.0f)
					{
						direction = tripleProduct(abNew, aoNew, abNew);
						if (direction.lengthSquared() < 1e-10f) return true;
					}
					else
					{
						simplex[0] = simplex[1];
						simplexSize = 1;
						direction = aoNew;
					}
				}
			}
			else if (ab.cross(abcNormal).dot(ao) > 0.0f)
			{
				simplex[0] = simplex[1];
				simplex[1] = simplex[2];
				simplexSize = 2;
				const sgc::Vec3f abNew = simplex[0] - simplex[1];
				const sgc::Vec3f aoNew = -simplex[1];
				if (abNew.dot(aoNew) > 0.0f)
				{
					direction = tripleProduct(abNew, aoNew, abNew);
					if (direction.lengthSquared() < 1e-10f) return true;
				}
				else
				{
					simplex[0] = simplex[1];
					simplexSize = 1;
					direction = aoNew;
				}
			}
			else
			{
				// 原点は三角形の面上
				if (abcNormal.dot(ao) > 0.0f)
				{
					direction = abcNormal;
				}
				else
				{
					// 三角形の裏側
					std::swap(simplex[0], simplex[1]);
					direction = -abcNormal;
				}
			}
		}
		else if (simplexSize == 4)
		{
			// 四面体ケース
			const sgc::Vec3f ab = simplex[2] - simplex[3];
			const sgc::Vec3f ac = simplex[1] - simplex[3];
			const sgc::Vec3f ad = simplex[0] - simplex[3];
			const sgc::Vec3f ao = -simplex[3];

			const sgc::Vec3f abcNormal = ab.cross(ac);
			const sgc::Vec3f acdNormal = ac.cross(ad);
			const sgc::Vec3f adbNormal = ad.cross(ab);

			if (abcNormal.dot(ao) > 0.0f)
			{
				// ABCの面側
				simplex[0] = simplex[1];
				simplex[1] = simplex[2];
				simplex[2] = simplex[3];
				simplexSize = 3;
				direction = abcNormal;
			}
			else if (acdNormal.dot(ao) > 0.0f)
			{
				// ACDの面側
				simplex[2] = simplex[3];
				simplexSize = 3;
				direction = acdNormal;
			}
			else if (adbNormal.dot(ao) > 0.0f)
			{
				// ADBの面側
				simplex[1] = simplex[0];
				simplex[0] = simplex[2];
				simplex[2] = simplex[3];
				simplexSize = 3;
				direction = adbNormal;
			}
			else
			{
				// 原点は四面体内部
				return true;
			}
		}
	}

	return false;
}

// ── 線分上の最近接点 ──────────────────────────────────────

/// @brief 線分上の最近接点を求める
/// @param point 対象点
/// @param segA 線分端点A
/// @param segB 線分端点B
/// @return 線分上の最近接点
[[nodiscard]] inline sgc::Vec3f closestPointOnSegment(
	const sgc::Vec3f& point, const sgc::Vec3f& segA, const sgc::Vec3f& segB) noexcept
{
	const sgc::Vec3f ab = segB - segA;
	const float abLenSq = ab.lengthSquared();

	if (abLenSq < 1e-10f) return segA;

	float t = (point - segA).dot(ab) / abLenSq;
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;

	return segA + ab * t;
}

/// @brief 2つの線分間の最近接点ペアを求める
/// @param a0 線分Aの端点0
/// @param a1 線分Aの端点1
/// @param b0 線分Bの端点0
/// @param b1 線分Bの端点1
/// @param outA 線分A上の最近接点（出力）
/// @param outB 線分B上の最近接点（出力）
inline void closestPointsSegmentSegment(
	const sgc::Vec3f& a0, const sgc::Vec3f& a1,
	const sgc::Vec3f& b0, const sgc::Vec3f& b1,
	sgc::Vec3f& outA, sgc::Vec3f& outB) noexcept
{
	const sgc::Vec3f d1 = a1 - a0;
	const sgc::Vec3f d2 = b1 - b0;
	const sgc::Vec3f r = a0 - b0;

	const float a = d1.dot(d1);
	const float e = d2.dot(d2);
	const float f = d2.dot(r);

	if (a < 1e-10f && e < 1e-10f)
	{
		outA = a0;
		outB = b0;
		return;
	}

	float s, t;

	if (a < 1e-10f)
	{
		s = 0.0f;
		t = f / e;
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
	}
	else
	{
		const float c = d1.dot(r);
		if (e < 1e-10f)
		{
			t = 0.0f;
			s = -c / a;
			if (s < 0.0f) s = 0.0f;
			if (s > 1.0f) s = 1.0f;
		}
		else
		{
			const float b = d1.dot(d2);
			const float denom = a * e - b * b;

			if (std::abs(denom) > 1e-10f)
			{
				s = (b * f - c * e) / denom;
				if (s < 0.0f) s = 0.0f;
				if (s > 1.0f) s = 1.0f;
			}
			else
			{
				s = 0.0f;
			}

			t = (b * s + f) / e;
			if (t < 0.0f)
			{
				t = 0.0f;
				s = -c / a;
				if (s < 0.0f) s = 0.0f;
				if (s > 1.0f) s = 1.0f;
			}
			else if (t > 1.0f)
			{
				t = 1.0f;
				s = (b - c) / a;
				if (s < 0.0f) s = 0.0f;
				if (s > 1.0f) s = 1.0f;
			}
		}
	}

	outA = a0 + d1 * s;
	outB = b0 + d2 * t;
}

// ── カプセル衝突 ──────────────────────────────────────────

/// @brief 球とカプセルの衝突判定
/// @param sphere 球
/// @param capsule カプセル
/// @return 接触情報
[[nodiscard]] inline ContactInfo3D testSphereCapsule(
	const SphereCollider& sphere, const CapsuleCollider& capsule) noexcept
{
	const sgc::Vec3f closest = closestPointOnSegment(
		sphere.center, capsule.pointA, capsule.pointB);

	// カプセルは線分 + 球として扱う
	SphereCollider capsuleSphere{closest, capsule.radius};
	return testSphereSphere(sphere, capsuleSphere);
}

/// @brief カプセル同士の衝突判定
/// @param a カプセルA
/// @param b カプセルB
/// @return 接触情報
[[nodiscard]] inline ContactInfo3D testCapsuleCapsule(
	const CapsuleCollider& a, const CapsuleCollider& b) noexcept
{
	sgc::Vec3f closestA, closestB;
	closestPointsSegmentSegment(
		a.pointA, a.pointB, b.pointA, b.pointB, closestA, closestB);

	SphereCollider sphereA{closestA, a.radius};
	SphereCollider sphereB{closestB, b.radius};
	return testSphereSphere(sphereA, sphereB);
}

} // namespace mitiru::physics3d
