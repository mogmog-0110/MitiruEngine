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
	/// @brief テキストを描画する
	/// @param screen 描画先Screen
	/// @param text 描画テキスト（ASCII）
	/// @param x 左上X座標
	/// @param y 左上Y座標
	/// @param scale スケール（デフォルト1）
	/// @param color 描画色
	template <typename ScreenType>
	static void drawText(ScreenType& screen, std::string_view text,
	                      float x, float y, int scale,
	                      const sgc::Colorf& color)
	{
		const int pixelSize = std::max(1, scale);
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
						const float px = cursorX + static_cast<float>(col * pixelSize);
						const float py = y + static_cast<float>(row * pixelSize);
						screen.drawRect(
							sgc::Rectf{px, py,
							           static_cast<float>(pixelSize),
							           static_cast<float>(pixelSize)},
							color);
					}
				}
			}

			cursorX += static_cast<float>(BitmapFont::GLYPH_WIDTH * pixelSize);
		}
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
