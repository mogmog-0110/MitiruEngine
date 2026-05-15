#pragma once

/// @file TrueTypeScreenRenderer.hpp
/// @brief TrueTypeFont → Screen 効率的レンダラー
/// @details グリフビットマップを水平ランレングス圧縮し、連続ピクセルを
///          1つのdrawRectにまとめて描画する。1文字あたりの描画コール数を
///          ピクセル数(数百)から行数+ラン数(数十)に削減する。

#include <mitiru/core/Screen.hpp>
#include <mitiru/vn/TrueTypeFont.hpp>
#include <sgc/types/Color.hpp>

#include <string_view>

namespace mitiru::render
{

/// @brief UTF-8文字列をコードポイント数でトリミングする
/// @param text UTF-8文字列
/// @param maxCodepoints 最大コードポイント数
/// @return トリミングされた文字列（UTF-8バイト境界で正しく切断）
[[nodiscard]] inline std::string utf8Substr(std::string_view text, std::size_t maxCodepoints)
{
	std::size_t bytePos = 0;
	std::size_t cpCount = 0;
	while (bytePos < text.size() && cpCount < maxCodepoints)
	{
		const auto b = static_cast<unsigned char>(text[bytePos]);
		std::size_t charLen = 1;
		if (b >= 0xF0) { charLen = 4; }
		else if (b >= 0xE0) { charLen = 3; }
		else if (b >= 0xC0) { charLen = 2; }
		bytePos += std::min(charLen, text.size() - bytePos);
		++cpCount;
	}
	return std::string(text.substr(0, bytePos));
}

/// @brief UTF-8文字列のコードポイント数を数える
[[nodiscard]] inline std::size_t utf8Length(std::string_view text)
{
	std::size_t count = 0;
	std::size_t i = 0;
	while (i < text.size())
	{
		const auto b = static_cast<unsigned char>(text[i]);
		if (b >= 0xF0) { i += 4; }
		else if (b >= 0xE0) { i += 3; }
		else if (b >= 0xC0) { i += 2; }
		else { i += 1; }
		++count;
	}
	return count;
}

/// @brief TrueTypeFontのグリフをScreen上に効率的に描画する
/// @details 各グリフのビットマップを走査し、同一行の連続する非透明ピクセルを
///          1つの矩形にまとめて描画する（ランレングス方式）。
///          これにより1文字あたりのdrawRect呼び出しを大幅に削減する。
class TrueTypeScreenRenderer
{
public:
	/// @brief テキストを描画する
	/// @param font TrueTypeFontインスタンス
	/// @param screen 描画先Screen
	/// @param x 左上X座標
	/// @param y ベースラインY座標
	/// @param text UTF-8テキスト
	/// @param fontSize フォントサイズ（ピクセル）
	/// @param color 描画色
	static void drawText(vn::TrueTypeFont& font, Screen& screen,
	                      float x, float y, std::string_view text,
	                      float fontSize, const sgc::Colorf& color)
	{
		if (!font.valid() || text.empty()) { return; }

		// yはテキストの上端位置。ベースラインはy+ascentの位置。
		// グリフのoffsetYはベースラインからの相対値なので、
		// 描画位置をy+ascentベースに変換する。
		const auto m = font.metrics(fontSize);
		const float baselineY = y + m.ascent;

		float cx = x;
		std::uint32_t prevCp = 0;
		vn::Utf8Iterator it(text);

		while (it.hasNext())
		{
			const std::uint32_t cp = it.next();

			// カーニング
			if (prevCp != 0)
			{
				cx += font.kerning(prevCp, cp, fontSize);
			}

			// グリフ取得
			auto* glyph = font.getGlyph(cp, fontSize);
			if (glyph && glyph->width > 0 && glyph->height > 0
				&& !glyph->bitmap.empty())
			{
				drawGlyphOptimized(screen, *glyph, cx, baselineY, color);
			}

			cx += font.advanceWidth(cp, fontSize);
			prevCp = cp;
		}
	}

	/// @brief テキスト幅を計測する
	static float measureWidth(vn::TrueTypeFont& font,
	                           std::string_view text, float fontSize)
	{
		return font.measureText(text, fontSize);
	}

	/// @brief 行の高さを計測する
	static float measureHeight(vn::TrueTypeFont& font, float fontSize)
	{
		return font.metrics(fontSize).lineHeight;
	}

private:
	/// @brief 1グリフをアルファ分割ランレングス方式で描画する
	/// @details 各行を走査し、同一アルファ値が続く連続ピクセルを1つのdrawRectに
	///          まとめて描画する。従来の平均アルファ方式と異なり、エッジの
	///          アンチエイリアシングを正確に再現する。
	static void drawGlyphOptimized(Screen& screen, const vn::GlyphInfo& glyph,
	                                float baseX, float baseY,
	                                const sgc::Colorf& color)
	{
		static constexpr std::uint8_t kAlphaThreshold = 8;

		/// @brief アルファ値を32段階に量子化する
		/// @details 完全にピクセル単位だとdrawRectが多すぎるため、
		///          近いアルファ値は同一ランとしてまとめる（255/8=約32段階）
		static constexpr int kAlphaQuantize = 8;

		const float glyphX = baseX + static_cast<float>(glyph.offsetX);
		const float glyphY = baseY + static_cast<float>(glyph.offsetY);

		for (int row = 0; row < glyph.height; ++row)
		{
			const float py = glyphY + static_cast<float>(row);
			int col = 0;

			while (col < glyph.width)
			{
				const auto idx = static_cast<std::size_t>(row * glyph.width + col);
				if (idx >= glyph.bitmap.size() || glyph.bitmap[idx] < kAlphaThreshold)
				{
					++col;
					continue;
				}

				// ランの開始: 同じ量子化アルファ値が続く限りまとめる
				const int runStart = col;
				const auto startAlpha = glyph.bitmap[idx];
				const int quantized = startAlpha / kAlphaQuantize;
				float alphaSum = 0.0f;
				int runLen = 0;

				while (col < glyph.width)
				{
					const auto ridx = static_cast<std::size_t>(row * glyph.width + col);
					if (ridx >= glyph.bitmap.size()) { break; }
					const auto a = glyph.bitmap[ridx];
					if (a < kAlphaThreshold) { break; }
					if ((a / kAlphaQuantize) != quantized) { break; }

					alphaSum += static_cast<float>(a) / 255.0f;
					++runLen;
					++col;
				}

				if (runLen > 0)
				{
					const float avgAlpha = alphaSum / static_cast<float>(runLen);
					const float px = glyphX + static_cast<float>(runStart);
					screen.drawRect(
						sgc::Rectf{px, py, static_cast<float>(runLen), 1.0f},
						{color.r, color.g, color.b, avgAlpha * color.a});
				}
			}
		}
	}
};

} // namespace mitiru::render
