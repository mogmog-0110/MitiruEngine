#pragma once

/// @file Scrollbar.hpp
/// @brief スクロールバー評価ユーティリティ
///
/// 垂直スクロールバーの状態・スクロール位置・サム矩形を一括で評価する。
/// ドラッグ操作とトラッククリック（ページアップ/ダウン）に対応。
///
/// @code
/// using namespace sgc::ui;
/// // 毎フレーム: wasDragging は前フレームの result.dragging を保持
/// auto result = evaluateScrollbar(trackBounds, mousePos, mouseDown, mousePressed,
///                                  scrollPos, contentH, viewportH, wasDragging);
/// if (result.changed) { scrollPos = result.scrollPos; }
/// wasDragging = result.dragging;
/// // result.thumbRect でサムを描画
/// @endcode

#include "sgc/ui/WidgetState.hpp"

namespace sgc::ui
{

/// @brief スクロールが必要かどうかを判定する
/// @param contentSize コンテンツ全体の高さ
/// @param viewportSize ビューポートの高さ
/// @return コンテンツがビューポートに収まらなければtrue
[[nodiscard]] constexpr bool isScrollNeeded(
	float contentSize, float viewportSize) noexcept
{
	return contentSize > viewportSize;
}

/// @brief 最大スクロール位置を算出する
/// @param contentSize コンテンツ全体の高さ
/// @param viewportSize ビューポートの高さ
/// @return 最大スクロール量（0以上）
[[nodiscard]] constexpr float maxScrollPos(
	float contentSize, float viewportSize) noexcept
{
	const float diff = contentSize - viewportSize;
	return (diff > 0.0f) ? diff : 0.0f;
}

/// @brief スクロールバー評価結果
struct ScrollbarResult
{
	WidgetState state{WidgetState::Normal};  ///< サムの視覚状態
	float scrollPos{0.0f};                    ///< 更新後のスクロール位置
	bool changed{false};                      ///< スクロール位置が変化したか
	bool dragging{false};                     ///< ドラッグ中か
	Rectf thumbRect{};                        ///< サム（つまみ）の矩形
};

/// @brief 垂直スクロールバーの状態を評価する
///
/// サムのサイズはビューポートとコンテンツの比率から自動算出する。
/// サムのドラッグとトラック上のクリック（ページ単位スクロール）に対応。
///
/// @param trackBounds スクロールバートラック全体の矩形
/// @param mousePos マウス座標
/// @param mouseDown マウスボタンが押下中か
/// @param mousePressed マウスボタンがこのフレームで押されたか
/// @param scrollPos 現在のスクロール位置（0 ～ maxScroll）
/// @param contentSize コンテンツ全体の高さ
/// @param viewportSize ビューポートの高さ
/// @param wasDragging 前フレームでドラッグ中だったか
/// @param enabled スクロールバーが有効か（デフォルト: true）
/// @param minThumbSize サム最小サイズ（デフォルト: 20ピクセル）
/// @return スクロールバーの状態・位置・変化フラグ・ドラッグ状態・サム矩形
[[nodiscard]] constexpr ScrollbarResult evaluateScrollbar(
	const Rectf& trackBounds, const Vec2f& mousePos,
	bool mouseDown, bool mousePressed,
	float scrollPos, float contentSize, float viewportSize,
	bool wasDragging = false, bool enabled = true,
	float minThumbSize = 20.0f) noexcept
{
	const float maxScroll = maxScrollPos(contentSize, viewportSize);

	// スクロール不要の場合
	if (maxScroll <= 0.0f || !enabled)
	{
		const Rectf thumb{trackBounds.x(), trackBounds.y(),
		                  trackBounds.width(), trackBounds.height()};
		const auto state = enabled ? WidgetState::Normal : WidgetState::Disabled;
		return {state, 0.0f, false, false, thumb};
	}

	// サムの高さを算出
	const float trackH = trackBounds.height();
	float thumbH = trackH * viewportSize / contentSize;
	if (thumbH < minThumbSize)
	{
		thumbH = minThumbSize;
	}
	if (thumbH > trackH)
	{
		thumbH = trackH;
	}

	// 現在のスクロール位置をクランプ
	float currentPos = scrollPos;
	if (currentPos < 0.0f)
	{
		currentPos = 0.0f;
	}
	if (currentPos > maxScroll)
	{
		currentPos = maxScroll;
	}

	// サムのY位置を算出
	const float scrollableTrack = trackH - thumbH;
	const float ratio = (maxScroll > 0.0f) ? (currentPos / maxScroll) : 0.0f;
	const float thumbY = trackBounds.y() + scrollableTrack * ratio;

	const Rectf thumbRect{trackBounds.x(), thumbY, trackBounds.width(), thumbH};

	// ヒットテスト
	const bool trackHovered = isMouseOver(mousePos, trackBounds);
	const bool thumbHovered = isMouseOver(mousePos, thumbRect);

	// ドラッグ判定
	const bool startDrag = thumbHovered && mousePressed;
	const bool isDragging = startDrag || (wasDragging && mouseDown);

	float newPos = currentPos;

	if (isDragging)
	{
		// ドラッグ: マウスのY座標からスクロール位置を算出
		const float relY = mousePos.y - trackBounds.y() - thumbH * 0.5f;
		float t = (scrollableTrack > 0.0f) ? (relY / scrollableTrack) : 0.0f;
		if (t < 0.0f)
		{
			t = 0.0f;
		}
		if (t > 1.0f)
		{
			t = 1.0f;
		}
		newPos = maxScroll * t;
	}
	else if (trackHovered && mousePressed && !thumbHovered)
	{
		// トラッククリック: ページアップ/ダウン
		if (mousePos.y < thumbY)
		{
			// サムより上: ページアップ
			newPos = currentPos - viewportSize;
		}
		else
		{
			// サムより下: ページダウン
			newPos = currentPos + viewportSize;
		}
		if (newPos < 0.0f)
		{
			newPos = 0.0f;
		}
		if (newPos > maxScroll)
		{
			newPos = maxScroll;
		}
	}

	// 更新後のサム矩形を再計算
	const float newRatio = (maxScroll > 0.0f) ? (newPos / maxScroll) : 0.0f;
	const float newThumbY = trackBounds.y() + scrollableTrack * newRatio;
	const Rectf newThumbRect{trackBounds.x(), newThumbY, trackBounds.width(), thumbH};

	const bool pressed = isDragging;
	const auto state = resolveWidgetState(true, thumbHovered || isDragging, pressed);
	const bool posChanged = (newPos != currentPos);

	return {state, newPos, posChanged, isDragging, newThumbRect};
}

} // namespace sgc::ui
