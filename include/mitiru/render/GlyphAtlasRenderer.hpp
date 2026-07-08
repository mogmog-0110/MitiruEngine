#pragma once

/// @file GlyphAtlasRenderer.hpp
/// @brief 文字を 1 文字ずつテクスチャに焼いて描く文字描画器。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/core/Screen.hpp>
#include <mitiru/render/Texture.hpp>
#include <mitiru/vn/TrueTypeFont.hpp>

namespace mitiru::render
{

/// @brief 文字を 1 文字 = 1 枚のクアッドで描く。
/// @details 各文字を一度だけ共有アトラスに焼き、以降は同じ (文字, サイズ) を再利用する。
///          Engine が 1 つ所有し、既定フォントの描画コールバックから使う。
class GlyphAtlasRenderer
{
public:
	/// @brief 1 行のテキストを描画する。
	/// @param screen 描画先
	/// @param font   グリフ供給元
	/// @param text   UTF-8 テキスト
	/// @param x      左上 X
	/// @param y      左上 Y (上端)
	/// @param fontSize 表示フォントサイズ (ピクセル)
	/// @param color  文字色
	void drawString(Screen& screen, vn::TrueTypeFont& font, std::string_view text,
	                float x, float y, float fontSize, const sgc::Colorf& color)
	{
		if (!font.valid() || text.empty() || fontSize <= 0.0f) { return; }
		const float sizeKey = quantizeSize(fontSize);

		ensureGlyphs(font, text, sizeKey);
		// アトラスが縦に伸びた場合は、古いサイズを参照する未送信の文字を先に送る。
		if (m_grew) { screen.flushSpriteBatch(); m_grew = false; }
		if (m_dirty) { m_atlasTex = Texture(m_w, m_h, m_pixels); m_dirty = false; }
		if (!m_atlasTex.valid()) { return; }

		const bool swPath = screen.softwareTextPath();
		const auto metrics = font.metrics(sizeKey);
		const float baselineY = y + metrics.ascent;
		float cursorX = x;
		std::uint32_t prevCp = 0;

		vn::Utf8Iterator it(text);
		while (it.hasNext())
		{
			const std::uint32_t cp = it.next();
			if (prevCp != 0) { cursorX += font.kerning(prevCp, cp, sizeKey); }

			const Entry* e = find(cp, sizeKey);
			if (e != nullptr && e->w > 0 && e->h > 0)
			{
				const sgc::Rectf dst{
					cursorX + static_cast<float>(e->offX),
					baselineY + static_cast<float>(e->offY),
					static_cast<float>(e->w), static_cast<float>(e->h)};
				const sgc::Rectf src{
					static_cast<float>(e->x), static_cast<float>(e->y),
					static_cast<float>(e->w), static_cast<float>(e->h)};
				// グリフを 1 枚描く。
				if (swPath) { screen.blitAtlasGlyph(m_atlasTex, dst, src, color); }
				else        { screen.drawSprite(m_atlasTex, dst, src, color); }
			}

			cursorX += font.advanceWidth(cp, sizeKey);
			prevCp = cp;
		}
	}

	/// @brief テキストの描画サイズを計測する。
	[[nodiscard]] static sgc::Vec2f measure(vn::TrueTypeFont& font,
	                                        std::string_view text, float fontSize)
	{
		const float sizeKey = quantizeSize(fontSize);
		return { font.measureText(text, sizeKey), font.metrics(sizeKey).lineHeight };
	}

private:
	/// @brief アトラスキー (文字 + サイズ)。
	struct Key
	{
		std::uint32_t cp = 0;
		int size = 0;
		[[nodiscard]] bool operator==(const Key& o) const noexcept
		{
			return cp == o.cp && size == o.size;
		}
	};
	struct KeyHash
	{
		[[nodiscard]] std::size_t operator()(const Key& k) const noexcept
		{
			return (std::hash<std::uint32_t>{}(k.cp) * 1099511628211ull)
			     ^ static_cast<std::size_t>(k.size);
		}
	};
	/// @brief アトラス内のグリフ矩形とベースライン相対オフセット。
	struct Entry
	{
		int x = 0, y = 0, w = 0, h = 0;
		int offX = 0, offY = 0;
	};

	/// @brief 表示サイズを整数ピクセルに丸める。
	[[nodiscard]] static float quantizeSize(float fontSize) noexcept
	{
		return std::max(1.0f, std::round(fontSize));
	}
	[[nodiscard]] static int bucket(float sizeKey) noexcept
	{
		return static_cast<int>(std::lround(sizeKey));
	}

	[[nodiscard]] const Entry* find(std::uint32_t cp, float sizeKey) const
	{
		const auto it = m_map.find(Key{cp, bucket(sizeKey)});
		return it == m_map.end() ? nullptr : &it->second;
	}

	/// @brief テキスト中の未登録の文字をアトラスへ追加する。
	void ensureGlyphs(vn::TrueTypeFont& font, std::string_view text, float sizeKey)
	{
		vn::Utf8Iterator it(text);
		while (it.hasNext())
		{
			const std::uint32_t cp = it.next();
			const Key key{cp, bucket(sizeKey)};
			if (m_map.find(key) != m_map.end()) { continue; }
			addGlyph(font, cp, key, sizeKey);
		}
	}

	/// @brief 1 文字をラスタライズしてアトラスへ焼き、表へ登録する。
	void addGlyph(vn::TrueTypeFont& font, std::uint32_t cp, const Key& key, float sizeKey)
	{
		const vn::GlyphInfo* g = font.getGlyph(cp, sizeKey);
		Entry e;
		if (g == nullptr || g->width <= 0 || g->height <= 0 || g->bitmap.empty())
		{
			// スペース等の空きグリフ。advance だけ持つ。
			m_map.emplace(key, e);
			return;
		}

		const int gw = g->width;
		const int gh = g->height;
		if (!reserveShelfSlot(gw, gh)) { return; }

		e.x = m_shelfX;
		e.y = m_shelfY;
		e.w = gw;
		e.h = gh;
		e.offX = g->offsetX;
		e.offY = g->offsetY;
		blitCoverage(*g, e.x, e.y);

		m_shelfX += gw + kPad;
		m_shelfH = std::max(m_shelfH, gh);
		m_map.emplace(key, e);
		m_dirty = true;
	}

	/// @brief (gw,gh) の置き場所を確保する。足りなければ縦に伸ばす。
	[[nodiscard]] bool reserveShelfSlot(int gw, int gh)
	{
		ensureAtlasInited();

		if (m_shelfX + gw + kPad > m_w)
		{
			m_shelfX = kPad;
			m_shelfY += m_shelfH + kPad;
			m_shelfH = 0;
		}

		int guard = 0;
		while (m_shelfY + gh + kPad > m_h)
		{
			if (m_h < kMaxH)
			{
				m_pixels.resize(static_cast<std::size_t>(m_w) * (m_h * 2) * 4, 0);
				m_h *= 2;
				m_grew = true;
			}
			else
			{
				resetAtlas();
				m_grew = true;
			}
			if (++guard > 32) { return false; }
		}
		return true;
	}

	void ensureAtlasInited()
	{
		if (m_w != 0) { return; }
		m_w = kInitW;
		m_h = kInitH;
		m_pixels.assign(static_cast<std::size_t>(m_w) * m_h * 4, 0);
		m_shelfX = kPad;
		m_shelfY = kPad;
		m_shelfH = 0;
	}

	void resetAtlas()
	{
		m_map.clear();
		m_pixels.assign(static_cast<std::size_t>(m_w) * m_h * 4, 0);
		m_shelfX = kPad;
		m_shelfY = kPad;
		m_shelfH = 0;
	}

	/// @brief グリフの濃さをアトラス (白 + アルファ) へ書く。
	void blitCoverage(const vn::GlyphInfo& g, int dstX, int dstY)
	{
		for (int row = 0; row < g.height; ++row)
		{
			const std::size_t dstRow =
				(static_cast<std::size_t>(dstY + row) * m_w + dstX) * 4;
			const std::size_t srcRow = static_cast<std::size_t>(row) * g.width;
			for (int col = 0; col < g.width; ++col)
			{
				const std::size_t d = dstRow + static_cast<std::size_t>(col) * 4;
				const std::uint8_t a = g.bitmap[srcRow + col];
				m_pixels[d + 0] = 255;
				m_pixels[d + 1] = 255;
				m_pixels[d + 2] = 255;
				m_pixels[d + 3] = a;
			}
		}
	}

	static constexpr int kInitW = 1024;
	static constexpr int kInitH = 512;
	static constexpr int kMaxH  = 8192;
	static constexpr int kPad   = 1;

	int m_w = 0, m_h = 0;
	int m_shelfX = 0, m_shelfY = 0, m_shelfH = 0;
	bool m_dirty = false;
	bool m_grew = false;
	std::vector<std::uint8_t> m_pixels;
	Texture m_atlasTex;
	std::unordered_map<Key, Entry, KeyHash> m_map;
};

} // namespace mitiru::render
