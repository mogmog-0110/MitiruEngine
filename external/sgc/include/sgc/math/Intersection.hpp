#pragma once

/// @file Intersection.hpp
/// @brief レイと幾何学プリミティブの交差判定
///
/// 各関数はレイパラメータ t を optional で返す。
/// t >= 0 であれば交差点は ray.pointAt(t) で取得できる。

#include <cmath>
#include <limits>
#include <optional>

#include "sgc/math/Geometry.hpp"
#include "sgc/math/Plane.hpp"
#include "sgc/math/Ray3.hpp"

namespace sgc
{

/// @brief 交差判定関数をまとめた名前空間
namespace intersection
{

namespace detail
{

/// @brief a t^2 + 2 halfB t + c = 0 の非負の最小根 (discriminant = halfB^2 - ac >= 0 を渡す)
/// @note 2 根を q/a と c/q で求め、-halfB ± sqrt の引き算による桁落ちを避ける
template <FloatingPoint T>
[[nodiscard]] std::optional<T> nearestNonNegativeRoot(T a, T halfB, T c, T discriminant) noexcept
{
	const T q = -halfB - std::copysign(std::sqrt(discriminant), halfB);
	if (q == T{0}) return T{0};
	T t1 = c / q;
	T t2 = q / a;
	if (t1 > t2) { const T tmp = t1; t1 = t2; t2 = tmp; }
	if (t1 >= T{0}) return t1;
	if (t2 >= T{0}) return t2;
	return std::nullopt;
}

/// @brief いずれかの成分が NaN か
/// @note スラブ法の比較は NaN で全て false になり「外れ」の早期 return を素通りするため、入口で弾く
template <FloatingPoint T>
[[nodiscard]] constexpr bool hasNaN(const Vec3<T>& v) noexcept
{
	return v.x != v.x || v.y != v.y || v.z != v.z;
}

template <FloatingPoint T>
[[nodiscard]] constexpr bool hasNaN(const Vec2<T>& v) noexcept
{
	return v.x != v.x || v.y != v.y;
}

} // namespace detail

/// @brief Ray3 と Plane の交差判定
/// @param ray 3Dレイ
/// @param plane 平面
/// @return 交差する場合はレイパラメータ t を返す
/// @note t < 0 の場合、交差点はレイの背後
[[nodiscard]] inline constexpr std::optional<float> ray3VsPlane(
	const Ray3<float>& ray, const Plane<float>& plane) noexcept
{
	const float denom = plane.normal.dot(ray.direction);
	if (denom == 0.0f) return std::nullopt;
	const float t = -(plane.normal.dot(ray.origin) + plane.distance) / denom;
	return t;
}

/// @brief Ray3 と Plane の交差判定（double版）
[[nodiscard]] inline constexpr std::optional<double> ray3VsPlane(
	const Ray3<double>& ray, const Plane<double>& plane) noexcept
{
	const double denom = plane.normal.dot(ray.direction);
	if (denom == 0.0) return std::nullopt;
	const double t = -(plane.normal.dot(ray.origin) + plane.distance) / denom;
	return t;
}

/// @brief Ray3 と Sphere の交差判定
/// @param ray 3Dレイ
/// @param sphere 球
/// @return 最も近い交差点のレイパラメータ t（t >= 0）
template <FloatingPoint T>
[[nodiscard]] std::optional<T> ray3VsSphere(
	const Ray3<T>& ray, const Sphere<T>& sphere) noexcept
{
	const Vec3<T> oc = ray.origin - sphere.center;
	const T a = ray.direction.dot(ray.direction);
	const T halfB = oc.dot(ray.direction);
	const T r2 = sphere.radius * sphere.radius;
	// b^2 - 4ac は |oc| が半径より大きいほど桁落ちするので、最接近点までの垂直成分から判別式を作る
	const Vec3<T> perp = oc - ray.direction * (halfB / a);
	const T discriminant = a * (r2 - perp.dot(perp));
	if (discriminant < T{0}) return std::nullopt;
	return detail::nearestNonNegativeRoot(a, halfB, oc.dot(oc) - r2, discriminant);
}

/// @brief Ray3 と AABB3 の交差判定（Slab法）
/// @param ray 3Dレイ
/// @param aabb 3D AABB
/// @return 最も近い交差点のレイパラメータ t（t >= 0）
template <FloatingPoint T>
[[nodiscard]] std::optional<T> ray3VsAABB3(
	const Ray3<T>& ray, const AABB3<T>& aabb) noexcept
{
	if (detail::hasNaN(ray.origin) || detail::hasNaN(ray.direction)
		|| detail::hasNaN(aabb.min) || detail::hasNaN(aabb.max))
		return std::nullopt;

	T tmin = T{0};
	T tmax = std::numeric_limits<T>::max();

	// X軸スラブ
	if (ray.direction.x != T{0})
	{
		const T invD = T{1} / ray.direction.x;
		T t0 = (aabb.min.x - ray.origin.x) * invD;
		T t1 = (aabb.max.x - ray.origin.x) * invD;
		if (t0 > t1) { const T tmp = t0; t0 = t1; t1 = tmp; }
		if (t0 > tmin) tmin = t0;
		if (t1 < tmax) tmax = t1;
		if (tmin > tmax) return std::nullopt;
	}
	else
	{
		if (ray.origin.x < aabb.min.x || ray.origin.x > aabb.max.x)
			return std::nullopt;
	}

	// Y軸スラブ
	if (ray.direction.y != T{0})
	{
		const T invD = T{1} / ray.direction.y;
		T t0 = (aabb.min.y - ray.origin.y) * invD;
		T t1 = (aabb.max.y - ray.origin.y) * invD;
		if (t0 > t1) { const T tmp = t0; t0 = t1; t1 = tmp; }
		if (t0 > tmin) tmin = t0;
		if (t1 < tmax) tmax = t1;
		if (tmin > tmax) return std::nullopt;
	}
	else
	{
		if (ray.origin.y < aabb.min.y || ray.origin.y > aabb.max.y)
			return std::nullopt;
	}

	// Z軸スラブ
	if (ray.direction.z != T{0})
	{
		const T invD = T{1} / ray.direction.z;
		T t0 = (aabb.min.z - ray.origin.z) * invD;
		T t1 = (aabb.max.z - ray.origin.z) * invD;
		if (t0 > t1) { const T tmp = t0; t0 = t1; t1 = tmp; }
		if (t0 > tmin) tmin = t0;
		if (t1 < tmax) tmax = t1;
		if (tmin > tmax) return std::nullopt;
	}
	else
	{
		if (ray.origin.z < aabb.min.z || ray.origin.z > aabb.max.z)
			return std::nullopt;
	}

	return tmin;
}

/// @brief Ray3 と三角形の交差判定（Moller-Trumbore法）
/// @param ray 3Dレイ
/// @param v0 三角形の頂点0
/// @param v1 三角形の頂点1
/// @param v2 三角形の頂点2
/// @return 交差する場合はレイパラメータ t（t >= 0）
template <FloatingPoint T>
[[nodiscard]] std::optional<T> ray3VsTriangle(
	const Ray3<T>& ray,
	const Vec3<T>& v0, const Vec3<T>& v1, const Vec3<T>& v2) noexcept
{
	const Vec3<T> e1 = v1 - v0;
	const Vec3<T> e2 = v2 - v0;
	const Vec3<T> h = ray.direction.cross(e2);
	const T a = e1.dot(h);

	// a は辺長の 2 乗とレイ方向の長さに比例するので、平行判定は |e1||h| に対する比で行う
	// (絶対値の閾値だと小さな三角形を正面から撃っても「平行」になる)
	constexpr T relEpsilon = std::numeric_limits<T>::epsilon() * T{4};
	if (a * a <= relEpsilon * relEpsilon * e1.lengthSquared() * h.lengthSquared()) return std::nullopt;

	const T f = T{1} / a;
	const Vec3<T> s = ray.origin - v0;
	const T u = f * s.dot(h);

	// 「範囲内でなければ外れ」の形で比べ、NaN (比較が全て false) も外れにする
	if (!(u >= T{0} && u <= T{1})) return std::nullopt;

	const Vec3<T> q = s.cross(e1);
	const T v = f * ray.direction.dot(q);

	if (!(v >= T{0} && u + v <= T{1})) return std::nullopt;

	const T t = f * e2.dot(q);
	if (!(t >= T{0})) return std::nullopt;

	return t;
}

/// @brief Ray2 と AABB2 の交差判定
/// @param ray 2Dレイ
/// @param aabb 2D AABB
/// @return 最も近い交差点のレイパラメータ t（t >= 0）
template <FloatingPoint T>
[[nodiscard]] std::optional<T> ray2VsAABB2(
	const Ray2<T>& ray, const AABB2<T>& aabb) noexcept
{
	if (detail::hasNaN(ray.origin) || detail::hasNaN(ray.direction)
		|| detail::hasNaN(aabb.min) || detail::hasNaN(aabb.max))
		return std::nullopt;

	T tmin = T{0};
	T tmax = std::numeric_limits<T>::max();

	// X軸
	if (ray.direction.x != T{0})
	{
		const T invD = T{1} / ray.direction.x;
		T t0 = (aabb.min.x - ray.origin.x) * invD;
		T t1 = (aabb.max.x - ray.origin.x) * invD;
		if (t0 > t1) { const T tmp = t0; t0 = t1; t1 = tmp; }
		if (t0 > tmin) tmin = t0;
		if (t1 < tmax) tmax = t1;
		if (tmin > tmax) return std::nullopt;
	}
	else
	{
		if (ray.origin.x < aabb.min.x || ray.origin.x > aabb.max.x)
			return std::nullopt;
	}

	// Y軸
	if (ray.direction.y != T{0})
	{
		const T invD = T{1} / ray.direction.y;
		T t0 = (aabb.min.y - ray.origin.y) * invD;
		T t1 = (aabb.max.y - ray.origin.y) * invD;
		if (t0 > t1) { const T tmp = t0; t0 = t1; t1 = tmp; }
		if (t0 > tmin) tmin = t0;
		if (t1 < tmax) tmax = t1;
		if (tmin > tmax) return std::nullopt;
	}
	else
	{
		if (ray.origin.y < aabb.min.y || ray.origin.y > aabb.max.y)
			return std::nullopt;
	}

	return tmin;
}

/// @brief Ray2 と Circle の交差判定
/// @param ray 2Dレイ
/// @param circle 円
/// @return 最も近い交差点のレイパラメータ t（t >= 0）
template <FloatingPoint T>
[[nodiscard]] std::optional<T> ray2VsCircle(
	const Ray2<T>& ray, const Circle<T>& circle) noexcept
{
	const Vec2<T> oc = ray.origin - circle.center;
	const T a = ray.direction.dot(ray.direction);
	const T halfB = oc.dot(ray.direction);
	const T r2 = circle.radius * circle.radius;
	const Vec2<T> perp = oc - ray.direction * (halfB / a);
	const T discriminant = a * (r2 - perp.dot(perp));
	if (discriminant < T{0}) return std::nullopt;
	return detail::nearestNonNegativeRoot(a, halfB, oc.dot(oc) - r2, discriminant);
}

/// @brief Sphere と AABB3 の交差判定
/// @param sphere 球
/// @param aabb 3D AABB
/// @return 交差している場合 true
template <FloatingPoint T>
[[nodiscard]] constexpr bool sphereVsAABB3(
	const Sphere<T>& sphere, const AABB3<T>& aabb) noexcept
{
	// AABBに最も近い点を計算
	auto clampAxis = [](T val, T minV, T maxV) -> T
	{
		if (val < minV) return minV;
		if (val > maxV) return maxV;
		return val;
	};

	const Vec3<T> closest{
		clampAxis(sphere.center.x, aabb.min.x, aabb.max.x),
		clampAxis(sphere.center.y, aabb.min.y, aabb.max.y),
		clampAxis(sphere.center.z, aabb.min.z, aabb.max.z)
	};

	return (sphere.center - closest).lengthSquared() <= sphere.radius * sphere.radius;
}

} // namespace intersection
} // namespace sgc
