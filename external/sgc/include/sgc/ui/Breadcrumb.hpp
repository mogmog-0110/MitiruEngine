#pragma once

/// @file Breadcrumb.hpp
/// @brief パンくずリスト評価ユーティリティ
///
/// ナビゲーション用パンくずリストの状態とクリック判定を一括で評価する。
/// 各項目は均等幅で配置され、クリックされた項目のインデックスを返す。
///
/// @code
/// using namespace sgc::ui;
/// std::vector<std::string> items = {"Home", "Category", "Item"};
/// auto result = evaluateBreadcrumb(bounds, static_cast<int>(items.size()),
///                                   mousePos, mouseDown, mousePressed);
/// if (result.clickedIndex >= 0) { /* 項目が選択された */ }
/// @endcode

#include "sgc/ui/WidgetState.hpp"

namespace sgc::ui
{

/// @brief パンくずリスト評価結果
struct BreadcrumbResult
{
	int clickedIndex{-1};                         ///< クリックされた項目のインデックス (-1 = なし)
	int hoveredIndex{-1};                         ///< ホバー中の項目のインデックス (-1 = なし)
	WidgetState state{WidgetState::Normal};       ///< ウィジェット全体の視覚状態
};

/// @brief パンくずリストの各項目の矩形を計算する
///
/// 全体矩形を項目数で均等分割する。
///
/// @param bounds パンくずリスト全体の矩形
/// @param itemCount 項目数
/// @param index 対象項目のインデックス
/// @return 項目の矩形
[[nodiscard]] constexpr Rectf breadcrumbItemRect(
	const Rectf& bounds, int itemCount, int index) noexcept
{
	if (itemCount <= 0)
	{
		return bounds;
	}
	const float itemWidth = bounds.width() / static_cast<float>(itemCount);
	return {
		bounds.x() + itemWidth * static_cast<float>(index),
		bounds.y(),
		itemWidth,
		bounds.height()
	};
}

/// @brief パンくずリストの状態とクリックを評価する
///
/// 各項目のヒットテストを行い、クリックされた項目のインデックスを返す。
/// 項目は全体矩形を均等に分割して配置される。
///
/// @param bounds パンくずリスト全体の矩形
/// @param itemCount 項目数
/// @param mousePos マウス座標
/// @param mouseDown マウスボタンが押下中か
/// @param mousePressed マウスボタンがこのフレームで押されたか
/// @param enabled パンくずリストが有効か（デフォルト: true）
/// @return パンくずリストの状態・クリック項目インデックス
[[nodiscard]] constexpr BreadcrumbResult evaluateBreadcrumb(
	const Rectf& bounds, int itemCount,
	const Vec2f& mousePos, bool mouseDown, bool mousePressed,
	bool enabled = true) noexcept
{
	if (!enabled)
	{
		return {-1, -1, WidgetState::Disabled};
	}

	if (itemCount <= 0)
	{
		return {-1, -1, WidgetState::Normal};
	}

	int hoveredIdx = -1;
	int clickedIdx = -1;

	for (int i = 0; i < itemCount; ++i)
	{
		const auto itemRect = breadcrumbItemRect(bounds, itemCount, i);
		if (isMouseOver(mousePos, itemRect))
		{
			hoveredIdx = i;
			if (mousePressed)
			{
				clickedIdx = i;
			}
			break;
		}
	}

	const bool anyHovered = hoveredIdx >= 0;
	const bool pressed = anyHovered && mouseDown;
	const auto state = resolveWidgetState(true, anyHovered, pressed);

	return {clickedIdx, hoveredIdx, state};
}

} // namespace sgc::ui
