#pragma once

/// @file GamepadAxisMath.hpp
/// @brief パッドの生の軸値を InputSnapshot の値 (stick は [-1,1]、trigger は [0,1]) へ直す計算。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include <mitiru/input/StickShaping.hpp>
#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::input
{

/// 0.24 は XInput の推奨値 (XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE 7849 / 32767) に合わせた
inline constexpr float kStickDeadZone = 0.24f;

/// 負側は -32768 まであるので、正負で割る数を変えないと -1 を超える
[[nodiscard]] inline float normalizeStick(std::int16_t raw) noexcept
{
	return raw >= 0 ? static_cast<float>(raw) / 32767.0f : static_cast<float>(raw) / 32768.0f;
}

/// 閾値の外側を [0,1] へ広げ直す。切り捨てるだけだと、閾値を越えた瞬間に値が 0 から 0.24 へ跳ぶ
[[nodiscard]] inline float applyDeadZone(float value, float threshold) noexcept
{
	const float t = std::clamp(threshold, 0.0f, 0.99f);  // 閾値が 1 だと 0 で割ることになる
	const float a = std::abs(value);
	if (a <= t) { return 0.0f; }
	const float sign = value >= 0.0f ? 1.0f : -1.0f;
	return sign * (a - t) / (1.0f - t);
}

/// SDL の stick は下が正なので、Y 軸だけ符号を返して上を正にする
[[nodiscard]] inline float stickAxis(std::int16_t raw, bool isY, float deadZone = kStickDeadZone) noexcept
{
	const float v = normalizeStick(raw);
	return applyDeadZone(isY ? -v : v, deadZone);
}

/// SDL の trigger は 0..32767。ここでは掛けない (閾値は shapePadAxes が設定から掛ける)
[[nodiscard]] inline float triggerAxis(std::int16_t raw) noexcept
{
	return std::clamp(static_cast<float>(raw) / 32767.0f, 0.0f, 1.0f);
}

/// @brief SDL の生の軸 6 本 (module::gamepad::Axis の順) を、設定の整形を通した InputSnapshot の値にする。
/// @details スティックは X と Y を組にして円形に整える。Y は SDL が下を正にするので、整形の前に上を正へ直す。
[[nodiscard]] inline std::array<float, module::gamepad::AxisCount> shapePadAxes(
	const std::array<std::int16_t, module::gamepad::AxisCount>& raw, const StickShaping& shaping) noexcept
{
	namespace pad = module::gamepad;
	std::array<float, pad::AxisCount> out{};
	const StickXY left = shapeLeftStick(normalizeStick(raw[pad::LeftStickX]), -normalizeStick(raw[pad::LeftStickY]), shaping);
	const StickXY right = shapeRightStick(normalizeStick(raw[pad::RightStickX]), -normalizeStick(raw[pad::RightStickY]), shaping);
	out[pad::LeftStickX] = left.x;
	out[pad::LeftStickY] = left.y;
	out[pad::RightStickX] = right.x;
	out[pad::RightStickY] = right.y;
	out[pad::LeftTrigger] = shapeTrigger(triggerAxis(raw[pad::LeftTrigger]), shaping.triggerThreshold);
	out[pad::RightTrigger] = shapeTrigger(triggerAxis(raw[pad::RightTrigger]), shaping.triggerThreshold);
	return out;
}

[[nodiscard]] inline std::uint16_t rumbleMagnitude(float strength01) noexcept
{
	return static_cast<std::uint16_t>(std::lround(std::clamp(strength01, 0.0f, 1.0f) * 65535.0f));
}

}  // namespace mitiru::input
