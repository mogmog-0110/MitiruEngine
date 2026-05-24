#pragma once

/// @file Tooltip.hpp
/// @brief ツールチップ配置ユーティリティ
///
/// ウィジェットに対するツールチップの位置を画面境界を考慮して計算する。
/// 優先側がはみ出す場合は反対側にフリップする。
///
/// @code
/// using namespace sgc::ui;
/// auto widget = sgc::Rectf{{100, 50}, {120, 30}};
/// auto screen = sgc::Rectf{{0, 0}, {800, 600}};
/// auto tip = evaluateTooltip(widget, screen, {150, 40}, TooltipSide::Above);
/// // tip.bounds にツールチップ矩形、tip.actualSide に実際の配置側
/// @endcode

#include "sgc/math/Rect.hpp"

namespace sgc::ui
{

/// @brief ツールチップの配置側
enum class TooltipSide
{
	Above,  ///< ウィジェットの上
	Below,  ///< ウィジェットの下
	Left,   ///< ウィジェットの左
	Right   ///< ウィジェットの右
};

/// @brief ツールチップ配置の計算結果
struct TooltipResult
{
	Rectf bounds;             ///< ツールチップの矩形
	TooltipSide actualSide;   ///< 実際に配置された側
};

/// @brief ウィジェットとツールチップ間のデフォルトギャップ（ピクセル）
inline constexpr float TOOLTIP_GAP = 4.0f;

/// @brief ツールチップの配置位置を計算する
///
/// ウィジェットの指定側にツールチップを配置し、画面外にはみ出す場合は
/// 反対側にフリップする。水平方向はウィジェット中心揃え、
/// 垂直方向もウィジェット中心揃えで配置する。
/// 配置後、画面境界内に収まるようクランプを行う。
///
/// @param widgetBounds ツールチップ対象のウィジェット矩形
/// @param screenBounds 画面（ビューポート）矩形
/// @param tooltipSize ツールチップのサイズ (幅, 高さ)
/// @param preferredSide 優先的に配置したい側
/// @param gap ウィジェットとツールチップの間隔（デフォルト: TOOLTIP_GAP）
/// @return ツールチップの配置結果
[[nodiscard]] constexpr TooltipResult evaluateTooltip(
	const Rectf& widgetBounds,
	const Rectf& screenBounds,
	const Vec2f& tooltipSize,
	TooltipSide preferredSide,
	float gap = TOOLTIP_GAP) noexcept
{
	// 反対側を返すヘルパー
	const auto flipSide = [](TooltipSide side) constexpr -> TooltipSide
	{
		switch (side)
		{
		case TooltipSide::Above: return TooltipSide::Below;
		case TooltipSide::Below: return TooltipSide::Above;
		case TooltipSide::Left:  return TooltipSide::Right;
		case TooltipSide::Right: return TooltipSide::Left;
		}
		return TooltipSide::Below;
	};

	// 指定側に配置したときの左上座標を計算するヘルパー
	const auto computePosition = [&](TooltipSide side) constexpr -> Vec2f
	{
		switch (side)
		{
		case TooltipSide::Above:
			return {
				widgetBounds.x() + widgetBounds.width() * 0.5f - tooltipSize.x * 0.5f,
				widgetBounds.y() - gap - tooltipSize.y
			};
		case TooltipSide::Below:
			return {
				widgetBounds.x() + widgetBounds.width() * 0.5f - tooltipSize.x * 0.5f,
				widgetBounds.bottom() + gap
			};
		case TooltipSide::Left:
			return {
				widgetBounds.x() - gap - tooltipSize.x,
				widgetBounds.y() + widgetBounds.height() * 0.5f - tooltipSize.y * 0.5f
			};
		case TooltipSide::Right:
			return {
				widgetBounds.right() + gap,
				widgetBounds.y() + widgetBounds.height() * 0.5f - tooltipSize.y * 0.5f
			};
		}
		return {};
	};

	// 画面内に収まるかチェック
	const auto fitsInScreen = [&](const Vec2f& pos) constexpr -> bool
	{
		return pos.x >= screenBounds.x()
			&& pos.y >= screenBounds.y()
			&& pos.x + tooltipSize.x <= screenBounds.right()
			&& pos.y + tooltipSize.y <= screenBounds.bottom();
	};

	// 画面境界内にクランプ
	const auto clampToScreen = [&](Vec2f pos) constexpr -> Vec2f
	{
		// 左端・上端クランプ
		if (pos.x < screenBounds.x())
		{
			pos.x = screenBounds.x();
		}
		if (pos.y < screenBounds.y())
		{
			pos.y = screenBounds.y();
		}
		// 右端・下端クランプ
		if (pos.x + tooltipSize.x > screenBounds.right())
		{
			pos.x = screenBounds.right() - tooltipSize.x;
		}
		if (pos.y + tooltipSize.y > screenBounds.bottom())
		{
			pos.y = screenBounds.bottom() - tooltipSize.y;
		}
		return pos;
	};

	// 優先側で試行
	Vec2f pos = computePosition(preferredSide);
	TooltipSide actualSide = preferredSide;

	// はみ出す場合は反対側にフリップ
	if (!fitsInScreen(pos))
	{
		const TooltipSide flipped = flipSide(preferredSide);
		const Vec2f flippedPos = computePosition(flipped);
		if (fitsInScreen(flippedPos))
		{
			pos = flippedPos;
			actualSide = flipped;
		}
		// 反対側もはみ出す場合は優先側のまま、クランプで対応
	}

	pos = clampToScreen(pos);
	return TooltipResult{Rectf{pos, tooltipSize}, actualSide};
}

} // namespace sgc::ui
