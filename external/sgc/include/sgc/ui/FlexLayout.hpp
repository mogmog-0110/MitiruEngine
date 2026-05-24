#pragma once

/// @file FlexLayout.hpp
/// @brief Flexbox風レイアウト計算ユーティリティ
///
/// CSSのFlexboxに着想を得た1次元レイアウト計算を提供する。
/// 方向、折り返し、主軸整列、交差軸整列をサポートする。
///
/// @code
/// using namespace sgc::ui;
///
/// FlexConfig config;
/// config.direction = FlexDirection::Row;
/// config.justify = FlexJustify::SpaceBetween;
/// config.align = FlexAlign::Center;
/// config.gap = 8.0f;
///
/// FlexItem items[] = {{100, 40}, {80, 40}, {120, 40}};
/// auto rects = flexLayout(parent, items, config);
/// @endcode

#include "sgc/math/Rect.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace sgc::ui
{

/// @brief Flex方向
enum class FlexDirection
{
	Row,           ///< 水平方向（左→右）
	RowReverse,    ///< 水平方向逆順（右→左）
	Column,        ///< 垂直方向（上→下）
	ColumnReverse  ///< 垂直方向逆順（下→上）
};

/// @brief 主軸の整列方法
enum class FlexJustify
{
	Start,         ///< 先頭寄せ
	End,           ///< 末尾寄せ
	Center,        ///< 中央寄せ
	SpaceBetween,  ///< 均等配分（両端にスペースなし）
	SpaceAround,   ///< 均等配分（両端に半分のスペース）
	SpaceEvenly    ///< 完全均等配分（全スペースが等しい）
};

/// @brief 交差軸の整列方法
enum class FlexAlign
{
	Start,    ///< 交差軸の先頭寄せ
	End,      ///< 交差軸の末尾寄せ
	Center,   ///< 交差軸の中央寄せ
	Stretch   ///< 交差軸方向にストレッチ
};

/// @brief Flex折り返しモード
enum class FlexWrap
{
	NoWrap,  ///< 折り返さない
	Wrap     ///< 折り返す
};

/// @brief Flexアイテムのサイズ情報
struct FlexItem
{
	float mainSize{0.0f};   ///< 主軸方向のサイズ（Row:幅, Column:高さ）
	float crossSize{0.0f};  ///< 交差軸方向のサイズ（Row:高さ, Column:幅）
	float flexGrow{0.0f};   ///< 余剰スペースの伸長比率（0で固定サイズ）
};

/// @brief Flexレイアウト設定
struct FlexConfig
{
	FlexDirection direction{FlexDirection::Row};  ///< 方向
	FlexJustify justify{FlexJustify::Start};      ///< 主軸整列
	FlexAlign align{FlexAlign::Start};            ///< 交差軸整列
	FlexWrap wrap{FlexWrap::NoWrap};              ///< 折り返し
	float gap{0.0f};                              ///< アイテム間のスペース
	float crossGap{0.0f};                         ///< 折り返し行間のスペース
};

namespace detail
{

/// @brief 主軸がRow系か判定する
[[nodiscard]] inline constexpr bool isRow(FlexDirection dir) noexcept
{
	return dir == FlexDirection::Row || dir == FlexDirection::RowReverse;
}

/// @brief 逆順か判定する
[[nodiscard]] inline constexpr bool isReverse(FlexDirection dir) noexcept
{
	return dir == FlexDirection::RowReverse || dir == FlexDirection::ColumnReverse;
}

/// @brief 1行分のアイテムを配置する
inline void layoutLine(
	std::vector<Rectf>& output,
	std::span<const std::size_t> indices,
	std::span<const FlexItem> items,
	float lineMainStart,
	float lineMainSize,
	float lineCrossStart,
	float lineCrossSize,
	const FlexConfig& config)
{
	const bool row = isRow(config.direction);
	const bool reverse = isReverse(config.direction);
	const std::size_t count = indices.size();
	if (count == 0) return;

	// アイテムの合計主軸サイズとflex-grow合計を計算
	float totalItemMain = 0.0f;
	float totalGrow = 0.0f;
	for (const auto idx : indices)
	{
		totalItemMain += items[idx].mainSize;
		totalGrow += items[idx].flexGrow;
	}
	const float totalGap = config.gap * static_cast<float>(count - 1);
	const float freeSpace = lineMainSize - totalItemMain - totalGap;

	// flex-growによる余剰分配
	std::vector<float> resolvedMain(count);
	for (std::size_t i = 0; i < count; ++i)
	{
		const auto& item = items[indices[i]];
		float extra = 0.0f;
		if (freeSpace > 0.0f && totalGrow > 0.0f && item.flexGrow > 0.0f)
		{
			extra = freeSpace * (item.flexGrow / totalGrow);
		}
		resolvedMain[i] = item.mainSize + extra;
	}

	// 主軸整列のオフセットとスペースを計算
	float resolvedTotal = 0.0f;
	for (const auto s : resolvedMain) resolvedTotal += s;
	const float remainingSpace = lineMainSize - resolvedTotal - totalGap;

	float mainOffset = 0.0f;
	float justifyGap = config.gap;

	if (totalGrow <= 0.0f || freeSpace <= 0.0f)
	{
		switch (config.justify)
		{
		case FlexJustify::Start:
			mainOffset = 0.0f;
			break;
		case FlexJustify::End:
			mainOffset = remainingSpace;
			break;
		case FlexJustify::Center:
			mainOffset = remainingSpace * 0.5f;
			break;
		case FlexJustify::SpaceBetween:
			if (count > 1)
			{
				justifyGap = (lineMainSize - resolvedTotal) / static_cast<float>(count - 1);
			}
			break;
		case FlexJustify::SpaceAround:
		{
			const float space = (lineMainSize - resolvedTotal) / static_cast<float>(count);
			justifyGap = space;
			mainOffset = space * 0.5f;
			break;
		}
		case FlexJustify::SpaceEvenly:
		{
			const float space = (lineMainSize - resolvedTotal) / static_cast<float>(count + 1);
			justifyGap = space;
			mainOffset = space;
			break;
		}
		}
	}

	// アイテム配置
	float cursor = lineMainStart + mainOffset;
	for (std::size_t i = 0; i < count; ++i)
	{
		const std::size_t actualIdx = reverse ? (count - 1 - i) : i;
		const auto& item = items[indices[actualIdx]];
		const float itemMain = resolvedMain[actualIdx];

		// 交差軸配置
		float itemCross = item.crossSize;
		float crossOffset = 0.0f;
		switch (config.align)
		{
		case FlexAlign::Start:
			break;
		case FlexAlign::End:
			crossOffset = lineCrossSize - itemCross;
			break;
		case FlexAlign::Center:
			crossOffset = (lineCrossSize - itemCross) * 0.5f;
			break;
		case FlexAlign::Stretch:
			itemCross = lineCrossSize;
			break;
		}

		Rectf rect;
		if (row)
		{
			rect = Rectf{{cursor, lineCrossStart + crossOffset}, {itemMain, itemCross}};
		}
		else
		{
			rect = Rectf{{lineCrossStart + crossOffset, cursor}, {itemCross, itemMain}};
		}
		output[indices[actualIdx]] = rect;

		cursor += itemMain + justifyGap;
	}
}

} // namespace detail

/// @brief Flexboxレイアウトを計算する
///
/// 親矩形内にアイテムをFlex方式で配置する。
/// 折り返し・整列・flex-growをサポートする。
///
/// @param parent 親矩形
/// @param items アイテムのサイズ配列
/// @param config レイアウト設定
/// @return 各アイテムの配置矩形（入力と同じ順序）
[[nodiscard]] inline std::vector<Rectf> flexLayout(
	const Rectf& parent,
	std::span<const FlexItem> items,
	const FlexConfig& config = {})
{
	const std::size_t count = items.size();
	std::vector<Rectf> result(count);
	if (count == 0) return result;

	const bool row = detail::isRow(config.direction);
	const float parentMain = row ? parent.width() : parent.height();
	const float parentCross = row ? parent.height() : parent.width();
	const float mainStart = row ? parent.x() : parent.y();
	const float crossStart = row ? parent.y() : parent.x();

	// 行分割
	struct LineInfo
	{
		std::vector<std::size_t> indices;
		float totalMain{0.0f};
		float maxCross{0.0f};
	};

	std::vector<LineInfo> lines;
	lines.emplace_back();

	for (std::size_t i = 0; i < count; ++i)
	{
		auto& line = lines.back();
		const float gapNeeded = line.indices.empty() ? 0.0f : config.gap;
		const float newTotal = line.totalMain + gapNeeded + items[i].mainSize;

		if (config.wrap == FlexWrap::Wrap && !line.indices.empty() && newTotal > parentMain)
		{
			lines.emplace_back();
			auto& newLine = lines.back();
			newLine.indices.push_back(i);
			newLine.totalMain = items[i].mainSize;
			newLine.maxCross = items[i].crossSize;
		}
		else
		{
			line.indices.push_back(i);
			line.totalMain = newTotal;
			line.maxCross = std::max(line.maxCross, items[i].crossSize);
		}
	}

	// 各行を配置
	float crossCursor = crossStart;
	for (const auto& line : lines)
	{
		const float lineCross = line.maxCross;
		detail::layoutLine(
			result,
			std::span<const std::size_t>(line.indices),
			items,
			mainStart, parentMain,
			crossCursor, lineCross,
			config);
		crossCursor += lineCross + config.crossGap;
	}

	return result;
}

} // namespace sgc::ui
