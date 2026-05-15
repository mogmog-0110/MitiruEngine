#pragma once

/// @file TextRenderer.hpp
/// @brief ビットマップテキストレンダラー
/// @details BitmapFontを使用してテキストをスケーラブルに描画する。
///          各グリフのピクセルを小さな矩形として描画する。

#include <algorithm>
#include <string_view>

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/BitmapFont.hpp>

namespace mitiru
{
class Screen;
} // namespace mitiru

namespace mitiru::render
{

/// @brief ビットマップテキストレンダラー
/// @details BitmapFontのグリフデータを読み取り、各ONピクセルを
///          (scale x scale)サイズの矩形としてScreenに描画する。
///
/// @code
/// mitiru::Screen screen(800, 600);
/// mitiru::render::TextRenderer::drawText(screen, "Hello", 10.0f, 20.0f, 2, sgc::Colorf::white());
/// @endcode
class TextRenderer
{
public:
	/// @brief テキストを描画する（整数スケール — 後方互換）
	/// @param screen 描画先Screen
	/// @param text 描画テキスト（ASCII）
	/// @param x 左上X座標
	/// @param y 左上Y座標
	/// @param scale 整数スケール（デフォルト1）
	/// @param color 描画色
	template <typename ScreenType>
	static void drawText(ScreenType& screen, std::string_view text,
	                      float x, float y, int scale,
	                      const sgc::Colorf& color)
	{
		drawTextFloat(screen, text, x, y, static_cast<float>(std::max(1, scale)), color);
	}

	/// @brief テキストを描画する（floatスケール — 任意のfontSizeに対応）
	/// @param screen 描画先Screen
	/// @param text 描画テキスト（ASCII）
	/// @param x 左上X座標
	/// @param y 左上Y座標
	/// @param scale floatスケール（1.0 = 8px, 1.5 = 12px, 2.0 = 16px）
	/// @param color 描画色
	/// @param letterSpacing 文字間スペーシング（ピクセル、デフォルト0）
	template <typename ScreenType>
	static void drawTextFloat(ScreenType& screen, std::string_view text,
	                           float x, float y, float scale,
	                           const sgc::Colorf& color,
	                           float letterSpacing = 0.0f)
	{
		const float pixelSize = std::max(0.5f, scale);
		float cursorX = x;

		for (const char ch : text)
		{
			const auto glyphData = BitmapFont::glyph(ch);

			for (int row = 0; row < BitmapFont::GLYPH_HEIGHT; ++row)
			{
				const std::uint8_t rowBits = glyphData[row];
				if (rowBits == 0)
				{
					continue;
				}

				for (int col = 0; col < BitmapFont::GLYPH_WIDTH; ++col)
				{
					if (rowBits & (0x80 >> col))
					{
						const float px = cursorX + static_cast<float>(col) * pixelSize;
						const float py = y + static_cast<float>(row) * pixelSize;
						screen.drawRect(
							sgc::Rectf{px, py, pixelSize, pixelSize},
							color);
					}
				}
			}

			cursorX += static_cast<float>(BitmapFont::GLYPH_WIDTH) * pixelSize + letterSpacing;
		}
	}

	/// @brief テキスト幅を計算する（floatスケール対応）
	/// @param text 計測対象テキスト
	/// @param scale floatスケール
	/// @param letterSpacing 文字間スペーシング（ピクセル、デフォルト0）
	/// @return 描画幅（ピクセル）
	[[nodiscard]] static constexpr float measureWidthFloat(std::string_view text, float scale,
	                                                       float letterSpacing = 0.0f) noexcept
	{
		if (text.empty()) return 0.0f;
		const float glyphW = static_cast<float>(BitmapFont::GLYPH_WIDTH) * scale;
		return static_cast<float>(text.size()) * glyphW
		     + static_cast<float>(text.size() - 1) * letterSpacing;
	}

	/// @brief テキスト高さを計算する（floatスケール対応）
	/// @param scale floatスケール
	/// @return 描画高さ（ピクセル）
	[[nodiscard]] static constexpr float measureHeightFloat(float scale) noexcept
	{
		return static_cast<float>(BitmapFont::GLYPH_HEIGHT) * scale;
	}

	/// @brief テキスト幅を計算する
	/// @param text 計測対象テキスト
	/// @param scale 拡大率（デフォルト1）
	/// @return 描画幅（ピクセル）
	[[nodiscard]] static constexpr int measureWidth(std::string_view text, int scale = 1) noexcept
	{
		return BitmapFont::textWidth(text, scale);
	}

	/// @brief テキスト高さを計算する
	/// @param scale 拡大率（デフォルト1）
	/// @return 描画高さ（ピクセル）
	[[nodiscard]] static constexpr int measureHeight(int scale = 1) noexcept
	{
		return BitmapFont::textHeight(scale);
	}
};

} // namespace mitiru::render
