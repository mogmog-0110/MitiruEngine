#pragma once

/// @file RadioButton.hpp
/// @brief ラジオボタン評価ユーティリティ
///
/// ラジオボタンの状態と選択判定を一括で評価する。
/// WidgetState.hppの関数群を合成した便利関数。
///
/// @code
/// using namespace sgc::ui;
/// for (int i = 0; i < optionCount; ++i)
/// {
///     auto result = evaluateRadio(radioBounds[i], mousePos, mouseDown, mousePressed,
///                                 i, selectedIndex);
///     if (result.changed) { selectedIndex = result.selected; }
///     // result.state で描画スタイルを切り替え
///     // radioCircleCenter() で円の中心を取得して描画
/// }
/// @endcode

#include "sgc/ui/WidgetState.hpp"

namespace sgc::ui
{

/// @brief ラジオボタン評価結果
struct RadioResult
{
	WidgetState state{WidgetState::Normal};  ///< ラジオボタンの視覚状態
	int selected{0};                          ///< 選択中のインデックス
	bool changed{false};                      ///< このフレームで選択が変更されたか
};

/// @brief ラジオボタンの状態と選択を評価する
///
/// isMouseOver + resolveWidgetState + クリック判定を合成した便利関数。
/// クリック時に選択インデックスを自身のインデックスに変更する。
///
/// @param bounds ラジオボタン矩形
/// @param mousePos マウス座標
/// @param mouseDown マウスボタンが押下中か（視覚状態のPressed判定用）
/// @param mousePressed マウスボタンがこのフレームで押されたか（選択判定用）
/// @param index このラジオボタンのインデックス
/// @param selectedIndex 現在選択中のインデックス
/// @param enabled ラジオボタンが有効か（デフォルト: true）
/// @return ラジオボタンの状態・選択インデックス・変更フラグ
[[nodiscard]] constexpr RadioResult evaluateRadio(
	const Rectf& bounds, const Vec2f& mousePos,
	bool mouseDown, bool mousePressed,
	int index, int selectedIndex,
	bool enabled = true) noexcept
{
	if (!enabled)
	{
		return {WidgetState::Disabled, selectedIndex, false};
	}

	const bool hovered = isMouseOver(mousePos, bounds);
	const bool pressed = hovered && mouseDown;
	const bool clicked = hovered && mousePressed;
	const auto state = resolveWidgetState(true, hovered, pressed);
	const bool isNewSelection = clicked && (index != selectedIndex);
	const int newSelected = isNewSelection ? index : selectedIndex;

	return {state, newSelected, isNewSelection};
}

/// @brief ラジオボタンの円の中心座標を計算する
///
/// 矩形の左辺中央にラジオボタンの円を配置するための中心座標を返す。
/// 円の半径は矩形の高さの半分とする。
///
/// @param bounds ラジオボタン矩形
/// @return 円の中心座標
[[nodiscard]] constexpr Vec2f radioCircleCenter(const Rectf& bounds) noexcept
{
	const float radius = bounds.height() / 2.0f;
	return {bounds.x() + radius, bounds.y() + radius};
}

} // namespace sgc::ui
