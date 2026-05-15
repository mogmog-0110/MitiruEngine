#pragma once

/// @file Easing.hpp
/// @brief General-purpose easing functions for the UI system.
/// @details Thin bridge that re-exports the comprehensive easing library
///          from mitiru::vn::Easing into the mitiru::ui::easing namespace.
///          All 31 standard easing types plus cubic-bezier and spring physics
///          are available for use with UIAnimation and other UI subsystems.

#include <mitiru/vn/EasingFunctions.hpp>

namespace mitiru::ui
{

/// @brief Easing type enumeration (re-exported from VN module).
using EasingType = vn::EasingType;

/// @brief General-purpose easing functions for UI animations.
/// @details All functions accept t in [0.0, 1.0] and return the eased value.
namespace easing
{

// ── Standard easing functions ────────────────────────────────────

/// @brief Linear interpolation (no easing).
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

/// @brief Cubic bezier easing (CSS cubic-bezier() equivalent).
/// @param x1 Control point 1 X.
/// @param y1 Control point 1 Y.
/// @param x2 Control point 2 X.
/// @param y2 Control point 2 Y.
/// @param t Input value [0.0, 1.0].
/// @return Eased value.
[[nodiscard]] inline float cubicBezier(
	float x1, float y1, float x2, float y2, float t) noexcept
{
	return vn::Easing::cubicBezier(x1, y1, x2, y2, t);
}

/// @brief Spring physics easing.
/// @param damping Damping coefficient (0.0 = no damping, 1.0 = critical).
/// @param frequency Oscillation frequency.
/// @param t Input value [0.0, 1.0].
/// @return Eased value.
[[nodiscard]] inline float spring(
	float damping, float frequency, float t) noexcept
{
	return vn::Easing::spring(damping, frequency, t);
}

// ── Dispatch ─────────────────────────────────────────────────────

/// @brief Apply easing by type enumeration.
/// @param type Easing type.
/// @param t Input value [0.0, 1.0].
/// @return Eased value.
[[nodiscard]] inline float apply(EasingType type, float t) noexcept
{
	return vn::Easing::apply(type, t);
}

} // namespace easing
} // namespace mitiru::ui
