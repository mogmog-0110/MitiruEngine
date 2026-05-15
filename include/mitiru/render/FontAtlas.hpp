#pragma once

/// @file FontAtlas.hpp
/// @brief BitmapFontからスケーリングされたフォントアトラスを生成する
/// @details 8x8 BitmapFontデータをscale倍に拡大してテクスチャアトラスを生成する。
///          stb_truetype等の外部依存なしで、より高解像度のフォント表示が可能になる。
///
/// @code
/// auto atlas = mitiru::render::FontAtlas::generate(2);
/// // atlas は 128x48 (scale=1) または 256x96 (scale=2) のテクスチャ
/// @endcode

#include <cstdint>
#include <vector>

#include <mitiru/render/BitmapFont.hpp>
#include <mitiru/render/Texture.hpp>

namespace mitiru::render
{

/// @brief BitmapFontベースのフォントアトラスジェネレータ
/// @details BitmapFontの8x8グリフデータを任意のスケールに拡大し、
///          16列×6行のアトラステクスチャとして出力する。
///          ASCII 32-126の95文字をサポートする。
class FontAtlas
{
public:
	static constexpr int ATLAS_COLS = 16;  ///< アトラスの列数
	static constexpr int ATLAS_ROWS = 6;   ///< アトラスの行数（96セル ≥ 95文字）

	/// @brief 指定スケールでフォントアトラステクスチャを生成する
	/// @param scale 拡大率（1 = 8x8, 2 = 16x16, 3 = 24x24 ...）
	/// @return RGBA8形式のアトラステクスチャ
	[[nodiscard]] static Texture generate(int scale = 2)
	{
		if (scale < 1)
		{
			scale = 1;
		}

		const int cellW = BitmapFont::GLYPH_WIDTH * scale;
		const int cellH = BitmapFont::GLYPH_HEIGHT * scale;
		const int atlasW = ATLAS_COLS * cellW;
		const int atlasH = ATLAS_ROWS * cellH;

		std::vector<std::uint8_t> pixels(
			static_cast<std::size_t>(atlasW) * atlasH * 4, 0);

		/// ASCII 32-126の各文字を描画する
		for (int ch = BitmapFont::FIRST_CHAR; ch <= BitmapFont::LAST_CHAR; ++ch)
		{
			const int idx = ch - BitmapFont::FIRST_CHAR;
			const int col = idx % ATLAS_COLS;
			const int row = idx / ATLAS_COLS;
			const auto glyphData = BitmapFont::glyph(static_cast<char>(ch));

			for (int gy = 0; gy < BitmapFont::GLYPH_HEIGHT; ++gy)
			{
				for (int gx = 0; gx < BitmapFont::GLYPH_WIDTH; ++gx)
				{
					if (!(glyphData[gy] & (0x80 >> gx)))
					{
						continue;
					}

					/// scale×scaleブロックを塗りつぶす
					for (int sy = 0; sy < scale; ++sy)
					{
						for (int sx = 0; sx < scale; ++sx)
						{
							const int px = col * cellW + gx * scale + sx;
							const int py = row * cellH + gy * scale + sy;
							const auto pi = static_cast<std::size_t>(
								(py * atlasW + px) * 4);
							pixels[pi + 0] = 255;
							pixels[pi + 1] = 255;
							pixels[pi + 2] = 255;
							pixels[pi + 3] = 255;
						}
					}
				}
			}
		}

		return Texture(atlasW, atlasH, pixels);
	}

	/// @brief セル幅を取得する（指定スケール）
	/// @param scale 拡大率
	/// @return セル幅（ピクセル）
	[[nodiscard]] static constexpr int cellWidth(int scale = 2) noexcept
	{
		return BitmapFont::GLYPH_WIDTH * (scale < 1 ? 1 : scale);
	}

	/// @brief セル高さを取得する（指定スケール）
	/// @param scale 拡大率
	/// @return セル高さ（ピクセル）
	[[nodiscard]] static constexpr int cellHeight(int scale = 2) noexcept
	{
		return BitmapFont::GLYPH_HEIGHT * (scale < 1 ? 1 : scale);
	}

	/// @brief 指定文字のアトラス上のUV座標を取得する
	/// @param ch ASCII文字
	/// @param scale 生成時と同じスケール値
	/// @param outU0 左上U座標（出力）
	/// @param outV0 左上V座標（出力）
	/// @param outU1 右下U座標（出力）
	/// @param outV1 右下V座標（出力）
	static void glyphUV(char ch, int scale,
		float& outU0, float& outV0, float& outU1, float& outV1)
	{
		const int idx = static_cast<int>(ch) - BitmapFont::FIRST_CHAR;
		if (idx < 0 || idx >= BitmapFont::GLYPH_COUNT)
		{
			outU0 = outV0 = outU1 = outV1 = 0.0f;
			return;
		}

		const int cellW = BitmapFont::GLYPH_WIDTH * scale;
		const int cellH = BitmapFont::GLYPH_HEIGHT * scale;
		const float atlasW = static_cast<float>(ATLAS_COLS * cellW);
		const float atlasH = static_cast<float>(ATLAS_ROWS * cellH);

		const int col = idx % ATLAS_COLS;
		const int row = idx / ATLAS_COLS;

		outU0 = static_cast<float>(col * cellW) / atlasW;
		outV0 = static_cast<float>(row * cellH) / atlasH;
		outU1 = static_cast<float>((col + 1) * cellW) / atlasW;
		outV1 = static_cast<float>((row + 1) * cellH) / atlasH;
	}
};

} // namespace mitiru::render
