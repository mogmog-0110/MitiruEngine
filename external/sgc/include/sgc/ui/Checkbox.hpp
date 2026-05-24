#pragma once

/// @file Checkbox.hpp
/// @brief チェックボックス評価ユーティリティ
///
/// チェックボックスの状態とトグル判定を一括で評価する。
/// WidgetState.hppの関数群を合成した便利関数。
///
/// @code
/// using namespace sgc::ui;
/// auto result = evaluateCheckbox(checkBounds, mousePos, mouseDown, mousePressed, isChecked);
/// if (result.toggled) { isChecked = result.checked; }
/// // result.state で描画スタイルを切り替え
/// @endcode

#include "sgc/ui/WidgetState.hpp"

namespace sgc::ui
{

/// @brief チェックボックス評価結果
struct CheckboxResult
{
	WidgetState state{WidgetState::Normal};  ///< チェックボックスの視覚状態
	bool checked{false};                      ///< チェック状態
	bool toggled{false};                      ///< このフレームでトグルされたか
};

/// @brief チェックボックスの状態とトグルを評価する
///
/// isMouseOver + resolveWidgetState + クリック判定を合成した便利関数。
/// クリック時に現在のチェック状態を反転する。
///
/// @param bounds チェックボックス矩形
/// @param mousePos マウス座標
/// @param mouseDown マウスボタンが押下中か（視覚状態のPressed判定用）
/// @param mousePressed マウスボタンがこのフレームで押されたか（トグル判定用）
/// @param currentChecked 現在のチェック状態
/// @param enabled チェックボックスが有効か（デフォルト: true）
/// @return チェックボックスの状態・チェック状態・トグルフラグ
[[nodiscard]] constexpr CheckboxResult evaluateCheckbox(
	const Rectf& bounds, const Vec2f& mousePos,
	bool mouseDown, bool mousePressed,
	bool currentChecked, bool enabled = true) noexcept
{
	if (!enabled)
	{
		return {WidgetState::Disabled, currentChecked, false};
	}

	const bool hovered = isMouseOver(mousePos, bounds);
	const bool pressed = hovered && mouseDown;
	const bool clicked = hovered && mousePressed;
	const auto state = resolveWidgetState(true, hovered, pressed);
	const bool newChecked = clicked ? !currentChecked : currentChecked;

	return {state, newChecked, clicked};
}

} // namespace sgc::ui
