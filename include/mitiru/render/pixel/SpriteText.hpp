#pragma once
/// @file SpriteText.hpp
/// @brief SpriteFont (BMFont) を Screen に描く。字幅可変の見出し + 縁取り + 美咲フォールバック。
/// @details 各グリフを Screen::drawSprite でページテクスチャから blit する（テクスチャバッチに
///          乗る）。SpriteFont に無い文字は美咲 PixelText に自動フォールバックするので、
///          見出しはスタイル付きスプライト字・本文や未収録漢字は等幅ドット、と混在できる。
///
/// @code
/// using namespace mitiru::render::pixel;
/// drawSpriteText(screen, titleFont, 40, 30, u8"STAGE 1", 2, sgc::Colorf{1,1,1,1});
/// drawSpriteTextOutlined(screen, titleFont, 40, 80, u8"WARNING", 3,
///                        sgc::Colorf{1,0.9,0.2,1}, sgc::Colorf{0.2,0,0,1}, 2);
/// @endcode

#include <string_view>

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/core/Screen.hpp>
#include <mitiru/render/pixel/SpriteFont.hpp>
#include <mitiru/render/pixel/PixelText.hpp> // 未収録文字の美咲フォールバック + PixelAlign

namespace mitiru::render::pixel
{

/// @brief スプライトテキストのピクセル寸法。
struct SpriteSize { int w = 0; int h = 0; };

namespace detail
{
/// @brief 未収録文字を美咲で送るときの送り幅（8px セル基準）。
[[nodiscard]] inline int misakiAdvance(std::uint32_t cp) noexcept
{
	const auto* g = glyphFor(cp);
	return (g ? g->advance : 4);
}

/// @brief 1 コードポイントを UTF-8 文字列へ（美咲フォールバック描画用）。
[[nodiscard]] inline std::string utf8Of(std::uint32_t cp)
{
	std::string s;
	if (cp < 0x80) s.push_back(static_cast<char>(cp));
	else if (cp < 0x800)
	{ s.push_back(static_cast<char>(0xC0 | (cp >> 6))); s.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
	else if (cp < 0x10000)
	{ s.push_back(static_cast<char>(0xE0 | (cp >> 12))); s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
	  s.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
	else
	{ s.push_back(static_cast<char>(0xF0 | (cp >> 18))); s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
	  s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); s.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
	return s;
}
} // namespace detail

/// @brief テキストの描画寸法を測る（改行 '\n' 可、カーニング込み）。
[[nodiscard]] inline SpriteSize measureSpriteText(const SpriteFont& font,
                                                  std::string_view text, int scale)
{
	if (scale < 1) scale = 1;
	const auto cps = decodeUtf8(text);
	int x = 0, maxW = 0, lines = 1;
	std::uint32_t prev = 0;
	for (const auto cp : cps)
	{
		if (cp == '\n') { maxW = (x > maxW) ? x : maxW; x = 0; lines++; prev = 0; continue; }
		if (const auto* g = font.glyph(cp))
			x += (g->xadvance + font.kerning(prev, cp)) * scale;
		else
			x += detail::misakiAdvance(cp) * scale; // 美咲フォールバック幅
		prev = cp;
	}
	maxW = (x > maxW) ? x : maxW;
	return { maxW, lines * font.lineHeight() * scale };
}

/// @brief スプライト文字列を (x,y) を左上として描く。未収録文字は美咲で補う。
inline void drawSpriteText(Screen& screen, const SpriteFont& font, float x, float y,
                           std::string_view text, int scale, const sgc::Colorf& tint)
{
	if (scale < 1) scale = 1;
	const auto cps = decodeUtf8(text);
	const float s = static_cast<float>(scale);
	float penX = x; const float originX = x; float penY = y;
	std::uint32_t prev = 0;
	for (const auto cp : cps)
	{
		if (cp == '\n') { penX = originX; penY += static_cast<float>(font.lineHeight()) * s; prev = 0; continue; }

		const auto* g = font.glyph(cp);
		if (g != nullptr)
		{
			penX += static_cast<float>(font.kerning(prev, cp)) * s;
			const auto* tex = font.page(g->page);
			if (tex != nullptr && g->w > 0 && g->h > 0)
			{
				const sgc::Rectf dst{ penX + static_cast<float>(g->xoffset) * s,
				                      penY + static_cast<float>(g->yoffset) * s,
				                      static_cast<float>(g->w) * s, static_cast<float>(g->h) * s };
				const sgc::Rectf src{ static_cast<float>(g->x), static_cast<float>(g->y),
				                      static_cast<float>(g->w), static_cast<float>(g->h) };
				screen.drawSprite(*tex, dst, src, tint);
			}
			penX += static_cast<float>(g->xadvance) * s;
		}
		else
		{
			// 未収録: 美咲ドットで描く（行ベースラインに寄せる）。
			const float fy = penY + static_cast<float>(font.base() > 0 ? font.base() - kCellSize : 0) * s;
			drawPixelText(screen, penX, fy, detail::utf8Of(cp), scale, tint);
			penX += static_cast<float>(detail::misakiAdvance(cp)) * s;
		}
		prev = cp;
	}
}

/// @brief 縁取り付きで描く（fill の周囲 8 方向に outline 色、その上に fill）。
inline void drawSpriteTextOutlined(Screen& screen, const SpriteFont& font, float x, float y,
                                   std::string_view text, int scale,
                                   const sgc::Colorf& fill, const sgc::Colorf& outline, int outlineWidth = 1)
{
	const float w = static_cast<float>(outlineWidth < 1 ? 1 : outlineWidth);
	const float off[8][2] = { {-w,0},{w,0},{0,-w},{0,w},{-w,-w},{w,-w},{-w,w},{w,w} };
	for (auto& o : off) drawSpriteText(screen, font, x + o[0], y + o[1], text, scale, outline);
	drawSpriteText(screen, font, x, y, text, scale, fill);
}

/// @brief 矩形内に配置して描く（水平 align、縦は中央。矩形でクリップ）。
inline void drawSpriteTextInRect(Screen& screen, const SpriteFont& font, const sgc::Rectf& rect,
                                 std::string_view text, int scale, const sgc::Colorf& tint,
                                 PixelAlign align = PixelAlign::Left)
{
	if (scale < 1) scale = 1;
	const SpriteSize sz = measureSpriteText(font, text, scale);
	float tx = rect.x();
	if (align == PixelAlign::Center) tx = rect.x() + (rect.width() - static_cast<float>(sz.w)) * 0.5f;
	else if (align == PixelAlign::Right) tx = rect.x() + (rect.width() - static_cast<float>(sz.w));
	const float ty = rect.y() + (rect.height() - static_cast<float>(sz.h)) * 0.5f;
	screen.pushClipRect(rect);
	drawSpriteText(screen, font, tx, ty, text, scale, tint);
	screen.popClipRect();
}

} // namespace mitiru::render::pixel
