#pragma once

/// @file TreeView.hpp
/// @brief ツリービュー評価ユーティリティ
///
/// 階層構造のツリーの状態を評価する。
/// ノードの展開/折りたたみ、選択、スクロールをフレームワーク非依存で判定する。
///
/// @code
/// using namespace sgc::ui;
/// int32_t parents[] = {-1, 0, 0, 1};
/// int32_t depths[] = {0, 1, 1, 2};
/// TreeNodeState states[] = {{true, false}, {true, false}, {false, false}, {false, false}};
/// TreeViewConfig config{bounds, 4, parents, depths, states, 24.0f, 20.0f, 0.0f};
/// auto result = evaluateTreeView(config, mousePos, mouseDown, mousePressed, scrollDelta);
/// @endcode

#include <cstdint>

#include "sgc/ui/WidgetState.hpp"

namespace sgc::ui
{

/// @brief デフォルトのインデント幅
inline constexpr float DEFAULT_INDENT_WIDTH = 20.0f;

/// @brief ツリーノードの状態
struct TreeNodeState
{
	bool isExpanded{false};  ///< 展開中か
	bool isSelected{false};  ///< 選択中か
};

/// @brief ツリービュー設定
struct TreeViewConfig
{
	Rectf bounds;                        ///< ツリービュー全体のバウンズ
	int32_t nodeCount{0};                ///< 全ノード数（フラット配列）
	const int32_t* parentIndices{nullptr}; ///< 各ノードの親インデックス (-1 = ルート)
	const int32_t* depths{nullptr};       ///< 各ノードの深さ (0 = ルート)
	const TreeNodeState* states{nullptr}; ///< 各ノードの状態
	float rowHeight{24.0f};              ///< 行の高さ
	float indentWidth{DEFAULT_INDENT_WIDTH}; ///< インデントの幅
	float scrollOffset{0.0f};            ///< スクロールオフセット
};

/// @brief ツリービュー評価結果
struct TreeViewResult
{
	WidgetState state{WidgetState::Normal};  ///< ウィジェットの視覚状態
	int32_t hoveredNode{-1};                 ///< ホバー中のノード (-1 = なし)
	int32_t clickedNode{-1};                 ///< クリックされたノード (-1 = なし)
	int32_t toggledNode{-1};                 ///< 展開/折りたたみが切り替わったノード (-1 = なし)
	float scrollOffset{0.0f};                ///< スクロールオフセット
	Rectf visibleBounds;                     ///< 表示領域のバウンズ
};

/// @brief ノードが可視かどうかを判定する（全祖先が展開中であること）
///
/// @param nodeIndex 判定するノードのインデックス
/// @param parentIndices 親インデックス配列
/// @param states ノード状態配列
/// @return 可視であればtrue
[[nodiscard]] constexpr bool isNodeVisible(
	int32_t nodeIndex, const int32_t* parentIndices,
	const TreeNodeState* states) noexcept
{
	int32_t parent = parentIndices[nodeIndex];
	while (parent >= 0)
	{
		if (!states[parent].isExpanded)
		{
			return false;
		}
		parent = parentIndices[parent];
	}
	return true;
}

/// @brief ノードが子を持つかどうかを判定する
///
/// @param nodeIndex 判定するノードのインデックス
/// @param nodeCount 全ノード数
/// @param parentIndices 親インデックス配列
/// @return 子を持てばtrue
[[nodiscard]] constexpr bool hasChildren(
	int32_t nodeIndex, int32_t nodeCount,
	const int32_t* parentIndices) noexcept
{
	for (int32_t i = 0; i < nodeCount; ++i)
	{
		if (parentIndices[i] == nodeIndex)
		{
			return true;
		}
	}
	return false;
}

/// @brief ツリービューの状態を評価する
///
/// 可視ノードの一覧を走査し、ホバー・クリック・展開切り替えを判定する。
///
/// @param config ツリービューの設定
/// @param mousePos マウス座標
/// @param mouseDown マウスボタンが押下中か
/// @param mousePressed マウスボタンがこのフレームで押されたか
/// @param scrollDelta マウスホイールのスクロール量
/// @return ツリービューの評価結果
[[nodiscard]] constexpr TreeViewResult evaluateTreeView(
	const TreeViewConfig& config, const Vec2f& mousePos,
	bool mouseDown, bool mousePressed,
	float scrollDelta) noexcept
{
	const bool hovered = isMouseOver(mousePos, config.bounds);
	int32_t hoveredNode = -1;
	int32_t clickedNode = -1;
	int32_t toggledNode = -1;
	float scroll = config.scrollOffset;

	// スクロール処理
	if (hovered)
	{
		scroll -= scrollDelta;
		if (scroll < 0.0f)
		{
			scroll = 0.0f;
		}
	}

	// 可視ノードを走査
	float currentY = config.bounds.y() - scroll;
	for (int32_t i = 0; i < config.nodeCount; ++i)
	{
		if (!isNodeVisible(i, config.parentIndices, config.states))
		{
			continue;
		}

		const float nodeX = config.bounds.x() + static_cast<float>(config.depths[i]) * config.indentWidth;
		const float nodeW = config.bounds.width() - static_cast<float>(config.depths[i]) * config.indentWidth;
		const Rectf nodeRect{{nodeX, currentY}, {nodeW, config.rowHeight}};

		// バウンズ内のノードのみ判定
		if (currentY + config.rowHeight > config.bounds.y() &&
		    currentY < config.bounds.y() + config.bounds.height())
		{
			if (isMouseOver(mousePos, nodeRect) && isMouseOver(mousePos, config.bounds))
			{
				hoveredNode = i;
				if (mousePressed)
				{
					clickedNode = i;
					// 子を持つノードの展開アイコン領域をクリック → 展開切り替え
					const Rectf toggleRect{
						{config.bounds.x() + static_cast<float>(config.depths[i]) * config.indentWidth - config.indentWidth,
						 currentY},
						{config.indentWidth, config.rowHeight}
					};
					if (hasChildren(i, config.nodeCount, config.parentIndices) &&
					    isMouseOver(mousePos, toggleRect))
					{
						toggledNode = i;
					}
				}
			}
		}

		currentY += config.rowHeight;
	}

	const bool pressed = hovered && mouseDown;
	const auto state = resolveWidgetState(true, hovered, pressed);

	return {state, hoveredNode, clickedNode, toggledNode, scroll, config.bounds};
}

/// @brief 特定ノードのバウンズを取得する
///
/// @param config ツリービューの設定
/// @param result ツリービューの評価結果
/// @param nodeIndex 取得するノードのインデックス
/// @return ノードのバウンズ
[[nodiscard]] constexpr Rectf treeNodeBounds(
	const TreeViewConfig& config, const TreeViewResult& result,
	int32_t nodeIndex) noexcept
{
	// 可視ノードを走査してノードの位置を計算
	float currentY = config.bounds.y() - result.scrollOffset;
	for (int32_t i = 0; i < config.nodeCount; ++i)
	{
		if (!isNodeVisible(i, config.parentIndices, config.states))
		{
			continue;
		}
		if (i == nodeIndex)
		{
			const float nodeX = config.bounds.x() + static_cast<float>(config.depths[i]) * config.indentWidth;
			const float nodeW = config.bounds.width() - static_cast<float>(config.depths[i]) * config.indentWidth;
			return Rectf{{nodeX, currentY}, {nodeW, config.rowHeight}};
		}
		currentY += config.rowHeight;
	}
	return Rectf{};
}

} // namespace sgc::ui
