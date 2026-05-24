#pragma once

/// @file Button.hpp
/// @brief ボタン評価ユーティリティ
///
/// 矩形ボタンの状態とクリック判定を一括で評価する。
/// WidgetState.hppの関数群を合成した便利関数。
///
/// @code
/// using namespace sgc::ui;
/// auto result = evaluateButton(buttonBounds, mousePos, mouseDown, mousePressed);
/// if (result.clicked) { /* ボタンが押された */ }
/// // result.state で描画スタイルを切り替え
/// @endcode

#include "sgc/ui/WidgetState.hpp"

namespace sgc::ui
{

/// @brief ボタン評価結果
struct ButtonResult
{
	WidgetState state{WidgetState::Normal};  ///< ボタンの視覚状態
	bool clicked{false};                      ///< このフレームでクリックされたか
};

/// @brief 矩形ボタンの状態とクリックを評価する
///
/// isMouseOver + resolveWidgetState + isClicked を合成した便利関数。
/// 1回の呼び出しで視覚状態とクリック判定の両方を得られる。
///
/// @param bounds ボタン矩形
/// @param mousePos マウス座標
/// @param mouseDown マウスボタンが押下中か（視覚状態のPressed判定用）
/// @param mousePressed マウスボタンがこのフレームで押されたか（クリック判定用）
/// @param enabled ボタンが有効か（デフォルト: true）
/// @return ボタン状態とクリック判定の結果
[[nodiscard]] constexpr ButtonResult evaluateButton(
	const Rectf& bounds, const Vec2f& mousePos,
	bool mouseDown, bool mousePressed,
	bool enabled = true) noexcept
{
	if (!enabled)
	{
		return {WidgetState::Disabled, false};
	}

	const bool hovered = isMouseOver(mousePos, bounds);
	const bool pressed = hovered && mouseDown;
	const bool clicked = hovered && mousePressed;
	const auto state = resolveWidgetState(true, hovered, pressed);

	return {state, clicked};
}

} // namespace sgc::ui
