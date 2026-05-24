#pragma once

/// @file RangeSlider.hpp
/// @brief 範囲スライダー評価ユーティリティ
///
/// 2つのハンドルを持つ範囲スライダーの状態と値を一括で評価する。
/// ドラッグ操作による下限・上限値の変更をフレームワーク非依存で判定する。
///
/// @code
/// using namespace sgc::ui;
/// auto result = evaluateRangeSlider(sliderBounds, mousePos, mouseDown, mousePressed,
///                                    low, high, 0.0f, 1.0f, dragTarget);
/// if (result.changed) { low = result.low; high = result.high; }
/// dragTarget = result.dragTarget;
/// @endcode

#include "sgc/ui/WidgetState.hpp"

namespace sgc::ui
{

/// @brief 範囲スライダーのドラッグ対象
enum class RangeSliderDragTarget
{
	None,  ///< ドラッグなし
	Low,   ///< 下限ハンドル
	High   ///< 上限ハンドル
};

/// @brief 範囲スライダー評価結果
struct RangeSliderResult
{
	float low{0.0f};                              ///< 下限値
	float high{1.0f};                             ///< 上限値
	bool changed{false};                          ///< 値が変化したか
	bool lowHandleFocused{false};                 ///< 下限ハンドルにフォーカス
	bool highHandleFocused{false};                ///< 上限ハンドルにフォーカス
	RangeSliderDragTarget dragTarget{RangeSliderDragTarget::None};  ///< 現在のドラッグ対象
	WidgetState state{WidgetState::Normal};       ///< ウィジェットの視覚状態
};

/// @brief 値からスライダー上のX座標を計算する
/// @param bounds スライダー矩形
/// @param value 値
/// @param minValue 最小値
/// @param maxValue 最大値
/// @return X座標
[[nodiscard]] constexpr float rangeSliderValueToX(
	const Rectf& bounds, float value, float minValue, float maxValue) noexcept
{
	const float range = maxValue - minValue;
	if (range <= 0.0f)
	{
		return bounds.x();
	}
	const float t = (value - minValue) / range;
	return bounds.x() + t * bounds.width();
}

/// @brief X座標からスライダーの値を計算する
/// @param bounds スライダー矩形
/// @param xPos X座標
/// @param minValue 最小値
/// @param maxValue 最大値
/// @return クランプ済みの値
[[nodiscard]] constexpr float rangeSliderXToValue(
	const Rectf& bounds, float xPos, float minValue, float maxValue) noexcept
{
	const float width = bounds.width();
	if (width <= 0.0f)
	{
		return minValue;
	}
	float t = (xPos - bounds.x()) / width;
	if (t < 0.0f)
	{
		t = 0.0f;
	}
	if (t > 1.0f)
	{
		t = 1.0f;
	}
	return minValue + t * (maxValue - minValue);
}

/// @brief 範囲スライダーの状態と値を評価する
///
/// 2つのハンドル（下限・上限）のドラッグ判定を行い、値を更新する。
/// ハンドルの判定領域は値の位置を中心に handleRadius の範囲。
/// 下限ハンドルが上限を超えないよう制約する。
///
/// @param bounds スライダー全体の矩形
/// @param mousePos マウス座標
/// @param mouseDown マウスボタンが押下中か
/// @param mousePressed マウスボタンがこのフレームで押されたか
/// @param currentLow 現在の下限値
/// @param currentHigh 現在の上限値
/// @param minValue スライダーの最小値
/// @param maxValue スライダーの最大値
/// @param prevDragTarget 前フレームのドラッグ対象
/// @param handleRadius ハンドルの判定半径（デフォルト: 10.0f）
/// @param enabled スライダーが有効か（デフォルト: true）
/// @return 範囲スライダーの状態・値・変化フラグ・ドラッグ対象
[[nodiscard]] constexpr RangeSliderResult evaluateRangeSlider(
	const Rectf& bounds, const Vec2f& mousePos,
	bool mouseDown, bool mousePressed,
	float currentLow, float currentHigh,
	float minValue, float maxValue,
	RangeSliderDragTarget prevDragTarget = RangeSliderDragTarget::None,
	float handleRadius = 10.0f,
	bool enabled = true) noexcept
{
	if (!enabled)
	{
		return {currentLow, currentHigh, false, false, false,
		        RangeSliderDragTarget::None, WidgetState::Disabled};
	}

	// ハンドル位置を計算
	const float lowX = rangeSliderValueToX(bounds, currentLow, minValue, maxValue);
	const float highX = rangeSliderValueToX(bounds, currentHigh, minValue, maxValue);
	const float centerY = bounds.y() + bounds.height() * 0.5f;

	// ハンドルのホバー判定（Y方向はスライダー高さ内）
	const bool inYRange = (mousePos.y >= bounds.y()) && (mousePos.y <= bounds.y() + bounds.height());
	const float lowDist = (mousePos.x - lowX) * (mousePos.x - lowX);
	const float highDist = (mousePos.x - highX) * (mousePos.x - highX);
	const float r2 = handleRadius * handleRadius;
	const bool lowHovered = inYRange && (lowDist <= r2);
	const bool highHovered = inYRange && (highDist <= r2);

	// ドラッグ開始判定
	auto dragTarget = RangeSliderDragTarget::None;
	if (prevDragTarget != RangeSliderDragTarget::None && mouseDown)
	{
		// 前フレームからドラッグ継続
		dragTarget = prevDragTarget;
	}
	else if (mousePressed)
	{
		// 新規ドラッグ開始: より近いハンドルを選択
		if (lowHovered && highHovered)
		{
			dragTarget = (lowDist <= highDist)
				? RangeSliderDragTarget::Low
				: RangeSliderDragTarget::High;
		}
		else if (lowHovered)
		{
			dragTarget = RangeSliderDragTarget::Low;
		}
		else if (highHovered)
		{
			dragTarget = RangeSliderDragTarget::High;
		}
	}

	float newLow = currentLow;
	float newHigh = currentHigh;

	if (dragTarget == RangeSliderDragTarget::Low)
	{
		newLow = rangeSliderXToValue(bounds, mousePos.x, minValue, maxValue);
		// 下限は上限を超えない
		if (newLow > currentHigh)
		{
			newLow = currentHigh;
		}
	}
	else if (dragTarget == RangeSliderDragTarget::High)
	{
		newHigh = rangeSliderXToValue(bounds, mousePos.x, minValue, maxValue);
		// 上限は下限を下回らない
		if (newHigh < currentLow)
		{
			newHigh = currentLow;
		}
	}

	const bool anyHovered = isMouseOver(mousePos, bounds);
	const bool pressed = dragTarget != RangeSliderDragTarget::None;
	const auto state = resolveWidgetState(true, anyHovered || pressed, pressed);
	const bool changed = (newLow != currentLow) || (newHigh != currentHigh);

	return {newLow, newHigh, changed, lowHovered, highHovered, dragTarget, state};
}

} // namespace sgc::ui
