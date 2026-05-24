#pragma once

/// @file Plane.hpp
/// @brief 3D平面型 Plane<T>
///
/// 法線ベクトルと原点からの距離で定義される無限平面。
/// 点の分類やレイとの交差判定に使用する。

#include "sgc/math/Vec3.hpp"

namespace sgc
{

/// @brief 点と平面の位置関係
enum class PlaneClassification
{
	Front,  ///< 法線の正方向側
	Back,   ///< 法線の負方向側
	On      ///< 平面上
};

/// @brief 3D平面（法線 + 原点からの距離）
/// @tparam T 浮動小数点型
///
/// 平面方程式: dot(normal, point) + distance = 0
///
/// @code
/// auto plane = sgc::Planef::fromNormalAndPoint({0,1,0}, {0,5,0});
/// float d = plane.signedDistanceTo({0, 10, 0});  // = 5
/// @endcode
template <FloatingPoint T>
struct Plane
{
	Vec3<T> normal{T{0}, T{1}, T{0}};  ///< 法線ベクトル（正規化済みを推奨）
	T distance{};                        ///< 原点から平面までの符号付き距離

	/// @brief デフォルトコンストラクタ
	constexpr Plane() noexcept = default;

	/// @brief 法線と距離を指定して構築する
	constexpr Plane(const Vec3<T>& normal, T distance) noexcept
		: normal(normal), distance(distance) {}

	[[nodiscard]] constexpr bool operator==(const Plane& rhs) const noexcept = default;

	// ── ファクトリ関数 ──────────────────────────────────────

	/// @brief 3点から平面を構築する
	/// @param a 第1点
	/// @param b 第2点
	/// @param c 第3点
	/// @return 3点を含む平面
	[[nodiscard]] static Plane fromThreePoints(
		const Vec3<T>& a, const Vec3<T>& b, const Vec3<T>& c) noexcept
	{
		const Vec3<T> n = (b - a).cross(c - a).normalized();
		return {n, -n.dot(a)};
	}

	/// @brief 法線と平面上の1点から構築する
	/// @param normal 法線ベクトル（正規化済み）
	/// @param point 平面上の点
	[[nodiscard]] static constexpr Plane fromNormalAndPoint(
		const Vec3<T>& normal, const Vec3<T>& point) noexcept
	{
		return {normal, -normal.dot(point)};
	}

	// ── 演算 ────────────────────────────────────────────────

	/// @brief 点までの符号付き距離を返す
	/// @param point 対象の点
	/// @return 正: 法線方向側、負: 法線の裏側、0: 平面上
	[[nodiscard]] constexpr T signedDistanceTo(const Vec3<T>& point) const noexcept
	{
		return normal.dot(point) + distance;
	}

	/// @brief 平面上の最近接点を返す
	/// @param point 対象の点
	[[nodiscard]] constexpr Vec3<T> closestPoint(const Vec3<T>& point) const noexcept
	{
		return point - normal * signedDistanceTo(point);
	}

	/// @brief 点の平面に対する位置を分類する
	/// @param point 対象の点
	/// @param epsilon 判定の許容誤差
	[[nodiscard]] PlaneClassification classify(
		const Vec3<T>& point, T epsilon = T{1e-6}) const noexcept
	{
		const T d = signedDistanceTo(point);
		if (d > epsilon) return PlaneClassification::Front;
		if (d < -epsilon) return PlaneClassification::Back;
		return PlaneClassification::On;
	}
};

// ── エイリアス ──────────────────────────────────────────────────

using Planef = Plane<float>;    ///< float版 Plane
using Planed = Plane<double>;   ///< double版 Plane

} // namespace sgc
