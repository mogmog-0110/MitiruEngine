#pragma once

/// @file Panel.hpp
/// @brief パネルレイアウト評価ユーティリティ
///
/// パネル（タイトルバー付きコンテナ）の領域分割を計算する。
/// タイトルバーとコンテンツ領域の矩形を一括で算出する。
///
/// @code
/// using namespace sgc::ui;
/// auto pad = Margin::uniform(8.0f);
/// auto result = evaluatePanel(panelBounds, 32.0f, pad);
/// // result.titleBounds でタイトルバーを描画
/// // result.contentBounds でコンテンツ領域を描画
/// @endcode

#include "sgc/ui/Anchor.hpp"

namespace sgc::ui
{

/// @brief パネル評価結果
struct PanelResult
{
	Rectf titleBounds{};    ///< タイトルバー矩形（titleHeight == 0 の場合はゼロサイズ）
	Rectf contentBounds{};  ///< コンテンツ領域矩形（パディング適用済み）
};

/// @brief パネルのタイトルバーとコンテンツ領域を計算する
///
/// 外枠矩形をタイトルバーとコンテンツ領域に分割し、
/// コンテンツ領域にはパディングを適用した矩形を返す。
///
/// @param outerBounds パネル全体の矩形
/// @param titleHeight タイトルバーの高さ（0でタイトルバーなし）
/// @param padding コンテンツ領域のパディング
/// @return タイトルバーとコンテンツ領域の矩形
[[nodiscard]] constexpr PanelResult evaluatePanel(
	const Rectf& outerBounds, float titleHeight,
	const Padding& padding) noexcept
{
	// タイトルバー矩形
	const Rectf titleBounds{
		outerBounds.x(), outerBounds.y(),
		outerBounds.width(), titleHeight
	};

	// コンテンツ領域（タイトルバー下、パディング適用）
	const float contentX = outerBounds.x() + padding.left;
	const float contentY = outerBounds.y() + titleHeight + padding.top;
	const float contentW = outerBounds.width() - padding.left - padding.right;
	const float contentH = outerBounds.height() - titleHeight - padding.top - padding.bottom;

	const Rectf contentBounds{
		contentX, contentY,
		(contentW > 0.0f) ? contentW : 0.0f,
		(contentH > 0.0f) ? contentH : 0.0f
	};

	return {titleBounds, contentBounds};
}

/// @brief 枠線の内側の矩形を計算する
///
/// パネルの外枠矩形から指定幅の枠線を引いた内側の矩形を返す。
///
/// @param outerBounds パネル全体の矩形
/// @param borderWidth 枠線の幅
/// @return 枠線の内側の矩形
[[nodiscard]] constexpr Rectf panelWithBorder(
	const Rectf& outerBounds, float borderWidth) noexcept
{
	const float innerX = outerBounds.x() + borderWidth;
	const float innerY = outerBounds.y() + borderWidth;
	const float innerW = outerBounds.width() - borderWidth * 2.0f;
	const float innerH = outerBounds.height() - borderWidth * 2.0f;

	return {
		innerX, innerY,
		(innerW > 0.0f) ? innerW : 0.0f,
		(innerH > 0.0f) ? innerH : 0.0f
	};
}

} // namespace sgc::ui
