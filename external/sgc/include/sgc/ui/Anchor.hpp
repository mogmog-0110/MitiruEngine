#pragma once

/// @file Anchor.hpp
/// @brief アンカーベースのレイアウトシステム
///
/// 9点アンカー、マージン/パディング、配置計算関数を提供する。
/// Rectfを基本とし、UI要素の配置計算を行う。
///
/// @code
/// using namespace sgc::ui;
/// auto screen = screenRect(800.0f, 600.0f);
/// auto btn = alignedRect(screen, {200, 40}, Anchor::BottomCenter, Margin::uniform(20));
/// @endcode

#include "sgc/math/Rect.hpp"

namespace sgc::ui
{

/// @brief 9点アンカー
enum class Anchor
{
	TopLeft,      ///< 左上
	TopCenter,    ///< 上中央
	TopRight,     ///< 右上
	CenterLeft,   ///< 左中央
	Center,       ///< 中央
	CenterRight,  ///< 右中央
	BottomLeft,   ///< 左下
	BottomCenter, ///< 下中央
	BottomRight   ///< 右下
};

/// @brief 4辺マージン/パディング
///
/// UI要素の余白を4辺個別に指定する。
/// static ファクトリで均一・対称・ゼロマージンを簡潔に生成できる。
struct Margin
{
	float top{};     ///< 上マージン
	float right{};   ///< 右マージン
	float bottom{};  ///< 下マージン
	float left{};    ///< 左マージン

	/// @brief 全辺同一マージン
	/// @param v マージン値
	[[nodiscard]] static constexpr Margin uniform(float v) noexcept
	{
		return {v, v, v, v};
	}

	/// @brief 水平・垂直マージン
	/// @param horizontal 左右マージン
	/// @param vertical 上下マージン
	[[nodiscard]] static constexpr Margin symmetric(float horizontal, float vertical) noexcept
	{
		return {vertical, horizontal, vertical, horizontal};
	}

	/// @brief ゼロマージン
	[[nodiscard]] static constexpr Margin zero() noexcept
	{
		return {0.0f, 0.0f, 0.0f, 0.0f};
	}

	[[nodiscard]] constexpr bool operator==(const Margin&) const noexcept = default;
};

/// @brief パディング（Marginのセマンティックエイリアス）
using Padding = Margin;

/// @brief 親領域内でアンカー基準点を取得する
/// @param parent 親矩形
/// @param anchor アンカー位置
/// @return アンカーポイント座標
[[nodiscard]] constexpr Vec2f anchorPoint(
	const Rectf& parent, Anchor anchor) noexcept
{
	const float cx = parent.x() + parent.width() * 0.5f;
	const float cy = parent.y() + parent.height() * 0.5f;

	switch (anchor)
	{
	case Anchor::TopLeft:      return {parent.left(), parent.top()};
	case Anchor::TopCenter:    return {cx, parent.top()};
	case Anchor::TopRight:     return {parent.right(), parent.top()};
	case Anchor::CenterLeft:   return {parent.left(), cy};
	case Anchor::Center:       return {cx, cy};
	case Anchor::CenterRight:  return {parent.right(), cy};
	case Anchor::BottomLeft:   return {parent.left(), parent.bottom()};
	case Anchor::BottomCenter: return {cx, parent.bottom()};
	case Anchor::BottomRight:  return {parent.right(), parent.bottom()};
	}
	return {parent.left(), parent.top()};
}

/// @brief 親領域内でアンカー基準 + オフセットの位置を計算する
/// @param parent 親矩形
/// @param anchor アンカー位置
/// @param offset アンカーからのオフセット
/// @return 計算された座標
[[nodiscard]] constexpr Vec2f anchoredPosition(
	const Rectf& parent, Anchor anchor,
	const Vec2f& offset = {}) noexcept
{
	return anchorPoint(parent, anchor) + offset;
}

/// @brief 子要素を親領域内にアンカー整列で配置する
///
/// アンカーポイントを基準に子要素を整列し、マージンを適用する。
/// - TopLeft系: 左上がアンカー基準（+マージン方向へオフセット）
/// - Center系: 中心がアンカー基準
/// - BottomRight系: 右下がアンカー基準（−マージン方向へオフセット）
///
/// @param parent 親矩形
/// @param childSize 子要素のサイズ
/// @param anchor アンカー位置
/// @param margin 余白
/// @return 配置された子要素の矩形
[[nodiscard]] constexpr Rectf alignedRect(
	const Rectf& parent, const Vec2f& childSize,
	Anchor anchor, const Margin& margin = {}) noexcept
{
	const Vec2f ap = anchorPoint(parent, anchor);

	float x = ap.x;
	float y = ap.y;

	// 水平配置
	switch (anchor)
	{
	case Anchor::TopLeft:
	case Anchor::CenterLeft:
	case Anchor::BottomLeft:
		x += margin.left;
		break;
	case Anchor::TopCenter:
	case Anchor::Center:
	case Anchor::BottomCenter:
		x -= childSize.x * 0.5f;
		break;
	case Anchor::TopRight:
	case Anchor::CenterRight:
	case Anchor::BottomRight:
		x -= childSize.x + margin.right;
		break;
	}

	// 垂直配置
	switch (anchor)
	{
	case Anchor::TopLeft:
	case Anchor::TopCenter:
	case Anchor::TopRight:
		y += margin.top;
		break;
	case Anchor::CenterLeft:
	case Anchor::Center:
	case Anchor::CenterRight:
		y -= childSize.y * 0.5f;
		break;
	case Anchor::BottomLeft:
	case Anchor::BottomCenter:
	case Anchor::BottomRight:
		y -= childSize.y + margin.bottom;
		break;
	}

	return Rectf{{x, y}, childSize};
}

/// @brief マージンを適用して縮小した領域を返す
/// @param rect 元の矩形
/// @param margin 適用するマージン
/// @return マージン適用後の矩形
[[nodiscard]] constexpr Rectf applyMargin(
	const Rectf& rect, const Margin& margin) noexcept
{
	const float w = rect.width() - margin.left - margin.right;
	const float h = rect.height() - margin.top - margin.bottom;
	return Rectf{
		{rect.x() + margin.left, rect.y() + margin.top},
		{(w > 0.0f ? w : 0.0f), (h > 0.0f ? h : 0.0f)}
	};
}

/// @brief パディングを適用して内側に縮小した領域を返す
///
/// applyMarginと同一の計算だが、意図を明示するためのエイリアス関数。
///
/// @param rect 元の矩形
/// @param padding 適用するパディング
/// @return パディング適用後の矩形
[[nodiscard]] constexpr Rectf applyPadding(
	const Rectf& rect, const Padding& padding) noexcept
{
	return applyMargin(rect, padding);
}

/// @brief 画面サイズからRectfを生成する
/// @param width 画面幅
/// @param height 画面高さ
/// @return 左上(0,0)からのRectf
[[nodiscard]] constexpr Rectf screenRect(
	float width, float height) noexcept
{
	return Rectf{{0.0f, 0.0f}, {width, height}};
}

} // namespace sgc::ui
