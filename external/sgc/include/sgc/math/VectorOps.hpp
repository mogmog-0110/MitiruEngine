#pragma once

/// @file VectorOps.hpp
/// @brief Vec2/Vec3用のフリー関数群
///
/// メンバ関数をラップし、より直感的な呼び出しを提供する。
///
/// @code
/// using namespace sgc::math;
/// sgc::Vec2f a{1.0f, 0.0f}, b{0.0f, 1.0f};
/// float d = dot(a, b);          // 0.0f
/// float c = cross(a, b);        // 1.0f
/// auto n = normalize(a);        // {1, 0}
/// @endcode

#include <cmath>

#include "sgc/math/Vec2.hpp"
#include "sgc/math/Vec3.hpp"
#include "sgc/types/Concepts.hpp"

namespace sgc::math
{

// ── Vec2 演算 ──────────────────────────────────────────────────

/// @brief 2Dベクトルの内積を計算する
/// @tparam T 算術型
/// @param a 左辺ベクトル
/// @param b 右辺ベクトル
/// @return 内積の値
template <Arithmetic T>
[[nodiscard]] constexpr T dot(const Vec2<T>& a, const Vec2<T>& b) noexcept
{
	return a.dot(b);
}

/// @brief 2D外積（スカラー値）を計算する
/// @tparam T 算術型
/// @param a 左辺ベクトル
/// @param b 右辺ベクトル
/// @return 外積のZ成分
template <Arithmetic T>
[[nodiscard]] constexpr T cross(const Vec2<T>& a, const Vec2<T>& b) noexcept
{
	return a.cross(b);
}

/// @brief 2点間の距離を計算する
/// @tparam T 浮動小数点型
/// @param a 始点
/// @param b 終点
/// @return 2点間の距離
template <FloatingPoint T>
[[nodiscard]] T distance(const Vec2<T>& a, const Vec2<T>& b) noexcept
{
	return a.distanceTo(b);
}

/// @brief 2点間の距離の二乗を計算する
/// @tparam T 算術型
/// @param a 始点
/// @param b 終点
/// @return 2点間の距離の二乗
template <Arithmetic T>
[[nodiscard]] constexpr T distanceSquared(const Vec2<T>& a, const Vec2<T>& b) noexcept
{
	return a.distanceSquaredTo(b);
}

/// @brief 2Dベクトルを正規化する
/// @tparam T 浮動小数点型
/// @param v 入力ベクトル
/// @return 単位ベクトル（長さ0の場合はゼロベクトル）
template <FloatingPoint T>
[[nodiscard]] Vec2<T> normalize(const Vec2<T>& v)
{
	return v.normalized();
}

/// @brief 2Dベクトルの線形補間を計算する
/// @tparam T 浮動小数点型
/// @param a 始点
/// @param b 終点
/// @param t 補間係数 [0, 1]
/// @return 補間結果
template <FloatingPoint T>
[[nodiscard]] Vec2<T> lerp(const Vec2<T>& a, const Vec2<T>& b, T t) noexcept
{
	return a.lerp(b, t);
}

/// @brief 2Dベクトルを法線で反射する
/// @tparam T 浮動小数点型
/// @param v 入射ベクトル
/// @param normal 反射面の法線（正規化済み）
/// @return 反射後のベクトル
template <FloatingPoint T>
[[nodiscard]] Vec2<T> reflect(const Vec2<T>& v, const Vec2<T>& normal)
{
	return v.reflect(normal);
}

/// @brief 2Dベクトルの垂直ベクトルを返す（反時計回り90度）
/// @tparam T 算術型
/// @param v 入力ベクトル
/// @return 垂直ベクトル
template <Arithmetic T>
[[nodiscard]] constexpr Vec2<T> perpendicular(const Vec2<T>& v) noexcept
{
	return v.perpendicular();
}

// ── Vec3 演算 ──────────────────────────────────────────────────

/// @brief 3Dベクトルの内積を計算する
/// @tparam T 算術型
/// @param a 左辺ベクトル
/// @param b 右辺ベクトル
/// @return 内積の値
template <Arithmetic T>
[[nodiscard]] constexpr T dot(const Vec3<T>& a, const Vec3<T>& b) noexcept
{
	return a.dot(b);
}

/// @brief 3D外積を計算する
/// @tparam T 算術型
/// @param a 左辺ベクトル
/// @param b 右辺ベクトル
/// @return 外積ベクトル
template <Arithmetic T>
[[nodiscard]] constexpr Vec3<T> cross(const Vec3<T>& a, const Vec3<T>& b) noexcept
{
	return a.cross(b);
}

/// @brief 3D空間の2点間の距離を計算する
/// @tparam T 浮動小数点型
/// @param a 始点
/// @param b 終点
/// @return 2点間の距離
template <FloatingPoint T>
[[nodiscard]] T distance(const Vec3<T>& a, const Vec3<T>& b) noexcept
{
	return a.distanceTo(b);
}

/// @brief 3Dベクトルを正規化する
/// @tparam T 浮動小数点型
/// @param v 入力ベクトル
/// @return 単位ベクトル（長さ0の場合はゼロベクトル）
template <FloatingPoint T>
[[nodiscard]] Vec3<T> normalize(const Vec3<T>& v)
{
	return v.normalized();
}

/// @brief 3Dベクトルの線形補間を計算する
/// @tparam T 浮動小数点型
/// @param a 始点
/// @param b 終点
/// @param t 補間係数 [0, 1]
/// @return 補間結果
template <FloatingPoint T>
[[nodiscard]] Vec3<T> lerp(const Vec3<T>& a, const Vec3<T>& b, T t) noexcept
{
	return a.lerp(b, t);
}

/// @brief 3Dベクトルを法線で反射する
/// @tparam T 浮動小数点型
/// @param v 入射ベクトル
/// @param normal 反射面の法線（正規化済み）
/// @return 反射後のベクトル
template <FloatingPoint T>
[[nodiscard]] Vec3<T> reflect(const Vec3<T>& v, const Vec3<T>& normal)
{
	return v.reflect(normal);
}

/// @brief 3Dベクトルの球面線形補間を計算する
/// @tparam T 浮動小数点型
/// @param a 始点（正規化済み推奨）
/// @param b 終点（正規化済み推奨）
/// @param t 補間係数 [0, 1]
/// @return 球面線形補間結果
template <FloatingPoint T>
[[nodiscard]] Vec3<T> slerp(const Vec3<T>& a, const Vec3<T>& b, T t)
{
	T d = a.dot(b);

	// クランプ
	if (d > T{1}) d = T{1};
	if (d < T{-1}) d = T{-1};

	const T theta = std::acos(d);

	// ほぼ平行な場合はlerpにフォールバック
	if (theta < T{1e-6})
	{
		return a.lerp(b, t);
	}

	const T sinTheta = std::sin(theta);
	const T wa = std::sin((T{1} - t) * theta) / sinTheta;
	const T wb = std::sin(t * theta) / sinTheta;

	return Vec3<T>{
		a.x * wa + b.x * wb,
		a.y * wa + b.y * wb,
		a.z * wa + b.z * wb
	};
}

} // namespace sgc::math
