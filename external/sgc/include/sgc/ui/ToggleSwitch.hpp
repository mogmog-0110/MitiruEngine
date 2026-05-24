#pragma once

/// @file ToggleSwitch.hpp
/// @brief トグルスイッチ評価ユーティリティ
///
/// トグルスイッチ（ON/OFFスイッチ）の状態とトグル判定を一括で評価する。
/// WidgetState.hppの関数群を合成した便利関数。
///
/// @code
/// using namespace sgc::ui;
/// auto result = evaluateToggle(toggleBounds, mousePos, mouseDown, mousePressed, isOn);
/// if (result.toggled) { isOn = result.value; }
/// // result.state で描画スタイルを切り替え
/// // toggleKnobRect() でノブ矩形を取得して描画
/// @endcode

#include "sgc/ui/WidgetState.hpp"

namespace sgc::ui
{

/// @brief トグルスイッチ評価結果
struct ToggleResult
{
	WidgetState state{WidgetState::Normal};  ///< トグルスイッチの視覚状態
	bool value{false};                        ///< ON/OFF状態
	bool toggled{false};                      ///< このフレームでトグルされたか
};

/// @brief トグルスイッチの状態とON/OFFを評価する
///
/// isMouseOver + resolveWidgetState + クリック判定を合成した便利関数。
/// クリック時に現在のON/OFF状態を反転する。
///
/// @param bounds トグルスイッチ全体の矩形（トラック領域）
/// @param mousePos マウス座標
/// @param mouseDown マウスボタンが押下中か（視覚状態のPressed判定用）
/// @param mousePressed マウスボタンがこのフレームで押されたか（トグル判定用）
/// @param isOn 現在のON/OFF状態
/// @param enabled トグルスイッチが有効か（デフォルト: true）
/// @return トグルスイッチの状態・ON/OFF値・トグルフラグ
[[nodiscard]] constexpr ToggleResult evaluateToggle(
	const Rectf& bounds, const Vec2f& mousePos,
	bool mouseDown, bool mousePressed,
	bool isOn, bool enabled = true) noexcept
{
	if (!enabled)
	{
		return {WidgetState::Disabled, isOn, false};
	}

	const bool hovered = isMouseOver(mousePos, bounds);
	const bool pressed = hovered && mouseDown;
	const bool clicked = hovered && mousePressed;
	const auto state = resolveWidgetState(true, hovered, pressed);
	const bool newValue = clicked ? !isOn : isOn;

	return {state, newValue, clicked};
}

/// @brief トグルスイッチのノブ矩形を計算する
///
/// トラック内でノブの位置をON/OFF状態に応じて左右に配置する。
/// ノブは正方形で、高さはトラックの高さと同じ。
/// OFF時は左端、ON時は右端に配置される。
///
/// @param bounds トグルスイッチ全体の矩形（トラック領域）
/// @param isOn 現在のON/OFF状態
/// @return ノブの矩形
[[nodiscard]] constexpr Rectf toggleKnobRect(
	const Rectf& bounds, bool isOn) noexcept
{
	const float knobSize = bounds.height();
	const float knobX = isOn
		? bounds.x() + bounds.width() - knobSize
		: bounds.x();
	return {knobX, bounds.y(), knobSize, knobSize};
}

} // namespace sgc::ui
