#pragma once

/// @file SpeechBubble.hpp
/// @brief 吹き出し（スピーチバブル）配置ユーティリティ
///
/// キャラクターやオブジェクトの近くに表示する吹き出しの
/// 位置とテール（三角形の矢印）を計算する。
///
/// @code
/// using namespace sgc::ui;
/// auto bubble = evaluateSpeechBubble(
///     {100.0f, 200.0f},  // 話者の位置
///     {200.0f, 80.0f},   // 吹き出しサイズ
///     {800.0f, 600.0f},  // 画面サイズ
///     BubbleDirection::Above
/// );
/// // bubble.bounds で吹き出し本体を描画
/// // bubble.tailTip, tailLeft, tailRight でテール三角形を描画
/// @endcode

#include "sgc/math/Rect.hpp"

namespace sgc::ui
{

/// @brief 吹き出しの表示方向
enum class BubbleDirection
{
	Above,  ///< 話者の上に表示
	Below,  ///< 話者の下に表示
	Left,   ///< 話者の左に表示
	Right,  ///< 話者の右に表示
};

/// @brief 吹き出しの評価結果
struct SpeechBubbleResult
{
	Rectf bounds{};          ///< 吹き出し本体の矩形
	Vec2f tailTip{};         ///< テール先端（話者側）
	Vec2f tailLeft{};        ///< テール左角（吹き出し側）
	Vec2f tailRight{};       ///< テール右角（吹き出し側）
	BubbleDirection actualDirection{BubbleDirection::Above};  ///< 実際の表示方向
};

/// @brief テールの長さ（吹き出し本体からの突出量）
inline constexpr float BUBBLE_TAIL_LENGTH = 12.0f;

/// @brief テールの幅
inline constexpr float BUBBLE_TAIL_WIDTH = 16.0f;

/// @brief 話者と吹き出しの間の余白
inline constexpr float BUBBLE_GAP = 6.0f;

/// @brief 吹き出しの位置とテール形状を計算する
///
/// 話者の位置に基づいて吹き出しの配置を計算する。
/// 画面外にはみ出す場合は反対方向にフリップする。
///
/// @param speakerPos 話者の座標（ワールドまたはスクリーン）
/// @param bubbleSize 吹き出し本体のサイズ（幅, 高さ）
/// @param screenSize 画面サイズ（クランプ用）
/// @param direction 優先表示方向
/// @param tailLength テールの長さ
/// @param tailWidth テールの幅
/// @param gap 話者との余白
/// @return 吹き出しの配置結果
[[nodiscard]] constexpr SpeechBubbleResult evaluateSpeechBubble(
	const Vec2f& speakerPos,
	const Vec2f& bubbleSize,
	const Vec2f& screenSize,
	BubbleDirection direction = BubbleDirection::Above,
	float tailLength = BUBBLE_TAIL_LENGTH,
	float tailWidth = BUBBLE_TAIL_WIDTH,
	float gap = BUBBLE_GAP) noexcept
{
	SpeechBubbleResult result;
	result.actualDirection = direction;

	// 吹き出し本体の位置を計算
	const auto computePosition = [&](BubbleDirection dir) constexpr -> Vec2f
	{
		switch (dir)
		{
		case BubbleDirection::Above:
			return {
				speakerPos.x - bubbleSize.x * 0.5f,
				speakerPos.y - gap - tailLength - bubbleSize.y
			};
		case BubbleDirection::Below:
			return {
				speakerPos.x - bubbleSize.x * 0.5f,
				speakerPos.y + gap + tailLength
			};
		case BubbleDirection::Left:
			return {
				speakerPos.x - gap - tailLength - bubbleSize.x,
				speakerPos.y - bubbleSize.y * 0.5f
			};
		case BubbleDirection::Right:
			return {
				speakerPos.x + gap + tailLength,
				speakerPos.y - bubbleSize.y * 0.5f
			};
		}
		return {};
	};

	// 画面内判定
	const auto fitsInScreen = [&](const Vec2f& pos) constexpr -> bool
	{
		return pos.x >= 0.0f && pos.y >= 0.0f
			&& pos.x + bubbleSize.x <= screenSize.x
			&& pos.y + bubbleSize.y <= screenSize.y;
	};

	// 反対方向
	const auto flipDir = [](BubbleDirection dir) constexpr -> BubbleDirection
	{
		switch (dir)
		{
		case BubbleDirection::Above: return BubbleDirection::Below;
		case BubbleDirection::Below: return BubbleDirection::Above;
		case BubbleDirection::Left:  return BubbleDirection::Right;
		case BubbleDirection::Right: return BubbleDirection::Left;
		}
		return BubbleDirection::Above;
	};

	Vec2f pos = computePosition(direction);

	// フリップ判定
	if (!fitsInScreen(pos))
	{
		const auto flipped = flipDir(direction);
		const Vec2f flippedPos = computePosition(flipped);
		if (fitsInScreen(flippedPos))
		{
			pos = flippedPos;
			result.actualDirection = flipped;
		}
	}

	// 画面境界クランプ
	if (pos.x < 0.0f) pos.x = 0.0f;
	if (pos.y < 0.0f) pos.y = 0.0f;
	if (pos.x + bubbleSize.x > screenSize.x)
		pos.x = screenSize.x - bubbleSize.x;
	if (pos.y + bubbleSize.y > screenSize.y)
		pos.y = screenSize.y - bubbleSize.y;

	result.bounds = Rectf{pos, bubbleSize};

	// テール三角形を計算
	const float halfTailW = tailWidth * 0.5f;
	switch (result.actualDirection)
	{
	case BubbleDirection::Above:
	{
		// テール先端は下（話者に向かう）
		const float cx = pos.x + bubbleSize.x * 0.5f;
		result.tailTip = {cx, pos.y + bubbleSize.y + tailLength};
		result.tailLeft = {cx - halfTailW, pos.y + bubbleSize.y};
		result.tailRight = {cx + halfTailW, pos.y + bubbleSize.y};
		break;
	}
	case BubbleDirection::Below:
	{
		// テール先端は上（話者に向かう）
		const float cx = pos.x + bubbleSize.x * 0.5f;
		result.tailTip = {cx, pos.y - tailLength};
		result.tailLeft = {cx + halfTailW, pos.y};
		result.tailRight = {cx - halfTailW, pos.y};
		break;
	}
	case BubbleDirection::Left:
	{
		// テール先端は右（話者に向かう）
		const float cy = pos.y + bubbleSize.y * 0.5f;
		result.tailTip = {pos.x + bubbleSize.x + tailLength, cy};
		result.tailLeft = {pos.x + bubbleSize.x, cy - halfTailW};
		result.tailRight = {pos.x + bubbleSize.x, cy + halfTailW};
		break;
	}
	case BubbleDirection::Right:
	{
		// テール先端は左（話者に向かう）
		const float cy = pos.y + bubbleSize.y * 0.5f;
		result.tailTip = {pos.x - tailLength, cy};
		result.tailLeft = {pos.x, cy + halfTailW};
		result.tailRight = {pos.x, cy - halfTailW};
		break;
	}
	}

	return result;
}

} // namespace sgc::ui
