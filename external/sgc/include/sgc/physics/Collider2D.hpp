#pragma once

/// @file Collider2D.hpp
/// @brief 2D衝突形状と衝突検出関数
///
/// 円、AABB、OBB、カプセルなどの2Dコライダー形状と、
/// 形状間の衝突検出（接触点・法線・めり込み深さ）を提供する。
///
/// @code
/// using namespace sgc::physics;
///
/// CircleCollider a{{0.0f, 0.0f}, 5.0f};
/// CircleCollider b{{8.0f, 0.0f}, 5.0f};
///
/// auto contact = testCircleCircle(a, b);
/// if (contact)
/// {
///     // contact->normal, contact->penetration, contact->point
/// }
/// @endcode

#include <cmath>
#include <optional>

#include "sgc/math/Vec2.hpp"

namespace sgc::physics
{

/// @brief 2Dコライダーの種類
enum class Collider2DType
{
	Circle,     ///< 円形
	AABB,       ///< 軸平行バウンディングボックス
	OBB,        ///< 有向バウンディングボックス
	Capsule,    ///< カプセル
	Polygon     ///< 多角形（将来拡張用）
};

/// @brief 円形コライダー
struct CircleCollider
{
	Vec2f center{};     ///< 中心座標
	float radius{0.0f}; ///< 半径
};

/// @brief AABBコライダー（軸平行バウンディングボックス）
struct AABBCollider
{
	Vec2f min{};  ///< 最小座標（左上）
	Vec2f max{};  ///< 最大座標（右下）

	/// @brief 中心座標を返す
	[[nodiscard]] constexpr Vec2f center() const noexcept
	{
		return {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
	}

	/// @brief ハーフサイズを返す
	[[nodiscard]] constexpr Vec2f halfExtents() const noexcept
	{
		return {(max.x - min.x) * 0.5f, (max.y - min.y) * 0.5f};
	}
};

/// @brief OBBコライダー（有向バウンディングボックス）
struct OBBCollider
{
	Vec2f center{};         ///< 中心座標
	Vec2f halfExtents{};    ///< ハーフサイズ（ローカル軸方向）
	float rotation{0.0f};   ///< 回転角度（ラジアン）
};

/// @brief カプセルコライダー
struct CapsuleCollider
{
	Vec2f pointA{};         ///< 線分の始点
	Vec2f pointB{};         ///< 線分の終点
	float radius{0.0f};     ///< 半径
};

/// @brief 2D接触情報
struct Contact2D
{
	Vec2f point{};          ///< 接触点（ワールド座標）
	Vec2f normal{};         ///< 接触法線（AからBへ向かう方向、正規化済み）
	float penetration{0.0f}; ///< めり込み深さ（正の値）
};

// ── 衝突検出関数 ────────────────────────────────────────

/// @brief 円と円の衝突検出
/// @param a 円A
/// @param b 円B
/// @return 衝突している場合は接触情報、していない場合はnullopt
///
/// @code
/// CircleCollider a{{0, 0}, 5.0f};
/// CircleCollider b{{8, 0}, 5.0f};
/// auto contact = testCircleCircle(a, b);
/// // contact->penetration == 2.0f
/// @endcode
[[nodiscard]] inline std::optional<Contact2D> testCircleCircle(
	const CircleCollider& a, const CircleCollider& b) noexcept
{
	const Vec2f diff = b.center - a.center;
	const float distSq = diff.lengthSquared();
	const float radiusSum = a.radius + b.radius;

	if (distSq >= radiusSum * radiusSum)
	{
		return std::nullopt;
	}

	const float dist = std::sqrt(distSq);

	Contact2D contact;

	if (dist < 1e-6f)
	{
		// 中心が一致 → 任意の法線を使用
		contact.normal = {1.0f, 0.0f};
		contact.penetration = radiusSum;
		contact.point = a.center;
	}
	else
	{
		contact.normal = diff / dist;
		contact.penetration = radiusSum - dist;
		contact.point = a.center + contact.normal * a.radius;
	}

	return contact;
}

/// @brief 円とAABBの衝突検出
/// @param circle 円コライダー
/// @param aabb AABBコライダー
/// @return 衝突している場合は接触情報、していない場合はnullopt
[[nodiscard]] inline std::optional<Contact2D> testCircleAABB(
	const CircleCollider& circle, const AABBCollider& aabb) noexcept
{
	// AABBに最も近い点を求める
	float closestX = circle.center.x;
	if (closestX < aabb.min.x) closestX = aabb.min.x;
	else if (closestX > aabb.max.x) closestX = aabb.max.x;

	float closestY = circle.center.y;
	if (closestY < aabb.min.y) closestY = aabb.min.y;
	else if (closestY > aabb.max.y) closestY = aabb.max.y;

	const Vec2f closest{closestX, closestY};
	const Vec2f diff = circle.center - closest;
	const float distSq = diff.lengthSquared();

	if (distSq >= circle.radius * circle.radius)
	{
		return std::nullopt;
	}

	Contact2D contact;
	const float dist = std::sqrt(distSq);

	if (dist < 1e-6f)
	{
		// 円の中心がAABB内部にある → AABBの各辺への距離で法線を決定
		const float dx1 = circle.center.x - aabb.min.x;
		const float dx2 = aabb.max.x - circle.center.x;
		const float dy1 = circle.center.y - aabb.min.y;
		const float dy2 = aabb.max.y - circle.center.y;

		float minDist = dx1;
		contact.normal = {-1.0f, 0.0f};

		if (dx2 < minDist) { minDist = dx2; contact.normal = {1.0f, 0.0f}; }
		if (dy1 < minDist) { minDist = dy1; contact.normal = {0.0f, -1.0f}; }
		if (dy2 < minDist) { minDist = dy2; contact.normal = {0.0f, 1.0f}; }

		contact.penetration = circle.radius + minDist;
		contact.point = circle.center;
	}
	else
	{
		contact.normal = diff / dist;
		contact.penetration = circle.radius - dist;
		contact.point = closest;
	}

	return contact;
}

/// @brief AABB同士の衝突検出
/// @param a AABB A
/// @param b AABB B
/// @return 衝突している場合は接触情報、していない場合はnullopt
[[nodiscard]] inline std::optional<Contact2D> testAABBAABB(
	const AABBCollider& a, const AABBCollider& b) noexcept
{
	// 各軸のオーバーラップを計算
	const float overlapX1 = a.max.x - b.min.x;
	const float overlapX2 = b.max.x - a.min.x;
	const float overlapY1 = a.max.y - b.min.y;
	const float overlapY2 = b.max.y - a.min.y;

	// いずれかの軸で分離されていれば衝突なし
	if (overlapX1 <= 0.0f || overlapX2 <= 0.0f ||
		overlapY1 <= 0.0f || overlapY2 <= 0.0f)
	{
		return std::nullopt;
	}

	// 各軸の最小オーバーラップと方向を決定
	const float overlapX = (overlapX1 < overlapX2) ? overlapX1 : overlapX2;
	const float signX = (overlapX1 < overlapX2) ? 1.0f : -1.0f;

	const float overlapY = (overlapY1 < overlapY2) ? overlapY1 : overlapY2;
	const float signY = (overlapY1 < overlapY2) ? 1.0f : -1.0f;

	Contact2D contact;

	if (overlapX < overlapY)
	{
		contact.normal = {signX, 0.0f};
		contact.penetration = overlapX;
	}
	else
	{
		contact.normal = {0.0f, signY};
		contact.penetration = overlapY;
	}

	// 接触点はオーバーラップ領域の中心
	const float cx = (std::max(a.min.x, b.min.x) + std::min(a.max.x, b.max.x)) * 0.5f;
	const float cy = (std::max(a.min.y, b.min.y) + std::min(a.max.y, b.max.y)) * 0.5f;
	contact.point = {cx, cy};

	return contact;
}

/// @brief 円とOBBの衝突検出
/// @param circle 円コライダー
/// @param obb OBBコライダー
/// @return 衝突している場合は接触情報、していない場合はnullopt
[[nodiscard]] inline std::optional<Contact2D> testCircleOBB(
	const CircleCollider& circle, const OBBCollider& obb) noexcept
{
	// 円の中心をOBBのローカル座標系に変換
	const float cosR = std::cos(-obb.rotation);
	const float sinR = std::sin(-obb.rotation);
	const Vec2f localCenter
	{
		(circle.center.x - obb.center.x) * cosR - (circle.center.y - obb.center.y) * sinR,
		(circle.center.x - obb.center.x) * sinR + (circle.center.y - obb.center.y) * cosR
	};

	// ローカル座標系でAABBとしてテスト
	float closestX = localCenter.x;
	if (closestX < -obb.halfExtents.x) closestX = -obb.halfExtents.x;
	else if (closestX > obb.halfExtents.x) closestX = obb.halfExtents.x;

	float closestY = localCenter.y;
	if (closestY < -obb.halfExtents.y) closestY = -obb.halfExtents.y;
	else if (closestY > obb.halfExtents.y) closestY = obb.halfExtents.y;

	const Vec2f localClosest{closestX, closestY};
	const Vec2f diff = localCenter - localClosest;
	const float distSq = diff.lengthSquared();

	if (distSq >= circle.radius * circle.radius)
	{
		return std::nullopt;
	}

	const float dist = std::sqrt(distSq);

	Contact2D contact;

	Vec2f localNormal;
	if (dist < 1e-6f)
	{
		// 円の中心がOBB内部 → 各辺への距離で法線決定
		const float dx1 = localCenter.x + obb.halfExtents.x;
		const float dx2 = obb.halfExtents.x - localCenter.x;
		const float dy1 = localCenter.y + obb.halfExtents.y;
		const float dy2 = obb.halfExtents.y - localCenter.y;

		float minDist = dx1;
		localNormal = {-1.0f, 0.0f};

		if (dx2 < minDist) { minDist = dx2; localNormal = {1.0f, 0.0f}; }
		if (dy1 < minDist) { minDist = dy1; localNormal = {0.0f, -1.0f}; }
		if (dy2 < minDist) { minDist = dy2; localNormal = {0.0f, 1.0f}; }

		contact.penetration = circle.radius + minDist;
	}
	else
	{
		localNormal = diff / dist;
		contact.penetration = circle.radius - dist;
	}

	// ローカル法線をワールド座標系に変換
	const float cosF = std::cos(obb.rotation);
	const float sinF = std::sin(obb.rotation);
	contact.normal =
	{
		localNormal.x * cosF - localNormal.y * sinF,
		localNormal.x * sinF + localNormal.y * cosF
	};

	// 接触点をワールド座標系に変換
	contact.point =
	{
		localClosest.x * cosF - localClosest.y * sinF + obb.center.x,
		localClosest.x * sinF + localClosest.y * cosF + obb.center.y
	};

	return contact;
}

// ── 点包含テスト ────────────────────────────────────────

/// @brief 点が円の内部にあるか判定する
/// @param point 判定する点
/// @param circle 円コライダー
/// @return 内部にある場合true
[[nodiscard]] constexpr bool pointInCircle(
	const Vec2f& point, const CircleCollider& circle) noexcept
{
	return point.distanceSquaredTo(circle.center) <= circle.radius * circle.radius;
}

/// @brief 点がAABBの内部にあるか判定する
/// @param point 判定する点
/// @param aabb AABBコライダー
/// @return 内部にある場合true
[[nodiscard]] constexpr bool pointInAABB(
	const Vec2f& point, const AABBCollider& aabb) noexcept
{
	return point.x >= aabb.min.x && point.x <= aabb.max.x
		&& point.y >= aabb.min.y && point.y <= aabb.max.y;
}

} // namespace sgc::physics
