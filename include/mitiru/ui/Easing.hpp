#pragma once

/// @file Easing.hpp
/// @brief UI システム向けの汎用 easing 関数。
/// @details mitiru::vn::Easing の包括的な easing ライブラリを
///          mitiru::ui::easing namespace へ re-export する薄い bridge。
///          31 種の標準 easing type に加え cubic-bezier と spring physics が
///          UIAnimation その他の UI subsystem から利用できる。

#include <mitiru/vn/EasingFunctions.hpp>

namespace mitiru::ui
{

/// @brief easing type の列挙 (VN module から re-export)。
using EasingType = vn::EasingType;

/// @brief UI animation 向けの汎用 easing 関数。
/// @details 全関数は t を [0.0, 1.0] で受け取り、eased value を返す。
namespace easing
{

// ── 標準 easing 関数 ────────────────────────────────────

/// @brief 線形補間 (easing なし)。
[[nodiscard]] constexpr float linear(float t) noexcept
{
	return vn::Easing::linear(t);
}

// ── Quadratic ────────────────────────────────────────────────────

[[nodiscard]] constexpr float easeInQuad(float t) noexcept
{
	return vn::Easing::easeInQuad(t);
}

[[nodiscard]] constexpr float easeOutQuad(float t) noexcept
{
	return vn::Easing::easeOutQuad(t);
}

[[nodiscard]] constexpr float easeInOutQuad(float t) noexcept
{
	return vn::Easing::easeInOutQuad(t);
}

// ── Cubic ────────────────────────────────────────────────────────

[[nodiscard]] constexpr float easeInCubic(float t) noexcept
{
	return vn::Easing::easeInCubic(t);
}

[[nodiscard]] constexpr float easeOutCubic(float t) noexcept
{
	return vn::Easing::easeOutCubic(t);
}

[[nodiscard]] constexpr float easeInOutCubic(float t) noexcept
{
	return vn::Easing::easeInOutCubic(t);
}

// ── Quartic ──────────────────────────────────────────────────────

[[nodiscard]] constexpr float easeInQuart(float t) noexcept
{
	return vn::Easing::easeInQuart(t);
}

[[nodiscard]] constexpr float easeOutQuart(float t) noexcept
{
	return vn::Easing::easeOutQuart(t);
}

[[nodiscard]] constexpr float easeInOutQuart(float t) noexcept
{
	return vn::Easing::easeInOutQuart(t);
}

// ── Quintic ──────────────────────────────────────────────────────

[[nodiscard]] constexpr float easeInQuint(float t) noexcept
{
	return vn::Easing::easeInQuint(t);
}

[[nodiscard]] constexpr float easeOutQuint(float t) noexcept
{
	return vn::Easing::easeOutQuint(t);
}

[[nodiscard]] constexpr float easeInOutQuint(float t) noexcept
{
	return vn::Easing::easeInOutQuint(t);
}

// ── Sine ─────────────────────────────────────────────────────────

[[nodiscard]] inline float easeInSine(float t) noexcept
{
	return vn::Easing::easeInSine(t);
}

[[nodiscard]] inline float easeOutSine(float t) noexcept
{
	return vn::Easing::easeOutSine(t);
}

[[nodiscard]] inline float easeInOutSine(float t) noexcept
{
	return vn::Easing::easeInOutSine(t);
}

// ── Exponential ──────────────────────────────────────────────────

[[nodiscard]] inline float easeInExpo(float t) noexcept
{
	return vn::Easing::easeInExpo(t);
}

[[nodiscard]] inline float easeOutExpo(float t) noexcept
{
	return vn::Easing::easeOutExpo(t);
}

[[nodiscard]] inline float easeInOutExpo(float t) noexcept
{
	return vn::Easing::easeInOutExpo(t);
}

// ── Circular ─────────────────────────────────────────────────────

[[nodiscard]] inline float easeInCirc(float t) noexcept
{
	return vn::Easing::easeInCirc(t);
}

[[nodiscard]] inline float easeOutCirc(float t) noexcept
{
	return vn::Easing::easeOutCirc(t);
}

[[nodiscard]] inline float easeInOutCirc(float t) noexcept
{
	return vn::Easing::easeInOutCirc(t);
}

// ── Elastic ──────────────────────────────────────────────────────

[[nodiscard]] inline float easeInElastic(float t) noexcept
{
	return vn::Easing::easeInElastic(t);
}

[[nodiscard]] inline float easeOutElastic(float t) noexcept
{
	return vn::Easing::easeOutElastic(t);
}

[[nodiscard]] inline float easeInOutElastic(float t) noexcept
{
	return vn::Easing::easeInOutElastic(t);
}

// ── Back ─────────────────────────────────────────────────────────

[[nodiscard]] constexpr float easeInBack(float t) noexcept
{
	return vn::Easing::easeInBack(t);
}

[[nodiscard]] constexpr float easeOutBack(float t) noexcept
{
	return vn::Easing::easeOutBack(t);
}

[[nodiscard]] constexpr float easeInOutBack(float t) noexcept
{
	return vn::Easing::easeInOutBack(t);
}

// ── Bounce ───────────────────────────────────────────────────────

[[nodiscard]] constexpr float easeInBounce(float t) noexcept
{
	return vn::Easing::easeInBounce(t);
}

[[nodiscard]] constexpr float easeOutBounce(float t) noexcept
{
	return vn::Easing::easeOutBounce(t);
}

[[nodiscard]] constexpr float easeInOutBounce(float t) noexcept
{
	return vn::Easing::easeInOutBounce(t);
}

// ── Advanced ─────────────────────────────────────────────────────

/// @brief cubic bezier easing (CSS cubic-bezier() 相当)。
/// @param x1 制御点 1 の X。
/// @param y1 制御点 1 の Y。
/// @param x2 制御点 2 の X。
/// @param y2 制御点 2 の Y。
/// @param t 入力値 [0.0, 1.0]。
/// @return eased value。
[[nodiscard]] inline float cubicBezier(
	float x1, float y1, float x2, float y2, float t) noexcept
{
	return vn::Easing::cubicBezier(x1, y1, x2, y2, t);
}

/// @brief spring physics easing。
/// @param damping 減衰係数 (0.0 = 減衰なし、1.0 = 臨界)。
/// @param frequency 振動周波数。
/// @param t 入力値 [0.0, 1.0]。
/// @return eased value。
[[nodiscard]] inline float spring(
	float damping, float frequency, float t) noexcept
{
	return vn::Easing::spring(damping, frequency, t);
}

// ── Dispatch ─────────────────────────────────────────────────────

/// @brief type 列挙を指定して easing を適用する。
/// @param type easing type。
/// @param t 入力値 [0.0, 1.0]。
/// @return eased value。
[[nodiscard]] inline float apply(EasingType type, float t) noexcept
{
	return vn::Easing::apply(type, t);
}

} // namespace easing
} // namespace mitiru::ui
