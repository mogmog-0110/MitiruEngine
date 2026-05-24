#pragma once

/// @file TextLayout.hpp
/// @brief テキストサイズから矩形を計算するユーティリティ
///
/// ITextMeasureを使ってテキストの描画サイズを取得し、
/// パディングを加味したウィジェット全体の矩形を計算する。
/// ボタンやラベルのサイズ自動決定に使用する。
///
/// @code
/// using namespace sgc::ui;
/// auto rect = textRect(measure, "Hello", 24.0f, Margin::uniform(8.0f));
/// auto btn = textRectAt(measure, "OK", 20.0f, {100, 200}, Margin::symmetric(16, 8));
/// auto centered = textRectCentered(measure, "Title", 32.0f, screenCenter);
/// float fontSize = fitTextInRect(measure, "Long text", 48.0f, targetRect);
/// @endcode

#include <string_view>

#include "sgc/graphics/ITextMeasure.hpp"
#include "sgc/math/Rect.hpp"
#include "sgc/ui/Anchor.hpp"

namespace sgc::ui
{

/// @brief テキストサイズ + パディングの矩形を計算する（左上(0,0)基準）
///
/// テキストを計測し、パディングを加味した全体サイズの矩形を返す。
/// 位置は(0,0)に配置される。
///
/// @param measure テキスト計測インターフェース
/// @param text テキスト文字列
/// @param fontSize フォントサイズ
/// @param padding パディング（デフォルト: ゼロ）
/// @return テキスト + パディングの矩形（左上(0,0)）
[[nodiscard]] inline Rectf textRect(
	const ITextMeasure& measure,
	std::string_view text, float fontSize,
	const Margin& padding = {}) noexcept
{
	const Vec2f textSize = measure.measure(text, fontSize);
	const float w = textSize.x + padding.left + padding.right;
	const float h = textSize.y + padding.top + padding.bottom;
	return Rectf{0.0f, 0.0f, w, h};
}

/// @brief テキストサイズ + パディングの矩形を指定位置に配置する
///
/// @param measure テキスト計測インターフェース
/// @param text テキスト文字列
/// @param fontSize フォントサイズ
/// @param position 左上座標
/// @param padding パディング（デフォルト: ゼロ）
/// @return テキスト + パディングの矩形（指定位置基準）
[[nodiscard]] inline Rectf textRectAt(
	const ITextMeasure& measure,
	std::string_view text, float fontSize,
	const Vec2f& position,
	const Margin& padding = {}) noexcept
{
	const Vec2f textSize = measure.measure(text, fontSize);
	const float w = textSize.x + padding.left + padding.right;
	const float h = textSize.y + padding.top + padding.bottom;
	return Rectf{position.x, position.y, w, h};
}

/// @brief テキストサイズ + パディングの矩形を中央配置する
///
/// @param measure テキスト計測インターフェース
/// @param text テキスト文字列
/// @param fontSize フォントサイズ
/// @param center 中央座標
/// @param padding パディング（デフォルト: ゼロ）
/// @return テキスト + パディングの矩形（中央配置）
[[nodiscard]] inline Rectf textRectCentered(
	const ITextMeasure& measure,
	std::string_view text, float fontSize,
	const Vec2f& center,
	const Margin& padding = {}) noexcept
{
	const Vec2f textSize = measure.measure(text, fontSize);
	const float w = textSize.x + padding.left + padding.right;
	const float h = textSize.y + padding.top + padding.bottom;
	return Rectf{center.x - w * 0.5f, center.y - h * 0.5f, w, h};
}

/// @brief テキストからボタンサイズを算出する
///
/// 位置を含まないサイズのみを返す便利関数。
///
/// @param measure テキスト計測インターフェース
/// @param text テキスト文字列
/// @param fontSize フォントサイズ
/// @param hPadding 水平パディング（左右それぞれに適用）
/// @param vPadding 垂直パディング（上下それぞれに適用）
/// @return テキスト + パディングのサイズ
[[nodiscard]] inline Vec2f buttonSizeFromText(
	const ITextMeasure& measure,
	std::string_view text, float fontSize,
	float hPadding, float vPadding) noexcept
{
	const Vec2f textSize = measure.measure(text, fontSize);
	return {textSize.x + hPadding * 2.0f, textSize.y + vPadding * 2.0f};
}

/// @brief アンカー基準でラベル矩形を配置する
///
/// 指定位置にアンカー整列でテキスト矩形を配置する。
/// 例: anchor=Center なら、テキストの中心がpositionに合う。
///
/// @param measure テキスト計測インターフェース
/// @param text テキスト文字列
/// @param fontSize フォントサイズ
/// @param position 基準座標
/// @param anchor アンカー位置
/// @param padding パディング（デフォルト: ゼロ）
/// @return アンカー整列されたラベル矩形
[[nodiscard]] inline Rectf labelBounds(
	const ITextMeasure& measure,
	std::string_view text, float fontSize,
	const Vec2f& position, Anchor anchor,
	const Margin& padding = {}) noexcept
{
	const Vec2f textSize = measure.measure(text, fontSize);
	const float w = textSize.x + padding.left + padding.right;
	const float h = textSize.y + padding.top + padding.bottom;

	float x = position.x;
	float y = position.y;

	// 水平配置
	switch (anchor)
	{
	case Anchor::TopLeft:
	case Anchor::CenterLeft:
	case Anchor::BottomLeft:
		// 左寄せ: そのまま
		break;
	case Anchor::TopCenter:
	case Anchor::Center:
	case Anchor::BottomCenter:
		x -= w * 0.5f;
		break;
	case Anchor::TopRight:
	case Anchor::CenterRight:
	case Anchor::BottomRight:
		x -= w;
		break;
	}

	// 垂直配置
	switch (anchor)
	{
	case Anchor::TopLeft:
	case Anchor::TopCenter:
	case Anchor::TopRight:
		// 上寄せ: そのまま
		break;
	case Anchor::CenterLeft:
	case Anchor::Center:
	case Anchor::CenterRight:
		y -= h * 0.5f;
		break;
	case Anchor::BottomLeft:
	case Anchor::BottomCenter:
	case Anchor::BottomRight:
		y -= h;
		break;
	}

	return Rectf{x, y, w, h};
}

/// @brief テキストが指定矩形に収まる最大フォントサイズを検索する
///
/// maxFontSizeから1.0刻みで縮小し、テキストが収まるサイズを見つける。
/// 最小値は1.0fで、それでも収まらない場合は1.0fを返す。
///
/// @param measure テキスト計測インターフェース
/// @param text テキスト文字列
/// @param maxFontSize 試行する最大フォントサイズ
/// @param targetRect テキストを収めたい矩形
/// @return テキストが収まる最大フォントサイズ
[[nodiscard]] inline float fitTextInRect(
	const ITextMeasure& measure,
	std::string_view text, float maxFontSize,
	const Rectf& targetRect) noexcept
{
	const float targetW = targetRect.width();
	const float targetH = targetRect.height();
	const float minSize = 1.0f;

	// 二分探索で最適サイズを見つける
	float lo = minSize;
	float hi = maxFontSize;
	float bestSize = minSize;

	while (hi - lo > 0.5f)
	{
		const float mid = (lo + hi) * 0.5f;
		const Vec2f textSize = measure.measure(text, mid);
		if (textSize.x <= targetW && textSize.y <= targetH)
		{
			bestSize = mid;
			lo = mid;
		}
		else
		{
			hi = mid;
		}
	}

	return bestSize;
}

/// @brief 複数行テキストの合計高さを算出する
///
/// 行数と行間から合計高さを計算する。
/// lineSpacingは行間の余白（行の高さには含まない）。
///
/// @param measure テキスト計測インターフェース
/// @param lineCount 行数
/// @param fontSize フォントサイズ
/// @param lineSpacing 行間スペース（デフォルト: 0）
/// @return 合計高さ
[[nodiscard]] inline float multiLineHeight(
	const ITextMeasure& measure,
	int lineCount, float fontSize,
	float lineSpacing = 0.0f) noexcept
{
	if (lineCount <= 0)
	{
		return 0.0f;
	}
	const float singleHeight = measure.lineHeight(fontSize);
	return singleHeight * static_cast<float>(lineCount)
		+ lineSpacing * static_cast<float>(lineCount - 1);
}

} // namespace sgc::ui
