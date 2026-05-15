#pragma once

/// @file EasingFunctions.hpp
/// @brief 包括的イージング関数ライブラリ
/// @details ビジュアルノベル演出およびアニメーション全般に使用する
///          イージング関数群を提供する。すべて float→float、入力 0.0-1.0。

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mitiru::vn
{

/// @brief イージング関数の種類
enum class EasingType
{
	Linear,
	EaseInQuad,
	EaseOutQuad,
	EaseInOutQuad,
	EaseInCubic,
	EaseOutCubic,
	EaseInOutCubic,
	EaseInQuart,
	EaseOutQuart,
	EaseInOutQuart,
	EaseInQuint,
	EaseOutQuint,
	EaseInOutQuint,
	EaseInSine,
	EaseOutSine,
	EaseInOutSine,
	EaseInExpo,
	EaseOutExpo,
	EaseInOutExpo,
	EaseInCirc,
	EaseOutCirc,
	EaseInOutCirc,
	EaseInElastic,
	EaseOutElastic,
	EaseInOutElastic,
	EaseInBack,
	EaseOutBack,
	EaseInOutBack,
	EaseInBounce,
	EaseOutBounce,
	EaseInOutBounce,
};

/// @brief イージング関数群
/// @details すべての関数は t を [0.0, 1.0] の範囲で受け取り、
///          イージング後の値を返す。
namespace Easing
{

/// @brief 線形補間（イージングなし）
[[nodiscard]] constexpr float linear(float t) noexcept
{
	return t;
}

// ── Quadratic ──────────────────────────────────────────────

[[nodiscard]] constexpr float easeInQuad(float t) noexcept
{
	return t * t;
}

[[nodiscard]] constexpr float easeOutQuad(float t) noexcept
{
	return t * (2.0f - t);
}

[[nodiscard]] constexpr float easeInOutQuad(float t) noexcept
{
	return (t < 0.5f)
		? 2.0f * t * t
		: -1.0f + (4.0f - 2.0f * t) * t;
}

// ── Cubic ──────────────────────────────────────────────────

[[nodiscard]] constexpr float easeInCubic(float t) noexcept
{
	return t * t * t;
}

[[nodiscard]] constexpr float easeOutCubic(float t) noexcept
{
	const float f = t - 1.0f;
	return f * f * f + 1.0f;
}

[[nodiscard]] constexpr float easeInOutCubic(float t) noexcept
{
	return (t < 0.5f)
		? 4.0f * t * t * t
		: 1.0f + (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f);
}

// ── Quartic ────────────────────────────────────────────────

[[nodiscard]] constexpr float easeInQuart(float t) noexcept
{
	return t * t * t * t;
}

[[nodiscard]] constexpr float easeOutQuart(float t) noexcept
{
	const float f = t - 1.0f;
	return 1.0f - f * f * f * f;
}

[[nodiscard]] constexpr float easeInOutQuart(float t) noexcept
{
	if (t < 0.5f)
	{
		return 8.0f * t * t * t * t;
	}
	const float f = t - 1.0f;
	return 1.0f - 8.0f * f * f * f * f;
}

// ── Quintic ────────────────────────────────────────────────

[[nodiscard]] constexpr float easeInQuint(float t) noexcept
{
	return t * t * t * t * t;
}

[[nodiscard]] constexpr float easeOutQuint(float t) noexcept
{
	const float f = t - 1.0f;
	return 1.0f + f * f * f * f * f;
}

[[nodiscard]] constexpr float easeInOutQuint(float t) noexcept
{
	if (t < 0.5f)
	{
		return 16.0f * t * t * t * t * t;
	}
	const float f = t - 1.0f;
	return 1.0f + 16.0f * f * f * f * f * f;
}

// ── Sine ───────────────────────────────────────────────────

[[nodiscard]] inline float easeInSine(float t) noexcept
{
	return 1.0f - std::cos(t * std::numbers::pi_v<float> * 0.5f);
}

[[nodiscard]] inline float easeOutSine(float t) noexcept
{
	return std::sin(t * std::numbers::pi_v<float> * 0.5f);
}

[[nodiscard]] inline float easeInOutSine(float t) noexcept
{
	return 0.5f * (1.0f - std::cos(std::numbers::pi_v<float> * t));
}

// ── Exponential ────────────────────────────────────────────

[[nodiscard]] inline float easeInExpo(float t) noexcept
{
	return (t <= 0.0f) ? 0.0f : std::pow(2.0f, 10.0f * (t - 1.0f));
}

[[nodiscard]] inline float easeOutExpo(float t) noexcept
{
	return (t >= 1.0f) ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
}

[[nodiscard]] inline float easeInOutExpo(float t) noexcept
{
	if (t <= 0.0f) return 0.0f;
	if (t >= 1.0f) return 1.0f;
	return (t < 0.5f)
		? 0.5f * std::pow(2.0f, 20.0f * t - 10.0f)
		: 1.0f - 0.5f * std::pow(2.0f, -20.0f * t + 10.0f);
}

// ── Circular ───────────────────────────────────────────────

[[nodiscard]] inline float easeInCirc(float t) noexcept
{
	return 1.0f - std::sqrt(1.0f - t * t);
}

[[nodiscard]] inline float easeOutCirc(float t) noexcept
{
	const float f = t - 1.0f;
	return std::sqrt(1.0f - f * f);
}

[[nodiscard]] inline float easeInOutCirc(float t) noexcept
{
	if (t < 0.5f)
	{
		return 0.5f * (1.0f - std::sqrt(1.0f - 4.0f * t * t));
	}
	const float f = 2.0f * t - 2.0f;
	return 0.5f * (std::sqrt(1.0f - f * f) + 1.0f);
}

// ── Elastic ────────────────────────────────────────────────

[[nodiscard]] inline float easeInElastic(float t) noexcept
{
	if (t <= 0.0f) return 0.0f;
	if (t >= 1.0f) return 1.0f;
	constexpr float c4 = (2.0f * std::numbers::pi_v<float>) / 3.0f;
	return -std::pow(2.0f, 10.0f * t - 10.0f)
		* std::sin((t * 10.0f - 10.75f) * c4);
}

[[nodiscard]] inline float easeOutElastic(float t) noexcept
{
	if (t <= 0.0f) return 0.0f;
	if (t >= 1.0f) return 1.0f;
	constexpr float c4 = (2.0f * std::numbers::pi_v<float>) / 3.0f;
	return std::pow(2.0f, -10.0f * t)
		* std::sin((t * 10.0f - 0.75f) * c4)
		+ 1.0f;
}

[[nodiscard]] inline float easeInOutElastic(float t) noexcept
{
	if (t <= 0.0f) return 0.0f;
	if (t >= 1.0f) return 1.0f;
	constexpr float c5 = (2.0f * std::numbers::pi_v<float>) / 4.5f;
	if (t < 0.5f)
	{
		return -0.5f * std::pow(2.0f, 20.0f * t - 10.0f)
			* std::sin((20.0f * t - 11.125f) * c5);
	}
	return 0.5f * std::pow(2.0f, -20.0f * t + 10.0f)
		* std::sin((20.0f * t - 11.125f) * c5)
		+ 1.0f;
}

// ── Back ───────────────────────────────────────────────────

[[nodiscard]] constexpr float easeInBack(float t) noexcept
{
	constexpr float c1 = 1.70158f;
	constexpr float c3 = c1 + 1.0f;
	return c3 * t * t * t - c1 * t * t;
}

[[nodiscard]] constexpr float easeOutBack(float t) noexcept
{
	constexpr float c1 = 1.70158f;
	constexpr float c3 = c1 + 1.0f;
	const float f = t - 1.0f;
	return 1.0f + c3 * f * f * f + c1 * f * f;
}

[[nodiscard]] constexpr float easeInOutBack(float t) noexcept
{
	constexpr float c1 = 1.70158f;
	constexpr float c2 = c1 * 1.525f;
	if (t < 0.5f)
	{
		return 0.5f * ((2.0f * t) * (2.0f * t) * ((c2 + 1.0f) * 2.0f * t - c2));
	}
	const float f = 2.0f * t - 2.0f;
	return 0.5f * (f * f * ((c2 + 1.0f) * f + c2) + 2.0f);
}

// ── Bounce ─────────────────────────────────────────────────

[[nodiscard]] constexpr float easeOutBounce(float t) noexcept
{
	constexpr float n1 = 7.5625f;
	constexpr float d1 = 2.75f;

	if (t < 1.0f / d1)
	{
		return n1 * t * t;
	}
	if (t < 2.0f / d1)
	{
		const float f = t - 1.5f / d1;
		return n1 * f * f + 0.75f;
	}
	if (t < 2.5f / d1)
	{
		const float f = t - 2.25f / d1;
		return n1 * f * f + 0.9375f;
	}
	const float f = t - 2.625f / d1;
	return n1 * f * f + 0.984375f;
}

[[nodiscard]] constexpr float easeInBounce(float t) noexcept
{
	return 1.0f - easeOutBounce(1.0f - t);
}

[[nodiscard]] constexpr float easeInOutBounce(float t) noexcept
{
	return (t < 0.5f)
		? 0.5f * (1.0f - easeOutBounce(1.0f - 2.0f * t))
		: 0.5f * (1.0f + easeOutBounce(2.0f * t - 1.0f));
}

// ── Cubic Bezier ───────────────────────────────────────────

/// @brief 3次ベジェ曲線によるイージング
/// @param x1 制御点1の X 座標
/// @param y1 制御点1の Y 座標
/// @param x2 制御点2の X 座標
/// @param y2 制御点2の Y 座標
/// @param t 入力値 [0.0, 1.0]
/// @return イージング後の値
/// @details CSS の cubic-bezier() と同等の動作をする。
///          ニュートン法で X 座標から t パラメータを逆算し、
///          対応する Y 値を返す。
[[nodiscard]] inline float cubicBezier(float x1, float y1,
                                       float x2, float y2,
                                       float t) noexcept
{
	if (t <= 0.0f) return 0.0f;
	if (t >= 1.0f) return 1.0f;

	/// ベジェ曲線の X 座標を計算する補助関数
	auto sampleCurveX = [x1, x2](float u) -> float
	{
		return (((-1.0f + 3.0f * x1 - 3.0f * x2 + 1.0f) * u
			+ (3.0f * x2 - 6.0f * x1 + 3.0f)) * u
			+ (3.0f * x1)) * u;
	};

	/// ベジェ曲線の X 微分を計算する補助関数
	auto sampleCurveDerivativeX = [x1, x2](float u) -> float
	{
		return (3.0f * (-1.0f + 3.0f * x1 - 3.0f * x2 + 1.0f) * u
			+ 2.0f * (3.0f * x2 - 6.0f * x1 + 3.0f)) * u
			+ 3.0f * x1;
	};

	/// ベジェ曲線の Y 座標を計算する補助関数
	auto sampleCurveY = [y1, y2](float u) -> float
	{
		return (((-1.0f + 3.0f * y1 - 3.0f * y2 + 1.0f) * u
			+ (3.0f * y2 - 6.0f * y1 + 3.0f)) * u
			+ (3.0f * y1)) * u;
	};

	/// ニュートン法で t に対応するパラメータ u を求める
	float u = t;
	for (int i = 0; i < 8; ++i)
	{
		const float currentX = sampleCurveX(u) - t;
		const float derivative = sampleCurveDerivativeX(u);
		if (std::abs(derivative) < 1e-6f)
		{
			break;
		}
		u -= currentX / derivative;
	}
	u = std::clamp(u, 0.0f, 1.0f);

	return sampleCurveY(u);
}

// ── Spring ─────────────────────────────────────────────────

/// @brief バネ物理によるイージング
/// @param damping 減衰係数（0.0 で無減衰、1.0 で臨界減衰）
/// @param frequency 振動周波数
/// @param t 入力値 [0.0, 1.0]
/// @return イージング後の値
[[nodiscard]] inline float spring(float damping, float frequency, float t) noexcept
{
	if (t <= 0.0f) return 0.0f;
	if (t >= 1.0f) return 1.0f;

	const float omega = frequency * 2.0f * std::numbers::pi_v<float>;
	const float dampedOmega = omega * std::sqrt(1.0f - damping * damping);
	const float decay = std::exp(-damping * omega * t);
	return 1.0f - decay * std::cos(dampedOmega * t);
}

// ── ディスパッチ ───────────────────────────────────────────

/// @brief EasingType に応じたイージング関数を適用する
/// @param type イージングの種類
/// @param t 入力値 [0.0, 1.0]
/// @return イージング後の値
[[nodiscard]] inline float apply(EasingType type, float t) noexcept
{
	switch (type)
	{
	case EasingType::Linear:           return linear(t);
	case EasingType::EaseInQuad:       return easeInQuad(t);
	case EasingType::EaseOutQuad:      return easeOutQuad(t);
	case EasingType::EaseInOutQuad:    return easeInOutQuad(t);
	case EasingType::EaseInCubic:      return easeInCubic(t);
	case EasingType::EaseOutCubic:     return easeOutCubic(t);
	case EasingType::EaseInOutCubic:   return easeInOutCubic(t);
	case EasingType::EaseInQuart:      return easeInQuart(t);
	case EasingType::EaseOutQuart:     return easeOutQuart(t);
	case EasingType::EaseInOutQuart:   return easeInOutQuart(t);
	case EasingType::EaseInQuint:      return easeInQuint(t);
	case EasingType::EaseOutQuint:     return easeOutQuint(t);
	case EasingType::EaseInOutQuint:   return easeInOutQuint(t);
	case EasingType::EaseInSine:       return easeInSine(t);
	case EasingType::EaseOutSine:      return easeOutSine(t);
	case EasingType::EaseInOutSine:    return easeInOutSine(t);
	case EasingType::EaseInExpo:       return easeInExpo(t);
	case EasingType::EaseOutExpo:      return easeOutExpo(t);
	case EasingType::EaseInOutExpo:    return easeInOutExpo(t);
	case EasingType::EaseInCirc:       return easeInCirc(t);
	case EasingType::EaseOutCirc:      return easeOutCirc(t);
	case EasingType::EaseInOutCirc:    return easeInOutCirc(t);
	case EasingType::EaseInElastic:    return easeInElastic(t);
	case EasingType::EaseOutElastic:   return easeOutElastic(t);
	case EasingType::EaseInOutElastic: return easeInOutElastic(t);
	case EasingType::EaseInBack:       return easeInBack(t);
	case EasingType::EaseOutBack:      return easeOutBack(t);
	case EasingType::EaseInOutBack:    return easeInOutBack(t);
	case EasingType::EaseInBounce:     return easeInBounce(t);
	case EasingType::EaseOutBounce:    return easeOutBounce(t);
	case EasingType::EaseInOutBounce:  return easeInOutBounce(t);
	}
	return t;
}

} // namespace Easing

} // namespace mitiru::vn
