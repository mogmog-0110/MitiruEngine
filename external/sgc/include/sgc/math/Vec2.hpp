#pragma once

/// @file Vec2.hpp
/// @brief 2次元ベクトル型 Vec2<T>
///
/// ゲーム開発で最も使用頻度の高い2Dベクトル。
/// constexprで全演算が可能。concepts制約付き。

#include <cmath>

#include "sgc/types/Concepts.hpp"

namespace sgc
{

/// @brief 2次元ベクトル
/// @tparam T 算術型（int, float, double等）
///
/// @code
/// sgc::Vec2f pos{10.0f, 20.0f};
/// sgc::Vec2f vel{1.0f, 0.0f};
/// auto next = pos + vel * deltaTime;
/// @endcode
template <Arithmetic T>
struct Vec2
{
	T x{};  ///< X成分
	T y{};  ///< Y成分

	/// @brief デフォルトコンストラクタ（ゼロ初期化）
	constexpr Vec2() noexcept = default;

	/// @brief 成分を指定して構築する
	/// @param x X成分
	/// @param y Y成分
	constexpr Vec2(T x, T y) noexcept : x(x), y(y) {}

	/// @brief 全成分を同じ値で初期化する
	/// @param value 全成分に設定する値
	constexpr explicit Vec2(T value) noexcept : x(value), y(value) {}

	/// @brief 異なる型のVec2から変換する
	/// @tparam U 変換元の型
	template <Arithmetic U>
	constexpr explicit Vec2(const Vec2<U>& other) noexcept
		: x(static_cast<T>(other.x)), y(static_cast<T>(other.y)) {}

	// ── 算術演算子 ──────────────────────────────────────────

	[[nodiscard]] constexpr Vec2 operator+(const Vec2& rhs) const noexcept { return {x + rhs.x, y + rhs.y}; }
	[[nodiscard]] constexpr Vec2 operator-(const Vec2& rhs) const noexcept { return {x - rhs.x, y - rhs.y}; }
	[[nodiscard]] constexpr Vec2 operator*(const Vec2& rhs) const noexcept { return {x * rhs.x, y * rhs.y}; }
	[[nodiscard]] constexpr Vec2 operator/(const Vec2& rhs) const noexcept { return {x / rhs.x, y / rhs.y}; }
	[[nodiscard]] constexpr Vec2 operator*(T scalar) const noexcept { return {x * scalar, y * scalar}; }
	[[nodiscard]] constexpr Vec2 operator/(T scalar) const noexcept { return {x / scalar, y / scalar}; }
	[[nodiscard]] constexpr Vec2 operator-() const noexcept { return {-x, -y}; }

	constexpr Vec2& operator+=(const Vec2& rhs) noexcept { x += rhs.x; y += rhs.y; return *this; }
	constexpr Vec2& operator-=(const Vec2& rhs) noexcept { x -= rhs.x; y -= rhs.y; return *this; }
	constexpr Vec2& operator*=(T scalar) noexcept { x *= scalar; y *= scalar; return *this; }
	constexpr Vec2& operator/=(T scalar) noexcept { x /= scalar; y /= scalar; return *this; }

	[[nodiscard]] constexpr bool operator==(const Vec2& rhs) const noexcept = default;

	// ── ベクトル演算 ────────────────────────────────────────

	/// @brief 内積を計算する
	/// @param rhs 右辺ベクトル
	/// @return 内積の値
	[[nodiscard]] constexpr T dot(const Vec2& rhs) const noexcept
	{
		return x * rhs.x + y * rhs.y;
	}

	/// @brief 2D外積（スカラー値）を計算する
	/// @param rhs 右辺ベクトル
	/// @return 外積のZ成分
	[[nodiscard]] constexpr T cross(const Vec2& rhs) const noexcept
	{
		return x * rhs.y - y * rhs.x;
	}

	/// @brief ベクトルの長さの二乗を返す（sqrtを避けたい場合に使用）
	/// @return 長さの二乗
	[[nodiscard]] constexpr T lengthSquared() const noexcept
	{
		return x * x + y * y;
	}

	/// @brief ベクトルの長さを返す
	/// @return ベクトルの長さ
	[[nodiscard]] T length() const noexcept
		requires FloatingPoint<T>
	{
		return std::sqrt(lengthSquared());
	}

	/// @brief 正規化されたベクトル（長さ1）を返す
	/// @return 単位ベクトル
	/// @note 長さがゼロの場合はゼロベクトルを返す
	[[nodiscard]] Vec2 normalized() const noexcept
		requires FloatingPoint<T>
	{
		const T len = length();
		if (len == T{0}) return {};
		return *this / len;
	}

	/// @brief 他のベクトルとの距離を返す
	/// @param other 対象ベクトル
	/// @return 2点間の距離
	[[nodiscard]] T distanceTo(const Vec2& other) const noexcept
		requires FloatingPoint<T>
	{
		return (*this - other).length();
	}

	/// @brief 他のベクトルとの距離の二乗を返す
	/// @param other 対象ベクトル
	/// @return 2点間の距離の二乗
	[[nodiscard]] constexpr T distanceSquaredTo(const Vec2& other) const noexcept
	{
		return (*this - other).lengthSquared();
	}

	/// @brief 法線で反射したベクトルを返す
	/// @param normal 反射面の法線（正規化済み）
	/// @return 反射後のベクトル
	[[nodiscard]] constexpr Vec2 reflect(const Vec2& normal) const noexcept
		requires FloatingPoint<T>
	{
		return *this - normal * (T{2} * this->dot(normal));
	}

	/// @brief 90度回転したベクトルを返す（反時計回り）
	/// @return 垂直ベクトル
	[[nodiscard]] constexpr Vec2 perpendicular() const noexcept
	{
		return {-y, x};
	}

	/// @brief 指定ラジアンだけ回転したベクトルを返す
	/// @param radians 回転角度（ラジアン、反時計回りが正）
	/// @return 回転後のベクトル
	[[nodiscard]] Vec2 rotate(T radians) const noexcept
		requires FloatingPoint<T>
	{
		const T c = std::cos(radians);
		const T s = std::sin(radians);
		return {x * c - y * s, x * s + y * c};
	}

	// ── 追加メソッド ────────────────────────────────────────

	/// @brief 各成分を[min, max]にクランプした新しいベクトルを返す
	/// @param minVal 最小値
	/// @param maxVal 最大値
	/// @return クランプ後のベクトル
	[[nodiscard]] constexpr Vec2 clamped(const Vec2& minVal, const Vec2& maxVal) const noexcept
	{
		auto clampVal = [](T v, T lo, T hi) constexpr -> T
		{
			if (v < lo) return lo;
			if (v > hi) return hi;
			return v;
		};
		return {clampVal(x, minVal.x, maxVal.x), clampVal(y, minVal.y, maxVal.y)};
	}

	/// @brief 他のベクトルとの線形補間を返す
	/// @param other 補間先
	/// @param t 補間係数 [0, 1]
	/// @return 補間結果
	[[nodiscard]] constexpr Vec2 lerp(const Vec2& other, T t) const noexcept
		requires FloatingPoint<T>
	{
		return {x + (other.x - x) * t, y + (other.y - y) * t};
	}

	/// @brief 他のベクトルへの角度を返す（ラジアン）
	/// @param other 対象ベクトル
	/// @return 角度（ラジアン）
	[[nodiscard]] T angleTo(const Vec2& other) const noexcept
		requires FloatingPoint<T>
	{
		return std::atan2(other.y - y, other.x - x);
	}

	/// @brief このベクトルをontoに射影したベクトルを返す
	/// @param onto 射影先のベクトル
	/// @return 射影ベクトル
	[[nodiscard]] constexpr Vec2 projected(const Vec2& onto) const noexcept
		requires FloatingPoint<T>
	{
		const T d = onto.dot(onto);
		if (d == T{0}) return {};
		return onto * (this->dot(onto) / d);
	}

	/// @brief 2つのベクトルの各成分の最小値を返す
	[[nodiscard]] static constexpr Vec2 min(const Vec2& a, const Vec2& b) noexcept
	{
		return {(a.x < b.x) ? a.x : b.x, (a.y < b.y) ? a.y : b.y};
	}

	/// @brief 2つのベクトルの各成分の最大値を返す
	[[nodiscard]] static constexpr Vec2 max(const Vec2& a, const Vec2& b) noexcept
	{
		return {(a.x > b.x) ? a.x : b.x, (a.y > b.y) ? a.y : b.y};
	}

	// ── 定数 ────────────────────────────────────────────────

	/// @brief ゼロベクトル (0, 0)
	[[nodiscard]] static constexpr Vec2 zero() noexcept { return {T{0}, T{0}}; }

	/// @brief 単位ベクトル (1, 1)
	[[nodiscard]] static constexpr Vec2 one() noexcept { return {T{1}, T{1}}; }

	/// @brief 上方向 (0, -1) ※画面座標系
	[[nodiscard]] static constexpr Vec2 up() noexcept { return {T{0}, T{-1}}; }

	/// @brief 下方向 (0, 1) ※画面座標系
	[[nodiscard]] static constexpr Vec2 down() noexcept { return {T{0}, T{1}}; }

	/// @brief 左方向 (-1, 0)
	[[nodiscard]] static constexpr Vec2 left() noexcept { return {T{-1}, T{0}}; }

	/// @brief 右方向 (1, 0)
	[[nodiscard]] static constexpr Vec2 right() noexcept { return {T{1}, T{0}}; }

	// ── シリアライズ ────────────────────────────────────────

	/// @brief シリアライズ（const版）
	template <typename V>
	void visit(V& v) const { v.write("x", x); v.write("y", y); }

	/// @brief デシリアライズ（非const版）
	template <typename V>
	void visit(V& v) { v.read("x", x); v.read("y", y); }
};

/// @brief スカラー * Vec2 演算子
template <Arithmetic T>
[[nodiscard]] constexpr Vec2<T> operator*(T scalar, const Vec2<T>& v) noexcept
{
	return v * scalar;
}

// ── エイリアス ──────────────────────────────────────────────────

using Vec2f = Vec2<float>;    ///< float版 Vec2
using Vec2d = Vec2<double>;   ///< double版 Vec2
using Vec2i = Vec2<int>;      ///< int版 Vec2

} // namespace sgc
