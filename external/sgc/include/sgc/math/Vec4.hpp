#pragma once

/// @file Vec4.hpp
/// @brief 4次元ベクトル型 Vec4<T>（同次座標、色表現等に使用）

#include <cmath>

#include "sgc/types/Concepts.hpp"

namespace sgc
{

/// @brief 4次元ベクトル
/// @tparam T 算術型
template <Arithmetic T>
struct Vec4
{
	T x{};  ///< X成分
	T y{};  ///< Y成分
	T z{};  ///< Z成分
	T w{};  ///< W成分

	constexpr Vec4() noexcept = default;
	constexpr Vec4(T x, T y, T z, T w) noexcept : x(x), y(y), z(z), w(w) {}
	constexpr explicit Vec4(T value) noexcept : x(value), y(value), z(value), w(value) {}

	template <Arithmetic U>
	constexpr explicit Vec4(const Vec4<U>& other) noexcept
		: x(static_cast<T>(other.x)), y(static_cast<T>(other.y))
		, z(static_cast<T>(other.z)), w(static_cast<T>(other.w)) {}

	// ── 算術演算子 ──────────────────────────────────────────

	[[nodiscard]] constexpr Vec4 operator+(const Vec4& r) const noexcept { return {x+r.x, y+r.y, z+r.z, w+r.w}; }
	[[nodiscard]] constexpr Vec4 operator-(const Vec4& r) const noexcept { return {x-r.x, y-r.y, z-r.z, w-r.w}; }
	[[nodiscard]] constexpr Vec4 operator*(const Vec4& r) const noexcept { return {x*r.x, y*r.y, z*r.z, w*r.w}; }
	[[nodiscard]] constexpr Vec4 operator/(const Vec4& r) const noexcept { return {x/r.x, y/r.y, z/r.z, w/r.w}; }
	[[nodiscard]] constexpr Vec4 operator*(T s) const noexcept { return {x*s, y*s, z*s, w*s}; }
	[[nodiscard]] constexpr Vec4 operator/(T s) const noexcept { return {x/s, y/s, z/s, w/s}; }
	[[nodiscard]] constexpr Vec4 operator-() const noexcept { return {-x, -y, -z, -w}; }

	constexpr Vec4& operator+=(const Vec4& r) noexcept { x+=r.x; y+=r.y; z+=r.z; w+=r.w; return *this; }
	constexpr Vec4& operator-=(const Vec4& r) noexcept { x-=r.x; y-=r.y; z-=r.z; w-=r.w; return *this; }
	constexpr Vec4& operator*=(T s) noexcept { x*=s; y*=s; z*=s; w*=s; return *this; }
	constexpr Vec4& operator/=(T s) noexcept { x/=s; y/=s; z/=s; w/=s; return *this; }

	[[nodiscard]] constexpr bool operator==(const Vec4& rhs) const noexcept = default;

	// ── ベクトル演算 ────────────────────────────────────────

	/// @brief 内積を計算する
	[[nodiscard]] constexpr T dot(const Vec4& rhs) const noexcept
	{
		return x * rhs.x + y * rhs.y + z * rhs.z + w * rhs.w;
	}

	/// @brief 長さの二乗を返す
	[[nodiscard]] constexpr T lengthSquared() const noexcept { return dot(*this); }

	/// @brief 長さを返す
	[[nodiscard]] T length() const noexcept requires FloatingPoint<T> { return std::sqrt(lengthSquared()); }

	/// @brief 正規化したベクトルを返す
	[[nodiscard]] Vec4 normalized() const noexcept requires FloatingPoint<T>
	{
		const T len = length();
		if (len == T{0}) return {};
		return *this / len;
	}

	// ── 定数 ────────────────────────────────────────────────

	[[nodiscard]] static constexpr Vec4 zero() noexcept { return {T{0}, T{0}, T{0}, T{0}}; }
	[[nodiscard]] static constexpr Vec4 one() noexcept { return {T{1}, T{1}, T{1}, T{1}}; }

	// ── シリアライズ ────────────────────────────────────────

	/// @brief シリアライズ（const版）
	template <typename V>
	void visit(V& v) const { v.write("x", x); v.write("y", y); v.write("z", z); v.write("w", w); }

	/// @brief デシリアライズ（非const版）
	template <typename V>
	void visit(V& v) { v.read("x", x); v.read("y", y); v.read("z", z); v.read("w", w); }
};

template <Arithmetic T>
[[nodiscard]] constexpr Vec4<T> operator*(T scalar, const Vec4<T>& v) noexcept { return v * scalar; }

using Vec4f = Vec4<float>;    ///< float版 Vec4
using Vec4d = Vec4<double>;   ///< double版 Vec4
using Vec4i = Vec4<int>;      ///< int版 Vec4

} // namespace sgc
