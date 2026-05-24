#pragma once

/// @file Badge.hpp
/// @brief バッジ配置ユーティリティ
///
/// ウィジェットの角や辺にバッジ（通知数等）を配置する計算関数を提供する。
/// Anchor を基準にバッジ矩形を算出する純粋関数群。
///
/// @code
/// using namespace sgc::ui;
/// auto base = sgc::Rectf{{100, 100}, {200, 40}};
/// auto badge = evaluateBadge(base, Anchor::TopRight, {24, 24}, {4, -4});
/// auto textBadge = badgeFromText(base, Anchor::TopRight, 30.0f, 16.0f, 6.0f, 4.0f, {4, -4});
/// @endcode

#include "sgc/ui/Anchor.hpp"

namespace sgc::ui
{

/// @brief バッジ矩形を算出する
///
/// baseRect のアンカー位置を基準にバッジを配置する。
/// アンカーポイントがバッジの中心となるよう計算し、offset で微調整する。
///
/// @param baseRect 親ウィジェットの矩形
/// @param anchor バッジを配置するアンカー位置
/// @param badgeSize バッジのサイズ (幅, 高さ)
/// @param offset アンカー位置からの追加オフセット
/// @return 配置されたバッジの矩形
[[nodiscard]] constexpr Rectf evaluateBadge(
	const Rectf& baseRect,
	Anchor anchor,
	const Vec2f& badgeSize,
	const Vec2f& offset = {}) noexcept
{
	const Vec2f ap = anchorPoint(baseRect, anchor);
	const float bx = ap.x - badgeSize.x * 0.5f + offset.x;
	const float by = ap.y - badgeSize.y * 0.5f + offset.y;
	return Rectf{{bx, by}, badgeSize};
}

/// @brief テキスト寸法からバッジ矩形を算出する
///
/// テキストの幅・高さにパディングを加えてバッジサイズを決定し、
/// evaluateBadge で配置する。
///
/// @param baseRect 親ウィジェットの矩形
/// @param anchor バッジを配置するアンカー位置
/// @param textWidth テキスト描画幅
/// @param textHeight テキスト描画高さ
/// @param hPadding 水平パディング（左右合計ではなく片側分）
/// @param vPadding 垂直パディング（上下合計ではなく片側分）
/// @param offset アンカー位置からの追加オフセット
/// @return 配置されたバッジの矩形
[[nodiscard]] constexpr Rectf badgeFromText(
	const Rectf& baseRect,
	Anchor anchor,
	float textWidth,
	float textHeight,
	float hPadding,
	float vPadding,
	const Vec2f& offset = {}) noexcept
{
	const Vec2f badgeSize{textWidth + hPadding * 2.0f, textHeight + vPadding * 2.0f};
	return evaluateBadge(baseRect, anchor, badgeSize, offset);
}

} // namespace sgc::ui
