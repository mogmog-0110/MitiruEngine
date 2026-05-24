#pragma once

/// @file NumericInput.hpp
/// @brief 数値入力（スピナー）評価ユーティリティ
///
/// +/-ボタンと値表示を持つ数値入力ウィジェットの状態を一括で評価する。
/// WidgetState.hppの関数群を合成した便利関数。
///
/// @code
/// using namespace sgc::ui;
/// NumericInputState state{50.0f, 0.0f, 100.0f, 1.0f};
/// auto result = evaluateNumericInput(bounds, mousePos, mouseDown, mousePressed, state);
/// if (result.changed) { state.value = result.value; }
/// @endcode

#include "sgc/ui/WidgetState.hpp"

#include <algorithm>

namespace sgc::ui
{

/// @brief 数値入力の評価結果
struct NumericInputResult
{
	float value{0.0f};                        ///< 現在の値
	bool changed{false};                      ///< 値が変化したか
	bool minusFocused{false};                 ///< マイナスボタンにフォーカス
	bool plusFocused{false};                   ///< プラスボタンにフォーカス
	WidgetState state{WidgetState::Normal};   ///< ウィジェット全体の視覚状態
};

/// @brief 数値入力のマイナスボタン矩形を計算する
///
/// 全体矩形の左側1/4をマイナスボタン領域とする。
///
/// @param bounds ウィジェット全体の矩形
/// @return マイナスボタンの矩形
[[nodiscard]] constexpr Rectf numericInputMinusRect(const Rectf& bounds) noexcept
{
	const float buttonWidth = bounds.width() * 0.25f;
	return {bounds.x(), bounds.y(), buttonWidth, bounds.height()};
}

/// @brief 数値入力のプラスボタン矩形を計算する
///
/// 全体矩形の右側1/4をプラスボタン領域とする。
///
/// @param bounds ウィジェット全体の矩形
/// @return プラスボタンの矩形
[[nodiscard]] constexpr Rectf numericInputPlusRect(const Rectf& bounds) noexcept
{
	const float buttonWidth = bounds.width() * 0.25f;
	return {bounds.x() + bounds.width() - buttonWidth, bounds.y(), buttonWidth, bounds.height()};
}

/// @brief 数値入力の値表示領域矩形を計算する
///
/// 全体矩形の中央50%を値表示領域とする。
///
/// @param bounds ウィジェット全体の矩形
/// @return 値表示領域の矩形
[[nodiscard]] constexpr Rectf numericInputValueRect(const Rectf& bounds) noexcept
{
	const float buttonWidth = bounds.width() * 0.25f;
	return {bounds.x() + buttonWidth, bounds.y(), bounds.width() - buttonWidth * 2.0f, bounds.height()};
}

/// @brief 数値入力の状態と値を評価する
///
/// +/-ボタンのクリック判定と値のクランプを行う。
/// ボタン領域は左右25%ずつ、中央50%が値表示領域。
///
/// @param bounds ウィジェット全体の矩形
/// @param mousePos マウス座標
/// @param mouseDown マウスボタンが押下中か
/// @param mousePressed マウスボタンがこのフレームで押されたか
/// @param currentValue 現在の値
/// @param minValue 最小値
/// @param maxValue 最大値
/// @param step 増減ステップ量
/// @param enabled ウィジェットが有効か（デフォルト: true）
/// @return 数値入力の状態・値・変化フラグ
[[nodiscard]] constexpr NumericInputResult evaluateNumericInput(
	const Rectf& bounds, const Vec2f& mousePos,
	bool mouseDown, bool mousePressed,
	float currentValue, float minValue, float maxValue,
	float step = 1.0f, bool enabled = true) noexcept
{
	if (!enabled)
	{
		return {currentValue, false, false, false, WidgetState::Disabled};
	}

	const auto minusRect = numericInputMinusRect(bounds);
	const auto plusRect = numericInputPlusRect(bounds);

	const bool minusHovered = isMouseOver(mousePos, minusRect);
	const bool plusHovered = isMouseOver(mousePos, plusRect);
	const bool anyHovered = isMouseOver(mousePos, bounds);

	const bool minusClicked = minusHovered && mousePressed;
	const bool plusClicked = plusHovered && mousePressed;

	float newValue = currentValue;
	if (minusClicked)
	{
		newValue = currentValue - step;
	}
	else if (plusClicked)
	{
		newValue = currentValue + step;
	}

	// クランプ
	if (newValue < minValue)
	{
		newValue = minValue;
	}
	if (newValue > maxValue)
	{
		newValue = maxValue;
	}

	const bool pressed = anyHovered && mouseDown;
	const auto state = resolveWidgetState(true, anyHovered, pressed);
	const bool changed = (newValue != currentValue);

	return {newValue, changed, minusHovered, plusHovered, state};
}

} // namespace sgc::ui
