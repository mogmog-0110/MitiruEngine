#pragma once

/// @file RayCast2D.hpp
/// @brief 2Dレイキャスト（AABB・円との交差判定）
///
/// レイ（半直線）とAABBまたは円の交差判定を行い、
/// 衝突点・距離・法線を返すユーティリティ群。
///
/// @code
/// using namespace sgc::physics;
///
/// sgc::Vec2f origin{0.0f, 5.0f};
/// sgc::Vec2f direction{1.0f, 0.0f};
/// sgc::AABB2f box{{3.0f, 0.0f}, {6.0f, 10.0f}};
///
/// auto hit = raycastAABB(origin, direction, box);
/// if (hit.hit)
/// {
///     // hit.point, hit.distance, hit.normal を使用
/// }
/// @endcode

#include <cmath>

#include "sgc/math/Geometry.hpp"

namespace sgc::physics
{

/// @brief レイキャスト結果
/// @tparam T 浮動小数点型
template <FloatingPoint T>
struct RayCastHit2D
{
	bool hit{false};         ///< ヒットしたか
	T distance{};            ///< レイ原点からの距離
	Vec2<T> point{};         ///< 衝突点
	Vec2<T> normal{};        ///< 衝突面の法線
};

/// @brief レイ vs AABB の交差判定（スラブ法）
///
/// レイが各軸のスラブ（帯領域）と交差する区間を計算し、
/// すべての軸で共通する区間があれば衝突とみなす。
///
/// @tparam T 浮動小数点型
/// @param origin レイの始点
/// @param direction レイの方向ベクトル（正規化推奨）
/// @param box 対象のAABB
/// @param maxDistance 最大検出距離
/// @return レイキャスト結果
///
/// @code
/// auto hit = raycastAABB(
///     {0.0f, 5.0f}, {1.0f, 0.0f},
///     {{3.0f, 0.0f}, {6.0f, 10.0f}});
/// // hit.hit == true, hit.distance == 3.0f
/// @endcode
template <FloatingPoint T>
[[nodiscard]] constexpr RayCastHit2D<T> raycastAABB(
	const Vec2<T>& origin, const Vec2<T>& direction,
	const AABB2<T>& box, T maxDistance = static_cast<T>(1e10)) noexcept
{
	RayCastHit2D<T> result;

	// ゼロ方向のチェック用の微小値
	constexpr T EPSILON = static_cast<T>(1e-8);

	T tMin = static_cast<T>(0);
	T tMax = maxDistance;

	// 衝突面の法線を追跡するための変数
	Vec2<T> normalMin{};

	// X軸スラブ
	if (direction.x > -EPSILON && direction.x < EPSILON)
	{
		// レイがX軸に平行 → X範囲外なら交差なし
		if (origin.x < box.min.x || origin.x > box.max.x)
		{
			return result;
		}
	}
	else
	{
		const T invD = T{1} / direction.x;
		T t1 = (box.min.x - origin.x) * invD;
		T t2 = (box.max.x - origin.x) * invD;

		Vec2<T> nNear = {T{-1}, T{0}};

		if (t1 > t2)
		{
			// swap
			const T tmp = t1;
			t1 = t2;
			t2 = tmp;
			nNear = {T{1}, T{0}};
		}

		if (t1 > tMin)
		{
			tMin = t1;
			normalMin = nNear;
		}
		if (t2 < tMax) tMax = t2;

		if (tMin > tMax) return result;
	}

	// Y軸スラブ
	if (direction.y > -EPSILON && direction.y < EPSILON)
	{
		if (origin.y < box.min.y || origin.y > box.max.y)
		{
			return result;
		}
	}
	else
	{
		const T invD = T{1} / direction.y;
		T t1 = (box.min.y - origin.y) * invD;
		T t2 = (box.max.y - origin.y) * invD;

		Vec2<T> nNear = {T{0}, T{-1}};

		if (t1 > t2)
		{
			const T tmp = t1;
			t1 = t2;
			t2 = tmp;
			nNear = {T{0}, T{1}};
		}

		if (t1 > tMin)
		{
			tMin = t1;
			normalMin = nNear;
		}
		if (t2 < tMax) tMax = t2;

		if (tMin > tMax) return result;
	}

	// 衝突あり
	result.hit = true;
	result.distance = tMin;
	result.point = origin + direction * tMin;
	result.normal = normalMin;

	return result;
}

/// @brief レイ vs 円 の交差判定
///
/// レイ原点から円中心へのベクトルを射影し、
/// 判別式を用いて交差判定を行う。
///
/// @tparam T 浮動小数点型
/// @param origin レイの始点
/// @param direction レイの方向ベクトル（正規化推奨）
/// @param circleCenter 円の中心座標
/// @param radius 円の半径
/// @param maxDistance 最大検出距離
/// @return レイキャスト結果
///
/// @code
/// auto hit = raycastCircle(
///     {0.0f, 0.0f}, {1.0f, 0.0f},
///     {5.0f, 0.0f}, 2.0f);
/// // hit.hit == true, hit.distance == 3.0f
/// @endcode
template <FloatingPoint T>
[[nodiscard]] RayCastHit2D<T> raycastCircle(
	const Vec2<T>& origin, const Vec2<T>& direction,
	const Vec2<T>& circleCenter, T radius,
	T maxDistance = static_cast<T>(1e10)) noexcept
{
	RayCastHit2D<T> result;

	// 原点から円中心へのベクトル
	const Vec2<T> oc = origin - circleCenter;

	// 2次方程式の係数: |origin + t*direction - center|^2 = radius^2
	// a*t^2 + 2*b*t + c = 0
	const T a = direction.dot(direction);
	const T b = oc.dot(direction);
	const T c = oc.dot(oc) - radius * radius;

	const T discriminant = b * b - a * c;

	if (discriminant < T{0})
	{
		return result;  // 交差なし
	}

	const T sqrtD = std::sqrt(discriminant);

	// 近い方の交点を試す
	T t = (-b - sqrtD) / a;

	// 近い交点が手前なら遠い交点を試す
	if (t < T{0})
	{
		t = (-b + sqrtD) / a;
	}

	// 有効範囲チェック
	if (t < T{0} || t > maxDistance)
	{
		return result;
	}

	result.hit = true;
	result.distance = t;
	result.point = origin + direction * t;
	result.normal = (result.point - circleCenter).normalized();

	return result;
}

using RayCastHit2Df = RayCastHit2D<float>;

} // namespace sgc::physics
