#pragma once

/// @file ScreenWidgets.hpp
/// @brief 2D UIウィジェット描画ヘルパー
/// @details ゲームHUD向けの便利な2D描画関数群。
///          プログレスバー、ボタン風矩形、ラベル+値表示、
///          星評価、ツールチップ、通知バナーなどを提供する。

#include <mitiru/core/Screen.hpp>

#include <algorithm>
#include <cstdio>
#include <string>

namespace mitiru::ui
{

/// @brief プログレスバーを描画する
/// @param screen 描画先Screen
/// @param x X座標
/// @param y Y座標
/// @param w 幅
/// @param h 高さ
/// @param progress 進捗（0.0-1.0）
/// @param fillColor 充填色
/// @param bgColor 背景色
inline void drawProgressBar(Screen& screen, float x, float y, float w, float h,
	float progress, const sgc::Colorf& fillColor = {0.3f, 0.8f, 0.4f, 1},
	const sgc::Colorf& bgColor = {0.2f, 0.2f, 0.2f, 0.8f})
{
	progress = std::clamp(progress, 0.0f, 1.0f);
	screen.drawRect(sgc::Rectf{x, y, w, h}, bgColor);
	screen.drawRect(sgc::Rectf{x + 1, y + 1, (w - 2) * progress, h - 2}, fillColor);
}

/// @brief ボタン風矩形を描画する
/// @param screen 描画先Screen
/// @param x X座標
/// @param y Y座標
/// @param w 幅
/// @param h 高さ
/// @param text 表示テキスト
/// @param bg 背景色
/// @param textColor テキスト色
inline void drawButton(Screen& screen, float x, float y, float w, float h,
	const std::string& text, const sgc::Colorf& bg = {0.3f, 0.3f, 0.4f, 0.9f},
	const sgc::Colorf& textColor = {1, 1, 1, 1})
{
	screen.drawRect(sgc::Rectf{x, y, w, h}, bg);
	// 上辺ハイライト
	screen.drawRect(sgc::Rectf{x, y, w, 1}, {1, 1, 1, 0.3f});
	// 下辺シャドウ
	screen.drawRect(sgc::Rectf{x, y + h - 1, w, 1}, {0, 0, 0, 0.3f});
	screen.drawTextInRect(sgc::Rectf{x, y, w, h}, text, textColor);
}

/// @brief ラベル+値ペアを描画する（例: "HP: 100"）
/// @param screen 描画先Screen
/// @param x X座標
/// @param y Y座標
/// @param w 幅
/// @param h 高さ
/// @param label ラベル文字列
/// @param value 値文字列
/// @param labelColor ラベル色
/// @param valueColor 値色
inline void drawLabelValue(Screen& screen, float x, float y, float w, float h,
	const std::string& label, const std::string& value,
	const sgc::Colorf& labelColor = {0.7f, 0.7f, 0.8f, 1},
	const sgc::Colorf& valueColor = {1, 1, 1, 1})
{
	const float labelW = w * 0.4f;
	screen.drawTextInRect(sgc::Rectf{x, y, labelW, h}, label, labelColor);
	screen.drawTextInRect(sgc::Rectf{x + labelW, y, w - labelW, h}, value, valueColor);
}

/// @brief 星評価を描画する（テキストベース）
/// @param screen 描画先Screen
/// @param x X座標
/// @param y Y座標
/// @param w 幅
/// @param h 高さ
/// @param rating 評価値
/// @param maxStars 最大星数
/// @param filledColor 充填星の色
/// @param emptyColor 空星の色
inline void drawStarRating(Screen& screen, float x, float y, float w, float h,
	int rating, int maxStars = 5,
	const sgc::Colorf& filledColor = {1, 0.85f, 0.2f, 1},
	const sgc::Colorf& emptyColor = {0.4f, 0.4f, 0.4f, 1})
{
	rating = std::clamp(rating, 0, maxStars);
	std::string stars;
	for (int i = 0; i < rating; ++i) stars += "*";
	for (int i = rating; i < maxStars; ++i) stars += ".";
	(void)emptyColor; // Phase 1: 単色描画（充填色のみ使用）
	screen.drawTextInRect(sgc::Rectf{x, y, w, h}, stars, filledColor);
}

/// @brief ツールチップ風ポップアップを描画する
/// @param screen 描画先Screen
/// @param x X座標
/// @param y Y座標
/// @param text 表示テキスト
/// @param bg 背景色
inline void drawTooltip(Screen& screen, float x, float y,
	const std::string& text, const sgc::Colorf& bg = {0.1f, 0.1f, 0.15f, 0.95f})
{
	const float w = static_cast<float>(text.size()) * 8.0f + 16.0f;
	const float h = 24.0f;
	screen.drawRect(sgc::Rectf{x, y, w, h}, bg);
	screen.drawTextInRect(sgc::Rectf{x + 4, y + 4, w - 8, h - 8}, text, {1, 1, 1, 1});
}

/// @brief 画面上部に通知バナーを描画する
/// @param screen 描画先Screen
/// @param screenW 画面幅
/// @param text 通知テキスト
/// @param progress 表示進捗（0.0でフェードアウト完了、1.0で完全表示）
/// @param bg 背景色
inline void drawNotification(Screen& screen, float screenW,
	const std::string& text, float progress = 1.0f,
	const sgc::Colorf& bg = {0.2f, 0.5f, 0.3f, 0.9f})
{
	if (progress <= 0.0f) return;
	const float h = 30.0f * std::min(progress, 1.0f);
	const float w = screenW * 0.6f;
	const float x = (screenW - w) * 0.5f;
	screen.drawRect(sgc::Rectf{x, 0, w, h}, bg);
	if (progress >= 0.5f)
	{
		screen.drawTextInRect(sgc::Rectf{x + 10, 5, w - 20, 20}, text, {1, 1, 1, 1});
	}
}

} // namespace mitiru::ui
