#pragma once

/// @file Vec3.hpp
/// @brief 3次元ベクトル型 Vec3<T>

#include <cmath>

#include "sgc/types/Concepts.hpp"

namespace sgc
{

/// @brief 3次元ベクトル
/// @tparam T 算術型（int, float, double等）
template <Arithmetic T>
struct Vec3
{
	T x{};  ///< X成分
	T y{};  ///< Y成分
	T z{};  ///< Z成分

	constexpr Vec3() noexcept = default;
	constexpr Vec3(T x, T y, T z) noexcept : x(x), y(y), z(z) {}
	constexpr explicit Vec3(T value) noexcept : x(value), y(value), z(value) {}

	template <Arithmetic U>
	constexpr explicit Vec3(const Vec3<U>& other) noexcept
		: x(static_cast<T>(other.x)), y(static_cast<T>(other.y)), z(static_cast<T>(other.z)) {}

	// ── 算術演算子 ──────────────────────────────────────────

	[[nodiscard]] constexpr Vec3 operator+(const Vec3& rhs) const noexcept { return {x + rhs.x, y + rhs.y, z + rhs.z}; }
	[[nodiscard]] constexpr Vec3 operator-(const Vec3& rhs) const noexcept { return {x - rhs.x, y - rhs.y, z - rhs.z}; }
	[[nodiscard]] constexpr Vec3 operator*(const Vec3& rhs) const noexcept { return {x * rhs.x, y * rhs.y, z * rhs.z}; }
	[[nodiscard]] constexpr Vec3 operator/(const Vec3& rhs) const noexcept { return {x / rhs.x, y / rhs.y, z / rhs.z}; }
	[[nodiscard]] constexpr Vec3 operator*(T s) const noexcept { return {x * s, y * s, z * s}; }
	[[nodiscard]] constexpr Vec3 operator/(T s) const noexcept { return {x / s, y / s, z / s}; }
	[[nodiscard]] constexpr Vec3 operator-() const noexcept { return {-x, -y, -z}; }

	constexpr Vec3& operator+=(const Vec3& rhs) noexcept { x += rhs.x; y += rhs.y; z += rhs.z; return *this; }
	constexpr Vec3& operator-=(const Vec3& rhs) noexcept { x -= rhs.x; y -= rhs.y; z -= rhs.z; return *this; }
	constexpr Vec3& operator*=(T s) noexcept { x *= s; y *= s; z *= s; return *this; }
	constexpr Vec3& operator/=(T s) noexcept { x /= s; y /= s; z /= s; return *this; }

	[[nodiscard]] constexpr bool operator==(const Vec3& rhs) const noexcept = default;

	// ── ベクトル演算 ────────────────────────────────────────

	/// @brief 内積を計算する
	[[nodiscard]] constexpr T dot(const Vec3& rhs) const noexcept
	{
		return x * rhs.x + y * rhs.y + z * rhs.z;
	}

	/// @brief 外積を計算する
	/// @param rhs 右辺ベクトル
	/// @return 外積ベクトル
	[[nodiscard]] constexpr Vec3 cross(const Vec3& rhs) const noexcept
	{
		return {
			y * rhs.z - z * rhs.y,
			z * rhs.x - x * rhs.z,
			x * rhs.y - y * rhs.x
		};
	}

	/// @brief 長さの二乗を返す
	[[nodiscard]] constexpr T lengthSquared() const noexcept { return x * x + y * y + z * z; }

	/// @brief 長さを返す
	[[nodiscard]] T length() const noexcept requires FloatingPoint<T> { return std::sqrt(lengthSquared()); }

	/// @brief 正規化したベクトルを返す
	[[nodiscard]] Vec3 normalized() const noexcept requires FloatingPoint<T>
	{
		const T len = length();
		if (len == T{0}) return {};
		return *this / len;
	}

	/// @brief 他のベクトルとの距離を返す
	[[nodiscard]] T distanceTo(const Vec3& other) const noexcept requires FloatingPoint<T>
	{
		return (*this - other).length();
	}

	/// @brief 他のベクトルとの距離の二乗を返す
	/// @param other 対象ベクトル
	/// @return 2点間の距離の二乗
	[[nodiscard]] constexpr T distanceSquaredTo(const Vec3& other) const noexcept
	{
		return (*this - other).lengthSquared();
	}

	/// @brief 法線で反射したベクトルを返す
	[[nodiscard]] constexpr Vec3 reflect(const Vec3& normal) const noexcept requires FloatingPoint<T>
	{
		return *this - normal * (T{2} * this->dot(normal));
	}

	// ── 追加メソッド ────────────────────────────────────────

	/// @brief 各成分を[min, max]にクランプした新しいベクトルを返す
	[[nodiscard]] constexpr Vec3 clamped(const Vec3& minVal, const Vec3& maxVal) const noexcept
	{
		auto clampVal = [](T v, T lo, T hi) constexpr -> T
		{
			if (v < lo) return lo;
			if (v > hi) return hi;
			return v;
		};
		return {clampVal(x, minVal.x, maxVal.x), clampVal(y, minVal.y, maxVal.y), clampVal(z, minVal.z, maxVal.z)};
	}

	/// @brief 他のベクトルとの線形補間を返す
	[[nodiscard]] constexpr Vec3 lerp(const Vec3& other, T t) const noexcept
		requires FloatingPoint<T>
	{
		return {x + (other.x - x) * t, y + (other.y - y) * t, z + (other.z - z) * t};
	}

	/// @brief 他のベクトルへの角度を返す（ラジアン）
	[[nodiscard]] T angleTo(const Vec3& other) const noexcept
		requires FloatingPoint<T>
	{
		const T d = this->dot(other);
		const T lenProd = this->length() * other.length();
		if (lenProd == T{0}) return T{0};
		// acos引数を[-1,1]にクランプ
		T cosAngle = d / lenProd;
		if (cosAngle > T{1}) cosAngle = T{1};
		if (cosAngle < T{-1}) cosAngle = T{-1};
		return std::acos(cosAngle);
	}

	/// @brief このベクトルをontoに射影したベクトルを返す
	[[nodiscard]] constexpr Vec3 projectOnto(const Vec3& onto) const noexcept
		requires FloatingPoint<T>
	{
		const T d = onto.dot(onto);
		if (d == T{0}) return {};
		return onto * (this->dot(onto) / d);
	}

	/// @brief 指定軸まわりにラジアン回転したベクトルを返す（Rodrigues公式）
	/// @param axis 回転軸（正規化済み）
	/// @param radians 回転角度（ラジアン）
	[[nodiscard]] Vec3 rotateAroundAxis(const Vec3& axis, T radians) const noexcept
		requires FloatingPoint<T>
	{
		const T cosA = std::cos(radians);
		const T sinA = std::sin(radians);
		return *this * cosA + axis.cross(*this) * sinA + axis * axis.dot(*this) * (T{1} - cosA);
	}

	/// @brief 2つのベクトルの各成分の最小値を返す
	[[nodiscard]] static constexpr Vec3 min(const Vec3& a, const Vec3& b) noexcept
	{
		return {(a.x < b.x) ? a.x : b.x, (a.y < b.y) ? a.y : b.y, (a.z < b.z) ? a.z : b.z};
	}

	/// @brief 2つのベクトルの各成分の最大値を返す
	[[nodiscard]] static constexpr Vec3 max(const Vec3& a, const Vec3& b) noexcept
	{
		return {(a.x > b.x) ? a.x : b.x, (a.y > b.y) ? a.y : b.y, (a.z > b.z) ? a.z : b.z};
	}

	// ── 定数 ────────────────────────────────────────────────

	[[nodiscard]] static constexpr Vec3 zero() noexcept { return {T{0}, T{0}, T{0}}; }
	[[nodiscard]] static constexpr Vec3 one() noexcept { return {T{1}, T{1}, T{1}}; }
	[[nodiscard]] static constexpr Vec3 unitX() noexcept { return {T{1}, T{0}, T{0}}; }
	[[nodiscard]] static constexpr Vec3 unitY() noexcept { return {T{0}, T{1}, T{0}}; }
	[[nodiscard]] static constexpr Vec3 unitZ() noexcept { return {T{0}, T{0}, T{1}}; }

	/// @brief 上方向 (0, 1, 0)
	[[nodiscard]] static constexpr Vec3 up() noexcept { return {T{0}, T{1}, T{0}}; }

	/// @brief 下方向 (0, -1, 0)
	[[nodiscard]] static constexpr Vec3 down() noexcept { return {T{0}, T{-1}, T{0}}; }

	/// @brief 左方向 (-1, 0, 0)
	[[nodiscard]] static constexpr Vec3 left() noexcept { return {T{-1}, T{0}, T{0}}; }

	/// @brief 右方向 (1, 0, 0)
	[[nodiscard]] static constexpr Vec3 right() noexcept { return {T{1}, T{0}, T{0}}; }

	/// @brief 前方向 (0, 0, -1) ※右手座標系
	[[nodiscard]] static constexpr Vec3 forward() noexcept { return {T{0}, T{0}, T{-1}}; }

	/// @brief 後方向 (0, 0, 1) ※右手座標系
	[[nodiscard]] static constexpr Vec3 back() noexcept { return {T{0}, T{0}, T{1}}; }

	// ── シリアライズ ────────────────────────────────────────

	/// @brief シリアライズ（const版）
	template <typename V>
	void visit(V& v) const { v.write("x", x); v.write("y", y); v.write("z", z); }

	/// @brief デシリアライズ（非const版）
	template <typename V>
	void visit(V& v) { v.read("x", x); v.read("y", y); v.read("z", z); }
};

template <Arithmetic T>
[[nodiscard]] constexpr Vec3<T> operator*(T scalar, const Vec3<T>& v) noexcept { return v * scalar; }

using Vec3f = Vec3<float>;    ///< float版 Vec3
using Vec3d = Vec3<double>;   ///< double版 Vec3
using Vec3i = Vec3<int>;      ///< int版 Vec3

} // namespace sgc
