#pragma once

/// @file ListView.hpp
/// @brief リストビュー評価ユーティリティ
///
/// スクロール可能なリストの選択・ホバー・スクロール状態を評価する。
/// Scrollbar.hppと連携してスクロールバーも一体で処理する。
///
/// @code
/// using namespace sgc::ui;
/// auto result = evaluateListView(bounds, itemCount, 32.0f, selected, scrollPos,
///                                 mousePos, mouseDown, mousePressed, wasDragging);
/// if (result.selectionChanged) { selected = result.selectedIndex; }
/// scrollPos = result.scrollOffset;
/// wasDragging = result.scrollbar.dragging;
/// @endcode

#include "sgc/ui/Scrollbar.hpp"

namespace sgc::ui
{

/// @brief リストビュー評価結果
struct ListViewResult
{
	int32_t selectedIndex{-1};    ///< 選択中のアイテムインデックス（-1=なし）
	int32_t hoveredIndex{-1};     ///< ホバー中のアイテムインデックス（-1=なし）
	bool selectionChanged{false}; ///< 選択が変わったか
	ScrollbarResult scrollbar{};  ///< スクロールバーの結果
	Rectf visibleArea{};          ///< リスト可視領域（スクロールバー除く）
	float scrollOffset{0.0f};     ///< 現在のスクロールオフセット
};

/// @brief リストビューの状態を評価する
///
/// アイテムの選択・ホバー判定とスクロールバーの評価を一括で行う。
/// 可視領域に表示されるアイテムのみ判定対象となる。
///
/// @param bounds リストビュー全体の矩形
/// @param itemCount アイテム総数
/// @param itemHeight 1アイテムの高さ
/// @param currentSelection 現在の選択インデックス（-1=なし）
/// @param scrollPos 現在のスクロール位置
/// @param mousePos マウス座標
/// @param mouseDown マウスボタン押下中か
/// @param mousePressed このフレームでマウスボタンが押されたか
/// @param wasDragging 前フレームでスクロールバーをドラッグ中だったか
/// @param scrollbarWidth スクロールバーの幅（デフォルト: 12px）
/// @return 選択・ホバー・スクロール状態
[[nodiscard]] inline ListViewResult evaluateListView(
	const Rectf& bounds, int32_t itemCount, float itemHeight,
	int32_t currentSelection, float scrollPos,
	const Vec2f& mousePos, bool mouseDown, bool mousePressed,
	bool wasDragging = false, float scrollbarWidth = 12.0f) noexcept
{
	ListViewResult result;
	result.selectedIndex = currentSelection;

	if (itemCount <= 0)
	{
		result.visibleArea = bounds;
		result.scrollOffset = 0.0f;
		return result;
	}

	const float contentH = static_cast<float>(itemCount) * itemHeight;
	const float viewportH = bounds.height();
	const bool needsScroll = contentH > viewportH;

	// リスト可視領域（スクロールバー分を引く）
	const float listW = needsScroll ? (bounds.width() - scrollbarWidth) : bounds.width();
	result.visibleArea = Rectf{bounds.x(), bounds.y(), listW, viewportH};

	// スクロールバー評価
	if (needsScroll)
	{
		const Rectf trackBounds{
			bounds.x() + listW, bounds.y(),
			scrollbarWidth, viewportH
		};
		result.scrollbar = evaluateScrollbar(
			trackBounds, mousePos, mouseDown, mousePressed,
			scrollPos, contentH, viewportH, wasDragging
		);
		result.scrollOffset = result.scrollbar.scrollPos;
	}
	else
	{
		result.scrollOffset = 0.0f;
	}

	// アイテムのホバー・クリック判定
	const bool mouseInList = isMouseOver(mousePos, result.visibleArea);

	if (mouseInList && !result.scrollbar.dragging)
	{
		const float relativeY = mousePos.y - bounds.y() + result.scrollOffset;
		const int32_t hoverIdx = static_cast<int32_t>(relativeY / itemHeight);

		if (hoverIdx >= 0 && hoverIdx < itemCount)
		{
			result.hoveredIndex = hoverIdx;

			if (mousePressed)
			{
				result.selectedIndex = hoverIdx;
				result.selectionChanged = (hoverIdx != currentSelection);
			}
		}
	}

	return result;
}

/// @brief リストビュー内のアイテム矩形を取得する
///
/// @param listArea リスト可視領域
/// @param itemIndex アイテムインデックス
/// @param itemHeight アイテムの高さ
/// @param scrollOffset スクロールオフセット
/// @return アイテムの画面上の矩形
[[nodiscard]] constexpr Rectf listItemBounds(
	const Rectf& listArea, int32_t itemIndex,
	float itemHeight, float scrollOffset) noexcept
{
	const float y = listArea.y() + static_cast<float>(itemIndex) * itemHeight - scrollOffset;
	return Rectf{listArea.x(), y, listArea.width(), itemHeight};
}

} // namespace sgc::ui
