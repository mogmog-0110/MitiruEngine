#pragma once

/// @file Slider.hpp
/// @brief スライダー評価ユーティリティ
///
/// 水平スライダーの状態と値を一括で評価する。
/// ドラッグ操作による値変更をフレームワーク非依存で判定する。
///
/// @code
/// using namespace sgc::ui;
/// // 毎フレーム: wasDragging は前フレームの result.dragging を保持
/// auto result = evaluateSlider(sliderBounds, mousePos, mouseDown, mousePressed,
///                              currentValue, 0.0f, 100.0f, wasDragging);
/// if (result.changed) { currentValue = result.value; }
/// wasDragging = result.dragging;
/// @endcode

#include "sgc/ui/WidgetState.hpp"

namespace sgc::ui
{

/// @brief スライダー評価結果
struct SliderResult
{
	WidgetState state{WidgetState::Normal};  ///< スライダーの視覚状態
	float value{0.0f};                        ///< 現在値 [minValue, maxValue]
	bool changed{false};                      ///< 値が変化したか
	bool dragging{false};                     ///< ドラッグ中か
};

/// @brief 水平スライダーの状態と値を評価する
///
/// マウス位置からスライダーの値を算出し、ドラッグ状態を管理する。
/// ドラッグ開始はスライダー矩形内でのマウス押下、
/// ドラッグ継続は前フレームでドラッグ中かつマウス押下中で判定する。
///
/// @param bounds スライダー全体の矩形
/// @param mousePos マウス座標
/// @param mouseDown マウスボタンが押下中か
/// @param mousePressed マウスボタンがこのフレームで押されたか
/// @param currentValue 現在の値
/// @param minValue 最小値
/// @param maxValue 最大値
/// @param wasDragging 前フレームでドラッグ中だったか（デフォルト: false）
/// @param enabled スライダーが有効か（デフォルト: true）
/// @return スライダーの状態・値・変化フラグ・ドラッグ状態
[[nodiscard]] constexpr SliderResult evaluateSlider(
	const Rectf& bounds, const Vec2f& mousePos,
	bool mouseDown, bool mousePressed,
	float currentValue, float minValue, float maxValue,
	bool wasDragging = false, bool enabled = true) noexcept
{
	if (!enabled)
	{
		return {WidgetState::Disabled, currentValue, false, false};
	}

	const bool hovered = isMouseOver(mousePos, bounds);
	const bool startDrag = hovered && mousePressed;
	const bool isDragging = startDrag || (wasDragging && mouseDown);

	float newValue = currentValue;
	if (isDragging)
	{
		const float relX = mousePos.x - bounds.x();
		const float width = bounds.width();
		float t = (width > 0.0f) ? (relX / width) : 0.0f;
		if (t < 0.0f)
		{
			t = 0.0f;
		}
		if (t > 1.0f)
		{
			t = 1.0f;
		}
		newValue = minValue + t * (maxValue - minValue);
	}

	const bool pressed = isDragging;
	const auto state = resolveWidgetState(true, hovered || isDragging, pressed);
	const bool changed = (newValue != currentValue);

	return {state, newValue, changed, isDragging};
}

} // namespace sgc::ui
