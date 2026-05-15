#pragma once

/// @file FontRenderer.hpp
/// @brief 拡張フォントレンダラー
/// @details BitmapFontをラップし、テキスト計測・ワードラップ・中央揃えなどの
///          高レベルテキスト描画機能を提供する。

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/BitmapFont.hpp>
#include <mitiru/render/TextRenderer.hpp>

namespace mitiru
{
class Screen;
} // namespace mitiru

namespace mitiru::render
{

/// @brief 拡張フォントレンダラー
/// @details BitmapFontを内部で使用し、テキスト計測・ワードラップ・
///          中央揃え描画などの高レベル機能を提供する。
///
/// @code
/// mitiru::render::FontRenderer fontRenderer;
/// auto size = fontRenderer.measureText("Hello World", 2.0f);
/// fontRenderer.drawTextWrapped(screen, "Long text...", 10, 10, 200, 2.0f, sgc::Colorf::white());
/// fontRenderer.drawTextCentered(screen, "Title", 400, 300, 3.0f, sgc::Colorf::white());
/// @endcode
class FontRenderer
{
public:
	/// @brief デフォルトコンストラクタ
	FontRenderer() noexcept = default;

	/// @brief テキストの描画サイズを計測する
	/// @param text 計測対象テキスト（ASCII）
	/// @param scale 拡大率
	/// @return 描画サイズ（幅, 高さ）
	[[nodiscard]] sgc::Vec2f measureText(std::string_view text, float scale) const noexcept
	{
		const int intScale = std::max(1, static_cast<int>(std::round(scale)));
		const float width = static_cast<float>(
			BitmapFont::textWidth(text, intScale));
		const float height = static_cast<float>(
			BitmapFont::textHeight(intScale));
		return sgc::Vec2f{width, height};
	}

	/// @brief テキストをワードラップして描画する
	/// @tparam ScreenType Screen型（テンプレートで前方宣言対応）
	/// @param screen 描画先Screen
	/// @param text 描画テキスト（ASCII、スペースで区切り）
	/// @param x 左上X座標
	/// @param y 左上Y座標
	/// @param maxWidth 最大行幅（ピクセル）
	/// @param scale 拡大率
	/// @param color 描画色
	template <typename ScreenType>
	void drawTextWrapped(ScreenType& screen, std::string_view text,
	                     float x, float y, float maxWidth,
	                     float scale, const sgc::Colorf& color) const
	{
		const int intScale = std::max(1, static_cast<int>(std::round(scale)));
		const float charWidth = static_cast<float>(
			BitmapFont::GLYPH_WIDTH * intScale);
		const float lineHeight = static_cast<float>(
			BitmapFont::GLYPH_HEIGHT * intScale);

		/// テキストを単語単位で分割する
		const auto words = splitWords(text);

		float cursorX = x;
		float cursorY = y;
		bool firstWordOnLine = true;

		for (const auto& word : words)
		{
			const float wordWidth = static_cast<float>(word.size()) * charWidth;
			const float spaceWidth = firstWordOnLine ? 0.0f : charWidth;

			/// 行幅を超える場合は改行する
			if (!firstWordOnLine && (cursorX - x + spaceWidth + wordWidth) > maxWidth)
			{
				cursorX = x;
				cursorY += lineHeight;
				firstWordOnLine = true;
			}

			/// 単語の前にスペースを挿入する（行頭以外）
			if (!firstWordOnLine)
			{
				cursorX += charWidth;
			}

			/// 単語を描画する
			TextRenderer::drawText(screen, word,
			                       cursorX, cursorY,
			                       intScale, color);

			cursorX += wordWidth;
			firstWordOnLine = false;
		}
	}

	/// @brief テキストを中央揃えで描画する
	/// @tparam ScreenType Screen型（テンプレートで前方宣言対応）
	/// @param screen 描画先Screen
	/// @param text 描画テキスト（ASCII）
	/// @param cx 中心X座標
	/// @param cy 中心Y座標
	/// @param scale 拡大率
	/// @param color 描画色
	template <typename ScreenType>
	void drawTextCentered(ScreenType& screen, std::string_view text,
	                      float cx, float cy, float scale,
	                      const sgc::Colorf& color) const
	{
		const int intScale = std::max(1, static_cast<int>(std::round(scale)));
		const auto size = measureText(text, scale);

		const float drawX = cx - size.x * 0.5f;
		const float drawY = cy - size.y * 0.5f;

		TextRenderer::drawText(screen, text,
		                       drawX, drawY,
		                       intScale, color);
	}

	/// @brief テキストを通常描画する（TextRendererの薄いラッパー）
	/// @tparam ScreenType Screen型
	/// @param screen 描画先Screen
	/// @param text 描画テキスト（ASCII）
	/// @param x 左上X座標
	/// @param y 左上Y座標
	/// @param scale 拡大率
	/// @param color 描画色
	template <typename ScreenType>
	void drawText(ScreenType& screen, std::string_view text,
	              float x, float y, float scale,
	              const sgc::Colorf& color) const
	{
		const int intScale = std::max(1, static_cast<int>(std::round(scale)));
		TextRenderer::drawText(screen, text, x, y, intScale, color);
	}

	/// @brief ワードラップ後の行数を計算する
	/// @param text 対象テキスト
	/// @param maxWidth 最大行幅（ピクセル）
	/// @param scale 拡大率
	/// @return 行数
	[[nodiscard]] int countWrappedLines(std::string_view text, float maxWidth,
	                                    float scale) const noexcept
	{
		const int intScale = std::max(1, static_cast<int>(std::round(scale)));
		const float charWidth = static_cast<float>(
			BitmapFont::GLYPH_WIDTH * intScale);

		const auto words = splitWords(text);

		int lineCount = 1;
		float cursorX = 0.0f;
		bool firstWordOnLine = true;

		for (const auto& word : words)
		{
			const float wordWidth = static_cast<float>(word.size()) * charWidth;
			const float spaceWidth = firstWordOnLine ? 0.0f : charWidth;

			if (!firstWordOnLine && (cursorX + spaceWidth + wordWidth) > maxWidth)
			{
				++lineCount;
				cursorX = 0.0f;
				firstWordOnLine = true;
			}

			if (!firstWordOnLine)
			{
				cursorX += charWidth;
			}

			cursorX += wordWidth;
			firstWordOnLine = false;
		}

		return lineCount;
	}

	/// @brief ワードラップ後の描画領域サイズを計算する
	/// @param text 対象テキスト
	/// @param maxWidth 最大行幅（ピクセル）
	/// @param scale 拡大率
	/// @return 描画領域サイズ（幅, 高さ）
	[[nodiscard]] sgc::Vec2f measureTextWrapped(std::string_view text, float maxWidth,
	                                            float scale) const noexcept
	{
		const int intScale = std::max(1, static_cast<int>(std::round(scale)));
		const float lineHeight = static_cast<float>(
			BitmapFont::GLYPH_HEIGHT * intScale);
		const int lines = countWrappedLines(text, maxWidth, scale);

		return sgc::Vec2f{maxWidth, lineHeight * static_cast<float>(lines)};
	}

private:
	/// @brief テキストをスペースで単語に分割する
	/// @param text 分割対象テキスト
	/// @return 単語の配列
	[[nodiscard]] static std::vector<std::string_view> splitWords(std::string_view text) noexcept
	{
		std::vector<std::string_view> words;
		std::size_t start = 0;

		while (start < text.size())
		{
			/// 先頭のスペースをスキップする
			while (start < text.size() && text[start] == ' ')
			{
				++start;
			}

			if (start >= text.size())
			{
				break;
			}

			/// 次のスペースまでが1単語
			std::size_t end = start;
			while (end < text.size() && text[end] != ' ')
			{
				++end;
			}

			words.push_back(text.substr(start, end - start));
			start = end;
		}

		return words;
	}
};

} // namespace mitiru::render
