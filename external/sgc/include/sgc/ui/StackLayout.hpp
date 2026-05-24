#pragma once

/// @file StackLayout.hpp
/// @brief スタックレイアウト計算ユーティリティ
///
/// ウィジェットを垂直・水平にスタック配置する関数群を提供する。
/// 親矩形内に等間隔または可変高さ/幅でアイテムを並べる。
///
/// @code
/// using namespace sgc::ui;
/// auto parent = sgc::Rectf{{10, 10}, {200, 400}};
/// float heights[] = {30.0f, 50.0f, 30.0f};
/// auto rects = vstack(parent, heights, 8.0f);
/// @endcode

#include "sgc/math/Rect.hpp"

#include <span>
#include <vector>

namespace sgc::ui
{

/// @brief 垂直スタックレイアウトを計算する
///
/// 親矩形の上端からアイテムを順に積み下げる。
/// 各アイテムは親の幅全体を使い、指定された高さで配置される。
///
/// @param parent 親矩形
/// @param itemHeights 各アイテムの高さ配列
/// @param spacing アイテム間のスペース
/// @return 各アイテムの矩形リスト
[[nodiscard]] inline std::vector<Rectf> vstack(
	const Rectf& parent,
	std::span<const float> itemHeights,
	float spacing = 0.0f)
{
	std::vector<Rectf> result;
	result.reserve(itemHeights.size());

	float currentY = parent.y();
	for (const float h : itemHeights)
	{
		result.push_back(Rectf{{parent.x(), currentY}, {parent.width(), h}});
		currentY += h + spacing;
	}

	return result;
}

/// @brief 水平スタックレイアウトを計算する
///
/// 親矩形の左端からアイテムを順に右へ並べる。
/// 各アイテムは親の高さ全体を使い、指定された幅で配置される。
///
/// @param parent 親矩形
/// @param itemWidths 各アイテムの幅配列
/// @param spacing アイテム間のスペース
/// @return 各アイテムの矩形リスト
[[nodiscard]] inline std::vector<Rectf> hstack(
	const Rectf& parent,
	std::span<const float> itemWidths,
	float spacing = 0.0f)
{
	std::vector<Rectf> result;
	result.reserve(itemWidths.size());

	float currentX = parent.x();
	for (const float w : itemWidths)
	{
		result.push_back(Rectf{{currentX, parent.y()}, {w, parent.height()}});
		currentX += w + spacing;
	}

	return result;
}

/// @brief 均等高さの垂直スタックレイアウトを計算する
///
/// 親矩形の高さからスペースを差し引き、残りを均等にアイテムへ分配する。
///
/// @param parent 親矩形
/// @param itemCount アイテム数
/// @param spacing アイテム間のスペース
/// @return 各アイテムの矩形リスト
[[nodiscard]] inline std::vector<Rectf> vstackFixed(
	const Rectf& parent,
	std::size_t itemCount,
	float spacing = 0.0f)
{
	if (itemCount == 0)
	{
		return {};
	}

	const float totalSpacing = spacing * static_cast<float>(itemCount - 1);
	const float itemHeight = (parent.height() - totalSpacing) / static_cast<float>(itemCount);

	std::vector<Rectf> result;
	result.reserve(itemCount);

	float currentY = parent.y();
	for (std::size_t i = 0; i < itemCount; ++i)
	{
		result.push_back(Rectf{{parent.x(), currentY}, {parent.width(), itemHeight}});
		currentY += itemHeight + spacing;
	}

	return result;
}

/// @brief 均等幅の水平スタックレイアウトを計算する
///
/// 親矩形の幅からスペースを差し引き、残りを均等にアイテムへ分配する。
///
/// @param parent 親矩形
/// @param itemCount アイテム数
/// @param spacing アイテム間のスペース
/// @return 各アイテムの矩形リスト
[[nodiscard]] inline std::vector<Rectf> hstackFixed(
	const Rectf& parent,
	std::size_t itemCount,
	float spacing = 0.0f)
{
	if (itemCount == 0)
	{
		return {};
	}

	const float totalSpacing = spacing * static_cast<float>(itemCount - 1);
	const float itemWidth = (parent.width() - totalSpacing) / static_cast<float>(itemCount);

	std::vector<Rectf> result;
	result.reserve(itemCount);

	float currentX = parent.x();
	for (std::size_t i = 0; i < itemCount; ++i)
	{
		result.push_back(Rectf{{currentX, parent.y()}, {itemWidth, parent.height()}});
		currentX += itemWidth + spacing;
	}

	return result;
}

} // namespace sgc::ui
