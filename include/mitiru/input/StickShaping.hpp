#pragma once

/// @file StickShaping.hpp
/// @brief スティックとトリガーの整形を宣言する。
/// @details host が InputSnapshot を作る前に、円形のデッドゾーン、外側の飽和、応答曲線を適用する。録画には整形後の値が記録されるため、設定を変えても過去の録画の再生は変わらない。--input-script の軸には整形を適用せず、台本の値をそのまま使う。

#include <algorithm>
#include <cmath>

namespace mitiru::input
{

/// 既定値は XInput の値。左 7849、右 8689、トリガー 30 を、それぞれ 32767、32767、255 で割る。
struct StickShaping
{
	float innerLeft = 0.24f;             ///< 左スティックの内側。この大きさ以下は 0
	float innerRight = 0.265f;           ///< 右スティック
	float outer = 1.0f;                  ///< この大きさ以上は 1。個体差で端まで倒れないパッドを救う
	float curve = 1.0f;                  ///< 内側と外側の間に掛ける指数。1 で直線、大きいほど小さな倒しが鈍い
	float triggerThreshold = 30.0f / 255.0f;

	[[nodiscard]] bool operator==(const StickShaping&) const noexcept = default;
};

struct StickXY
{
	float x = 0.0f;
	float y = 0.0f;
};

/// @brief 円形のデッドゾーンと外側の飽和を適用する。向きは変えず、大きさだけを直す。
/// @details 軸ごとに区切ると、斜め入力の大きさが一定にならない。傾きが急になりすぎないよう、外側は内側から 0.05 以上離す。
[[nodiscard]] inline StickXY shapeStick(float x, float y, float inner, float outer, float curve) noexcept
{
	const float magnitude = std::hypot(x, y);
	const float lo = std::clamp(inner, 0.0f, 0.9f);
	if (magnitude <= lo) { return {}; }
	const float hi = std::clamp(outer, lo + 0.05f, 1.0f);
	float t = std::clamp((magnitude - lo) / (hi - lo), 0.0f, 1.0f);
	if (curve != 1.0f) { t = std::pow(t, std::clamp(curve, 0.5f, 3.0f)); }
	const float scale = t / magnitude;
	return { x * scale, y * scale };
}

[[nodiscard]] inline StickXY shapeLeftStick(float x, float y, const StickShaping& s) noexcept
{
	return shapeStick(x, y, s.innerLeft, s.outer, s.curve);
}

[[nodiscard]] inline StickXY shapeRightStick(float x, float y, const StickShaping& s) noexcept
{
	return shapeStick(x, y, s.innerRight, s.outer, s.curve);
}

/// 閾値以下を 0 にし、その上を [0,1] へ広げ直す。半押しの量は残る。
[[nodiscard]] inline float shapeTrigger(float value01, float threshold) noexcept
{
	const float t = std::clamp(threshold, 0.0f, 0.9f);
	const float v = std::clamp(value01, 0.0f, 1.0f);
	return v <= t ? 0.0f : (v - t) / (1.0f - t);
}

}  // namespace mitiru::input
