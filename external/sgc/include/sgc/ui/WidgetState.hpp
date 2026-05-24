#pragma once

/// @file WidgetState.hpp
/// @brief ウィジェット状態管理とヒットテストユーティリティ
///
/// UI要素の視覚状態の解決とマウスヒットテストを提供する。
/// フレームワーク非依存で、IInputProviderと組み合わせて使用する。
///
/// @code
/// auto mousePos = input.mousePosition();
/// bool hovered = sgc::ui::isMouseOver(mousePos, buttonRect);
/// bool pressed = hovered && input.isMouseButtonDown(IInputProvider::MOUSE_LEFT);
/// auto state = sgc::ui::resolveWidgetState(true, hovered, pressed);
/// @endcode

#include "sgc/math/Rect.hpp"
#include "sgc/math/Vec2.hpp"

namespace sgc::ui
{

/// @brief ウィジェットの視覚状態
enum class WidgetState
{
	Normal,    ///< 通常
	Hovered,   ///< マウスホバー中
	Pressed,   ///< 押下中
	Focused,   ///< フォーカス中（キーボード選択）
	Disabled   ///< 無効化
};

/// @brief 入力状態からウィジェット状態を解決する
///
/// 優先度: Disabled > Pressed > Hovered > Focused > Normal
///
/// @param isEnabled 有効か
/// @param isHovered マウスが領域内か
/// @param isPressed マウスボタンが押されているか
/// @param isFocused キーボードフォーカスがあるか
/// @return 解決されたウィジェット状態
[[nodiscard]] constexpr WidgetState resolveWidgetState(
	bool isEnabled,
	bool isHovered,
	bool isPressed,
	bool isFocused = false) noexcept
{
	if (!isEnabled)
	{
		return WidgetState::Disabled;
	}
	if (isPressed)
	{
		return WidgetState::Pressed;
	}
	if (isHovered)
	{
		return WidgetState::Hovered;
	}
	if (isFocused)
	{
		return WidgetState::Focused;
	}
	return WidgetState::Normal;
}

// ── ヒットテストユーティリティ ──────────────────────────

/// @brief 矩形内のマウスヒットテスト
///
/// Rectf::contains()のラッパーだが、UIコードでの意図を明確にする。
///
/// @param mousePos マウス座標
/// @param rect 判定対象矩形
/// @return マウスが矩形内にあればtrue
[[nodiscard]] constexpr bool isMouseOver(
	const Vec2f& mousePos, const Rectf& rect) noexcept
{
	return rect.contains(mousePos);
}

/// @brief 円形内のマウスヒットテスト
/// @param mousePos マウス座標
/// @param center 円の中心
/// @param radius 円の半径
/// @return マウスが円内にあればtrue
[[nodiscard]] constexpr bool isMouseOverCircle(
	const Vec2f& mousePos, const Vec2f& center, float radius) noexcept
{
	return mousePos.distanceSquaredTo(center) <= radius * radius;
}

/// @brief マウスクリック判定（ホバー中 かつ ボタンが押された）
///
/// 典型的なボタンクリック判定のヘルパー。
///
/// @param mousePos マウス座標
/// @param rect 判定対象矩形
/// @param mouseButtonPressed マウスボタンがこのフレームで押されたか
/// @return クリックされたとみなせればtrue
[[nodiscard]] constexpr bool isClicked(
	const Vec2f& mousePos, const Rectf& rect,
	bool mouseButtonPressed) noexcept
{
	return mouseButtonPressed && rect.contains(mousePos);
}

} // namespace sgc::ui
