#pragma once

/// @file Interpolation.hpp
/// @brief 補間関数（lerp, smoothstep, remap等）

#include "sgc/types/Concepts.hpp"

namespace sgc
{

/// @brief 線形補間
/// @tparam T 浮動小数点型
/// @param a 開始値
/// @param b 終了値
/// @param t 補間係数 [0, 1]
/// @return a と b の間の補間値
template <FloatingPoint T>
[[nodiscard]] constexpr T lerp(T a, T b, T t) noexcept
{
	return a + (b - a) * t;
}

/// @brief 逆線形補間 — 値が [a, b] のどの位置にあるかを [0, 1] で返す
/// @param a 範囲の開始値
/// @param b 範囲の終了値
/// @param value 評価する値
/// @return 補間係数 t
template <FloatingPoint T>
[[nodiscard]] constexpr T inverseLerp(T a, T b, T value) noexcept
{
	return (value - a) / (b - a);
}

/// @brief 範囲のリマップ — ある範囲の値を別の範囲に変換する
/// @param inMin  入力範囲の最小値
/// @param inMax  入力範囲の最大値
/// @param outMin 出力範囲の最小値
/// @param outMax 出力範囲の最大値
/// @param value  変換する値
/// @return 出力範囲にリマップされた値
///
/// @code
/// // 0〜100のHP値を0〜1のバー幅に変換
/// float barWidth = sgc::remap(0.0f, 100.0f, 0.0f, 1.0f, currentHP);
/// @endcode
template <FloatingPoint T>
[[nodiscard]] constexpr T remap(T inMin, T inMax, T outMin, T outMax, T value) noexcept
{
	const T t = (value - inMin) / (inMax - inMin);
	return outMin + (outMax - outMin) * t;
}

/// @brief Hermite補間（3次スムーズステップ）
/// @param edge0 下限
/// @param edge1 上限
/// @param x 評価値
/// @return 0と1の間のスムーズな補間値
///
/// @note edge0 <= x <= edge1 の範囲で滑らかに0から1に遷移する
template <FloatingPoint T>
[[nodiscard]] constexpr T smoothstep(T edge0, T edge1, T x) noexcept
{
	T t = (x - edge0) / (edge1 - edge0);
	if (t < T{0}) t = T{0};
	if (t > T{1}) t = T{1};
	return t * t * (T{3} - T{2} * t);
}

/// @brief 5次スムーズステップ（Ken Perlin改良版）
/// @param edge0 下限
/// @param edge1 上限
/// @param x 評価値
/// @return 0と1の間のより滑らかな補間値
///
/// @note smoothstepより滑らかで、1次・2次導関数もゼロになる
template <FloatingPoint T>
[[nodiscard]] constexpr T smootherstep(T edge0, T edge1, T x) noexcept
{
	T t = (x - edge0) / (edge1 - edge0);
	if (t < T{0}) t = T{0};
	if (t > T{1}) t = T{1};
	return t * t * t * (t * (t * T{6} - T{15}) + T{10});
}

/// @brief 二点間の歩進 — 現在地からゴールに向かって最大delta分だけ進む
/// @param current 現在の値
/// @param target  目標の値
/// @param delta   最大変化量（正の値）
/// @return ゴールに向かって進んだ値
template <FloatingPoint T>
[[nodiscard]] constexpr T moveTowards(T current, T target, T delta) noexcept
{
	const T diff = target - current;
	if (diff > T{0})
	{
		return (diff <= delta) ? target : current + delta;
	}
	return (-diff <= delta) ? target : current - delta;
}

} // namespace sgc
