#pragma once

/// @file MathConstants.hpp
/// @brief 数学定数と角度変換ユーティリティ

#include <cmath>
#include <numbers>
#include <type_traits>

#include "sgc/types/Concepts.hpp"

namespace sgc
{

// ── 数学定数 ────────────────────────────────────────────────────

/// @brief 円周率 (float)
inline constexpr float PI_F = std::numbers::pi_v<float>;

/// @brief 円周率 (double)
inline constexpr double PI = std::numbers::pi;

/// @brief 円周率の2倍（タウ） (float)
inline constexpr float TAU_F = PI_F * 2.0f;

/// @brief 円周率の2倍（タウ） (double)
inline constexpr double TAU = PI * 2.0;

/// @brief 自然対数の底 e (float)
inline constexpr float E_F = std::numbers::e_v<float>;

/// @brief 自然対数の底 e (double)
inline constexpr double E = std::numbers::e;

/// @brief 2の平方根 (float)
inline constexpr float SQRT2_F = std::numbers::sqrt2_v<float>;

/// @brief 2の平方根 (double)
inline constexpr double SQRT2 = std::numbers::sqrt2;

/// @brief 黄金比 (float)
inline constexpr float PHI_F = std::numbers::phi_v<float>;

/// @brief 黄金比 (double)
inline constexpr double PHI = std::numbers::phi;

/// @brief 微小値（浮動小数点比較用） (float)
inline constexpr float EPSILON_F = 1.0e-6f;

/// @brief 微小値（浮動小数点比較用） (double)
inline constexpr double EPSILON = 1.0e-9;

// ── 角度変換 ────────────────────────────────────────────────────

/// @brief 度をラジアンに変換する
/// @tparam T 浮動小数点型
/// @param degrees 度数法の角度
/// @return ラジアン
template <FloatingPoint T>
[[nodiscard]] constexpr T toRadians(T degrees) noexcept
{
	return degrees * std::numbers::pi_v<T> / static_cast<T>(180);
}

/// @brief ラジアンを度に変換する
/// @tparam T 浮動小数点型
/// @param radians ラジアン
/// @return 度数法の角度
template <FloatingPoint T>
[[nodiscard]] constexpr T toDegrees(T radians) noexcept
{
	return radians * static_cast<T>(180) / std::numbers::pi_v<T>;
}

// ── 浮動小数点比較 ──────────────────────────────────────────────

/// @brief 2つの浮動小数点値がほぼ等しいか判定する
/// @tparam T 浮動小数点型
/// @param a 比較値1
/// @param b 比較値2
/// @param epsilon 許容誤差
/// @return 差がepsilon以内なら true
template <FloatingPoint T>
[[nodiscard]] constexpr bool approxEqual(T a, T b, T epsilon = static_cast<T>(1e-6)) noexcept
{
	const T diff = (a > b) ? (a - b) : (b - a);
	return diff <= epsilon;
}

/// @brief 値を指定範囲にクランプする
/// @tparam T 算術型
/// @param value クランプする値
/// @param minVal 最小値
/// @param maxVal 最大値
/// @return クランプされた値
template <Arithmetic T>
[[nodiscard]] constexpr T clamp(T value, T minVal, T maxVal) noexcept
{
	if (value < minVal) return minVal;
	if (value > maxVal) return maxVal;
	return value;
}

} // namespace sgc
