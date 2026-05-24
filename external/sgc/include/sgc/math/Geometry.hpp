#pragma once

/// @file Geometry.hpp
/// @brief 幾何学プリミティブ（AABB, 円, 線分）と交差判定

#include <numbers>

#include "sgc/math/Vec2.hpp"
#include "sgc/math/Vec3.hpp"

namespace sgc
{

/// @brief 2D軸平行バウンディングボックス
/// @tparam T 浮動小数点型
template <FloatingPoint T>
struct AABB2
{
	Vec2<T> min{};  ///< 最小座標（左上）
	Vec2<T> max{};  ///< 最大座標（右下）

	/// @brief 中心座標を返す
	[[nodiscard]] constexpr Vec2<T> center() const noexcept
	{
		return {(min.x + max.x) / T{2}, (min.y + max.y) / T{2}};
	}

	/// @brief サイズ（幅と高さ）を返す
	[[nodiscard]] constexpr Vec2<T> size() const noexcept
	{
		return {max.x - min.x, max.y - min.y};
	}

	/// @brief 面積を返す
	[[nodiscard]] constexpr T area() const noexcept
	{
		return (max.x - min.x) * (max.y - min.y);
	}

	/// @brief 点が内部に含まれるか判定する
	[[nodiscard]] constexpr bool contains(const Vec2<T>& point) const noexcept
	{
		return point.x >= min.x && point.x <= max.x
			&& point.y >= min.y && point.y <= max.y;
	}

	/// @brief 他のAABBと交差しているか判定する
	[[nodiscard]] constexpr bool intersects(const AABB2& other) const noexcept
	{
		return min.x <= other.max.x && max.x >= other.min.x
			&& min.y <= other.max.y && max.y >= other.min.y;
	}
};

/// @brief 2D円
/// @tparam T 浮動小数点型
template <FloatingPoint T>
struct Circle
{
	Vec2<T> center{};  ///< 中心座標
	T radius{};        ///< 半径

	/// @brief 面積を返す
	[[nodiscard]] T area() const noexcept
	{
		constexpr T pi = std::numbers::pi_v<T>;
		return pi * radius * radius;
	}

	/// @brief 点が内部に含まれるか判定する
	[[nodiscard]] constexpr bool contains(const Vec2<T>& point) const noexcept
	{
		return center.distanceSquaredTo(point) <= radius * radius;
	}

	/// @brief 他の円と交差しているか判定する
	[[nodiscard]] constexpr bool intersects(const Circle& other) const noexcept
	{
		const T sumR = radius + other.radius;
		return center.distanceSquaredTo(other.center) <= sumR * sumR;
	}

	/// @brief AABBと交差しているか判定する
	[[nodiscard]] constexpr bool intersects(const AABB2<T>& box) const noexcept
	{
		T closestX = center.x;
		if (closestX < box.min.x) closestX = box.min.x;
		else if (closestX > box.max.x) closestX = box.max.x;

		T closestY = center.y;
		if (closestY < box.min.y) closestY = box.min.y;
		else if (closestY > box.max.y) closestY = box.max.y;

		const Vec2<T> closest{closestX, closestY};
		return center.distanceSquaredTo(closest) <= radius * radius;
	}
};

/// @brief 2Dレイ（半直線）
/// @tparam T 浮動小数点型
template <FloatingPoint T>
struct Ray2
{
	Vec2<T> origin{};     ///< 始点
	Vec2<T> direction{};  ///< 方向（正規化済みを推奨）

	/// @brief レイ上の指定距離の点を返す
	/// @param t 距離パラメータ
	[[nodiscard]] constexpr Vec2<T> pointAt(T t) const noexcept
	{
		return origin + direction * t;
	}
};

/// @brief 3D軸平行バウンディングボックス
/// @tparam T 浮動小数点型
template <FloatingPoint T>
struct AABB3
{
	Vec3<T> min{};  ///< 最小座標
	Vec3<T> max{};  ///< 最大座標

	/// @brief 中心座標を返す
	[[nodiscard]] constexpr Vec3<T> center() const noexcept
	{
		return {
			(min.x + max.x) / T{2},
			(min.y + max.y) / T{2},
			(min.z + max.z) / T{2}
		};
	}

	/// @brief 点が内部に含まれるか判定する
	[[nodiscard]] constexpr bool contains(const Vec3<T>& point) const noexcept
	{
		return point.x >= min.x && point.x <= max.x
			&& point.y >= min.y && point.y <= max.y
			&& point.z >= min.z && point.z <= max.z;
	}

	/// @brief 他のAABBと交差しているか判定する
	[[nodiscard]] constexpr bool intersects(const AABB3& other) const noexcept
	{
		return min.x <= other.max.x && max.x >= other.min.x
			&& min.y <= other.max.y && max.y >= other.min.y
			&& min.z <= other.max.z && max.z >= other.min.z;
	}
};

/// @brief 3D球
/// @tparam T 浮動小数点型
template <FloatingPoint T>
struct Sphere
{
	Vec3<T> center{};  ///< 中心座標
	T radius{};        ///< 半径

	/// @brief 点が内部に含まれるか判定する
	[[nodiscard]] constexpr bool contains(const Vec3<T>& point) const noexcept
	{
		return (point - center).lengthSquared() <= radius * radius;
	}

	/// @brief 他の球と交差しているか判定する
	[[nodiscard]] constexpr bool intersects(const Sphere& other) const noexcept
	{
		const T sumR = radius + other.radius;
		return (center - other.center).lengthSquared() <= sumR * sumR;
	}
};

// ── エイリアス ──────────────────────────────────────────────────

using AABB2f = AABB2<float>;     ///< float版 AABB2
using AABB2d = AABB2<double>;    ///< double版 AABB2
using Circlef = Circle<float>;   ///< float版 Circle
using Circled = Circle<double>;  ///< double版 Circle
using Ray2f = Ray2<float>;       ///< float版 Ray2
using Ray2d = Ray2<double>;      ///< double版 Ray2
using AABB3f = AABB3<float>;     ///< float版 AABB3
using AABB3d = AABB3<double>;    ///< double版 AABB3
using Spheref = Sphere<float>;   ///< float版 Sphere
using Sphered = Sphere<double>;  ///< double版 Sphere

} // namespace sgc
