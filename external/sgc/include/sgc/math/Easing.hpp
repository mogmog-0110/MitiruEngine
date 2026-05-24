#pragma once

/// @file Easing.hpp
/// @brief イージング関数コレクション
///
/// ゲームのアニメーション・UI遷移で使用する標準イージング関数。
/// すべての関数は t=[0,1] を受け取り [0,1] 付近の値を返す。
/// @see https://easings.net/

#include <cmath>
#include <numbers>

#include "sgc/types/Concepts.hpp"

namespace sgc
{

/// @brief イージング関数をまとめた名前空間
namespace easing
{

// ── Linear ──────────────────────────────────────────────────────

/// @brief 線形補間（イージングなし）
template <FloatingPoint T>
[[nodiscard]] constexpr T linear(T t) noexcept { return t; }

// ── Quad ────────────────────────────────────────────────────────

/// @brief 二次関数（加速）
template <FloatingPoint T>
[[nodiscard]] constexpr T inQuad(T t) noexcept { return t * t; }

/// @brief 二次関数（減速）
template <FloatingPoint T>
[[nodiscard]] constexpr T outQuad(T t) noexcept { return t * (T{2} - t); }

/// @brief 二次関数（加速→減速）
template <FloatingPoint T>
[[nodiscard]] constexpr T inOutQuad(T t) noexcept
{
	return (t < T{0.5}) ? T{2} * t * t : T{-1} + (T{4} - T{2} * t) * t;
}

// ── Cubic ───────────────────────────────────────────────────────

/// @brief 三次関数（加速）
template <FloatingPoint T>
[[nodiscard]] constexpr T inCubic(T t) noexcept { return t * t * t; }

/// @brief 三次関数（減速）
template <FloatingPoint T>
[[nodiscard]] constexpr T outCubic(T t) noexcept { const T u = t - T{1}; return u * u * u + T{1}; }

/// @brief 三次関数（加速→減速）
template <FloatingPoint T>
[[nodiscard]] constexpr T inOutCubic(T t) noexcept
{
	return (t < T{0.5}) ? T{4} * t * t * t : (t - T{1}) * (T{2} * t - T{2}) * (T{2} * t - T{2}) + T{1};
}

// ── Quart ───────────────────────────────────────────────────────

/// @brief 四次関数（加速）
template <FloatingPoint T>
[[nodiscard]] constexpr T inQuart(T t) noexcept { return t * t * t * t; }

/// @brief 四次関数（減速）
template <FloatingPoint T>
[[nodiscard]] constexpr T outQuart(T t) noexcept { const T u = t - T{1}; return T{1} - u * u * u * u; }

/// @brief 四次関数（加速→減速）
template <FloatingPoint T>
[[nodiscard]] constexpr T inOutQuart(T t) noexcept
{
	if (t < T{0.5}) return T{8} * t * t * t * t;
	const T u = t - T{1};
	return T{1} - T{8} * u * u * u * u;
}

// ── Quint ───────────────────────────────────────────────────────

/// @brief 五次関数（加速）
template <FloatingPoint T>
[[nodiscard]] constexpr T inQuint(T t) noexcept { return t * t * t * t * t; }

/// @brief 五次関数（減速）
template <FloatingPoint T>
[[nodiscard]] constexpr T outQuint(T t) noexcept { const T u = t - T{1}; return u * u * u * u * u + T{1}; }

/// @brief 五次関数（加速→減速）
template <FloatingPoint T>
[[nodiscard]] constexpr T inOutQuint(T t) noexcept
{
	if (t < T{0.5}) return T{16} * t * t * t * t * t;
	const T u = t - T{1};
	return T{1} + T{16} * u * u * u * u * u;
}

// ── Sine ────────────────────────────────────────────────────────

/// @brief サイン波（加速）
template <FloatingPoint T>
[[nodiscard]] T inSine(T t) noexcept
{
	return T{1} - std::cos(t * std::numbers::pi_v<T> / T{2});
}

/// @brief サイン波（減速）
template <FloatingPoint T>
[[nodiscard]] T outSine(T t) noexcept
{
	return std::sin(t * std::numbers::pi_v<T> / T{2});
}

/// @brief サイン波（加速→減速）
template <FloatingPoint T>
[[nodiscard]] T inOutSine(T t) noexcept
{
	return (T{1} - std::cos(std::numbers::pi_v<T> * t)) / T{2};
}

// ── Expo ────────────────────────────────────────────────────────

/// @brief 指数関数（加速）
template <FloatingPoint T>
[[nodiscard]] T inExpo(T t) noexcept
{
	return (t == T{0}) ? T{0} : std::pow(T{2}, T{10} * (t - T{1}));
}

/// @brief 指数関数（減速）
template <FloatingPoint T>
[[nodiscard]] T outExpo(T t) noexcept
{
	return (t == T{1}) ? T{1} : T{1} - std::pow(T{2}, T{-10} * t);
}

// ── Circ ────────────────────────────────────────────────────────

/// @brief 円弧（加速）
template <FloatingPoint T>
[[nodiscard]] T inCirc(T t) noexcept
{
	return T{1} - std::sqrt(T{1} - t * t);
}

/// @brief 円弧（減速）
template <FloatingPoint T>
[[nodiscard]] T outCirc(T t) noexcept
{
	const T u = t - T{1};
	return std::sqrt(T{1} - u * u);
}

// ── Elastic ─────────────────────────────────────────────────────

/// @brief 弾性（加速）— ゴムのような跳ね返り
template <FloatingPoint T>
[[nodiscard]] T inElastic(T t) noexcept
{
	if (t == T{0} || t == T{1}) return t;
	return -std::pow(T{2}, T{10} * t - T{10}) *
		std::sin((t * T{10} - T{10.75}) * (T{2} * std::numbers::pi_v<T> / T{3}));
}

/// @brief 弾性（減速）
template <FloatingPoint T>
[[nodiscard]] T outElastic(T t) noexcept
{
	if (t == T{0} || t == T{1}) return t;
	return std::pow(T{2}, T{-10} * t) *
		std::sin((t * T{10} - T{0.75}) * (T{2} * std::numbers::pi_v<T> / T{3})) + T{1};
}

// ── Back ────────────────────────────────────────────────────────

/// @brief 引き戻し（加速）— 少し戻ってから進む
template <FloatingPoint T>
[[nodiscard]] constexpr T inBack(T t) noexcept
{
	constexpr T c = T{1.70158};
	return (c + T{1}) * t * t * t - c * t * t;
}

/// @brief 引き戻し（減速）
template <FloatingPoint T>
[[nodiscard]] constexpr T outBack(T t) noexcept
{
	constexpr T c = T{1.70158};
	const T u = t - T{1};
	return T{1} + (c + T{1}) * u * u * u + c * u * u;
}

// ── Bounce ──────────────────────────────────────────────────────

/// @brief バウンス（減速）— 地面で跳ね返るような動き
template <FloatingPoint T>
[[nodiscard]] constexpr T outBounce(T t) noexcept
{
	constexpr T n1 = T{7.5625};
	constexpr T d1 = T{2.75};

	if (t < T{1} / d1)
	{
		return n1 * t * t;
	}
	if (t < T{2} / d1)
	{
		const T u = t - T{1.5} / d1;
		return n1 * u * u + T{0.75};
	}
	if (t < T{2.5} / d1)
	{
		const T u = t - T{2.25} / d1;
		return n1 * u * u + T{0.9375};
	}
	const T u = t - T{2.625} / d1;
	return n1 * u * u + T{0.984375};
}

/// @brief バウンス（加速）
template <FloatingPoint T>
[[nodiscard]] constexpr T inBounce(T t) noexcept
{
	return T{1} - outBounce(T{1} - t);
}

// ── InOut 追加 ─────────────────────────────────────────────────

/// @brief 指数関数（加速→減速）
template <FloatingPoint T>
[[nodiscard]] T inOutExpo(T t) noexcept
{
	if (t == T{0} || t == T{1}) return t;
	if (t < T{0.5})
	{
		return std::pow(T{2}, T{20} * t - T{10}) / T{2};
	}
	return (T{2} - std::pow(T{2}, T{-20} * t + T{10})) / T{2};
}

/// @brief 円弧（加速→減速）
template <FloatingPoint T>
[[nodiscard]] T inOutCirc(T t) noexcept
{
	if (t < T{0.5})
	{
		return (T{1} - std::sqrt(T{1} - T{4} * t * t)) / T{2};
	}
	const T u = T{-2} * t + T{2};
	return (std::sqrt(T{1} - u * u) + T{1}) / T{2};
}

/// @brief 弾性（加速→減速）
template <FloatingPoint T>
[[nodiscard]] T inOutElastic(T t) noexcept
{
	if (t == T{0} || t == T{1}) return t;
	constexpr T c = T{2} * std::numbers::pi_v<T> / T{4.5};
	if (t < T{0.5})
	{
		return -(std::pow(T{2}, T{20} * t - T{10}) * std::sin((T{20} * t - T{11.125}) * c)) / T{2};
	}
	return (std::pow(T{2}, T{-20} * t + T{10}) * std::sin((T{20} * t - T{11.125}) * c)) / T{2} + T{1};
}

/// @brief 引き戻し（加速→減速）
template <FloatingPoint T>
[[nodiscard]] constexpr T inOutBack(T t) noexcept
{
	constexpr T c1 = T{1.70158};
	constexpr T c2 = c1 * T{1.525};
	if (t < T{0.5})
	{
		return (T{4} * t * t * ((c2 + T{1}) * T{2} * t - c2)) / T{2};
	}
	const T u = T{2} * t - T{2};
	return (u * u * ((c2 + T{1}) * u + c2) + T{2}) / T{2};
}

/// @brief バウンス（加速→減速）
template <FloatingPoint T>
[[nodiscard]] constexpr T inOutBounce(T t) noexcept
{
	if (t < T{0.5})
	{
		return (T{1} - outBounce(T{1} - T{2} * t)) / T{2};
	}
	return (T{1} + outBounce(T{2} * t - T{1})) / T{2};
}

} // namespace easing
} // namespace sgc
