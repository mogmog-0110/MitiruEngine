#pragma once

/// @file UIHitTest.hpp
/// @brief UIノードのヒットテスト
/// @details スクリーン座標からUIノードを検出するヒットテスト機能を提供する。
///          可視性・hitTestEnabledフラグ・z-indexを考慮する。

#include <algorithm>
#include <vector>

#include <mitiru/ui/UINode.hpp>

namespace mitiru::ui
{

/// @brief 指定座標がノードのバウンズ内にあるか判定する
/// @param node 判定対象のノード
/// @param x スクリーンX座標
/// @param y スクリーンY座標
/// @return バウンズ内であればtrue
[[nodiscard]] inline bool isPointInBounds(const UINode& node, float x, float y) noexcept
{
	const auto& b = node.bounds();
	return x >= b.x() && x < b.x() + b.width()
		&& y >= b.y() && y < b.y() + b.height();
}

namespace detail
{

/// @brief ヒットテストの再帰実装（全ノード収集）
inline void hitTestAllImpl(UINode& node, float x, float y, std::vector<UINode*>& results)
{
	// 不可視ノードはスキップ
	if (!node.visible())
	{
		return;
	}

	// hitTestが無効なノードはスキップ（子ノードも含めてパススルー）
	if (!node.hitTestEnabled())
	{
		return;
	}

	// 子ノードを逆順（後ろから＝手前から）に走査
	const auto& children = node.children();
	for (auto it = children.rbegin(); it != children.rend(); ++it)
	{
		hitTestAllImpl(**it, x, y, results);
	}

	// 自身がポイントを含むなら結果に追加
	if (isPointInBounds(node, x, y))
	{
		results.push_back(&node);
	}
}

} // namespace detail

/// @brief 指定座標の最も手前のノードを返す
/// @details ツリーを深さ優先で走査し、z-indexとツリー順序を考慮して
///          最も手前（最も深い・最も後ろの子）のノードを返す。
/// @param root UIツリーのルートノード
/// @param x スクリーンX座標
/// @param y スクリーンY座標
/// @return ヒットしたノードへのポインタ（なければnullptr）
[[nodiscard]] inline UINode* hitTest(UINode& root, float x, float y)
{
	std::vector<UINode*> all;
	detail::hitTestAllImpl(root, x, y, all);

	if (all.empty())
	{
		return nullptr;
	}

	// z-indexが最も高いノードを返す（同z-indexならツリー走査順で手前のもの＝先頭）
	auto* best = all.front();
	for (auto* node : all)
	{
		if (node->zIndex() > best->zIndex())
		{
			best = node;
		}
	}
	return best;
}

/// @brief 指定座標に含まれるすべてのノードを返す（手前から奥の順）
/// @details z-indexで降順ソートし、同z-indexではツリー走査順を維持する。
/// @param root UIツリーのルートノード
/// @param x スクリーンX座標
/// @param y スクリーンY座標
/// @return ヒットしたノードのリスト（手前から奥の順）
[[nodiscard]] inline std::vector<UINode*> hitTestAll(UINode& root, float x, float y)
{
	std::vector<UINode*> results;
	detail::hitTestAllImpl(root, x, y, results);

	// z-indexで降順安定ソート（同z-indexではツリー走査順維持）
	std::stable_sort(results.begin(), results.end(),
		[](const UINode* a, const UINode* b)
		{
			return a->zIndex() > b->zIndex();
		});

	return results;
}

} // namespace mitiru::ui
