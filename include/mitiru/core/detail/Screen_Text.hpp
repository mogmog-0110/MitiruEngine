#pragma once
// Detail header for mitiru::Screen — do not include directly; included via core/Screen.hpp

inline void mitiru::Screen::setTrueTypeFont(void* font, TtDrawFunc drawFn, TtMeasureFunc measureFn) noexcept
{
	m_ttFont = font;
	m_ttDrawFunc = drawFn;
	m_ttMeasureFunc = measureFn;
}

inline void mitiru::Screen::setSdfFont(void* font, SdfDrawFunc drawFn, SdfMeasureFunc measureFn) noexcept
{
	m_sdfFont = font;
	m_sdfDrawFunc = drawFn;
	m_sdfMeasureFunc = measureFn;
}

inline void mitiru::Screen::clearSdfFont() noexcept
{
	m_sdfFont = nullptr;
	m_sdfDrawFunc = nullptr;
	m_sdfMeasureFunc = nullptr;
}

inline void mitiru::Screen::registerFont(const std::string& name, void* font,
                                          TtDrawFunc drawFn, TtMeasureFunc measureFn)
{
	m_fontRegistry[name] = FontEntry{font, drawFn, measureFn};
}

inline void mitiru::Screen::setFont(const std::string& name)
{
	if (name.empty())
	{
		m_activeFont = nullptr;
		return;
	}
	const auto it = m_fontRegistry.find(name);
	if (it != m_fontRegistry.end())
	{
		m_activeFont = &it->second;
	}
}

inline void mitiru::Screen::drawText(const sgc::Vec2f& position, std::string_view text,
                                      const sgc::Colorf& color, float fontSize)
{
	{
		const auto sz = measureText(text, fontSize);
		validateTextDraw(position, sz.x, sz.y, "drawText", color);
	}
	if (m_activeFont && m_activeFont->font && m_activeFont->drawFn)
	{
		m_activeFont->drawFn(m_activeFont->font, *this, position.x, position.y, text, fontSize, color);
		++m_drawCallCount;
		return;
	}
	if (m_sdfFont && m_sdfDrawFunc)
	{
		m_sdfDrawFunc(m_sdfFont, *this, position.x, position.y, text, fontSize, color);
		++m_drawCallCount;
		return;
	}
	if (m_ttFont && m_ttDrawFunc)
	{
		m_ttDrawFunc(m_ttFont, *this, position.x, position.y, text, fontSize, color);
		++m_drawCallCount;
		return;
	}
	const float scale = fontSize / static_cast<float>(render::BitmapFont::GLYPH_HEIGHT);
	render::TextRenderer::drawTextFloat(*this, text,
	                                    position.x, position.y,
	                                    scale, color);
}

inline void mitiru::Screen::drawTextClipped(const sgc::Rectf& rect, std::string_view text,
                                             const sgc::Colorf& color, float fontSize,
                                             float padX, float padY)
{
	{
		const auto tw = measureText(text, fontSize).x;
		validateTextOverflow(rect, rect.width() - padX * 2.0f, tw, "drawTextClipped");
		validateColor(color, "drawTextClipped", rect);
	}
	const float maxW = rect.width() - padX * 2.0f;
	if (maxW <= 0.0f) return;

	const auto size = measureText(text, fontSize);
	if (size.x <= maxW)
	{
		drawText({rect.x() + padX, rect.y() + padY}, text, color, fontSize);
		return;
	}

	const auto ellipsisW = measureText("...", fontSize).x;
	if (maxW <= ellipsisW)
	{
		drawText({rect.x() + padX, rect.y() + padY}, ".", color, fontSize);
		return;
	}

	const float availW = maxW - ellipsisW;
	std::size_t lo = 0, hi = text.size();
	while (lo < hi)
	{
		const auto mid = (lo + hi + 1) / 2;
		if (measureText(text.substr(0, mid), fontSize).x <= availW)
			lo = mid;
		else
			hi = mid - 1;
	}

	const std::string truncated = std::string(text.substr(0, lo)) + "...";
	drawText({rect.x() + padX, rect.y() + padY}, truncated, color, fontSize);
}

inline void mitiru::Screen::drawTextInRect(const sgc::Rectf& rect, std::string_view text,
                                            const sgc::Colorf& color, float fontSize,
                                            TextAlignH alignH,
                                            TextAlignV alignV,
                                            float padX, float padY)
{
	{
		validateColor(color, "drawTextInRect", rect);
	}
	const float innerW = rect.width() - padX * 2.0f;
	const float innerH = rect.height() - padY * 2.0f;
	if (innerW <= 0.0f || innerH <= 0.0f) return;

	// CRITICAL: do NOT proportional-shrink fontSize when text overflows.
	// Reducing 16 → 11.3 lands on non-atlas fractions (atlas is 32px so
	// only 32 / 24 / 16 / 12 / 8 render crisply) — the result is smudgy,
	// unreadable text in narrow windows. Instead, keep fontSize and let
	// the text get truncated with an ellipsis (mirrors drawTextClipped
	// semantics). This keeps SDF rendering on atlas-aligned fractions
	// and preserves legibility at any window width.
	const auto fullSize = measureText(text, fontSize);
	std::string_view drawable = text;
	std::string ellipsisBuf;  // owns truncated form when ellipsis applied
	sgc::Vec2f size = fullSize;
	if (fullSize.x > innerW && !text.empty())
	{
		const auto ellipsisW = measureText("...", fontSize).x;
		if (innerW > ellipsisW)
		{
			const float availW = innerW - ellipsisW;
			std::size_t lo = 0, hi = text.size();
			while (lo < hi)
			{
				const auto mid = (lo + hi + 1) / 2;
				if (measureText(text.substr(0, mid), fontSize).x <= availW)
					lo = mid;
				else
					hi = mid - 1;
			}
			ellipsisBuf.assign(text.substr(0, lo));
			ellipsisBuf += "...";
			drawable = ellipsisBuf;
			size = measureText(drawable, fontSize);
		}
		else
		{
			// Rect too small even for "..." — drop to single "."
			ellipsisBuf = ".";
			drawable = ellipsisBuf;
			size = measureText(drawable, fontSize);
		}
	}

	float x = rect.x() + padX;
	if (alignH == TextAlignH::Center) x = rect.x() + (rect.width() - size.x) * 0.5f;
	else if (alignH == TextAlignH::Right) x = rect.x() + rect.width() - padX - size.x;

	float y = rect.y() + padY;
	if (alignV == TextAlignV::Middle) y = rect.y() + (rect.height() - size.y) * 0.5f;
	else if (alignV == TextAlignV::Bottom) y = rect.y() + rect.height() - padY - size.y;

	drawText({x, y}, drawable, color, fontSize);
}

inline void mitiru::Screen::drawTextWrapped(const sgc::Rectf& rect, std::string_view text,
                                             const sgc::Colorf& color, float fontSize,
                                             float padX, float padY,
                                             float lineSpacing)
{
	validateDrawCall(rect, "drawTextWrapped");
	validateColor(color, "drawTextWrapped", rect);
	const float innerW = rect.width() - padX * 2.0f;
	const float innerH = rect.height() - padY * 2.0f;
	if (innerW <= 0.0f || innerH <= 0.0f) return;

	const auto spaceSize = measureText(" ", fontSize);
	const float charW = spaceSize.x;
	const float lineH = spaceSize.y * lineSpacing;
	float curY = rect.y() + padY;

	std::size_t pos = 0;
	while (pos < text.size() && curY + lineH <= rect.y() + rect.height())
	{
		std::size_t lineEnd = pos;
		std::size_t lastSpace = pos;
		float lineWidth = 0.0f;

		while (lineEnd < text.size() && text[lineEnd] != '\n')
		{
			if (text[lineEnd] == ' ') lastSpace = lineEnd;
			const float glyphW =
				(m_sdfFont && m_sdfMeasureFunc) || (m_ttFont && m_ttMeasureFunc)
					? measureText(text.substr(lineEnd, 1), fontSize).x
					: charW;
			lineWidth += glyphW;
			if (lineWidth > innerW)
			{
				if (lastSpace > pos)
				{
					lineEnd = lastSpace;
				}
				break;
			}
			++lineEnd;
		}

		if (lineEnd == pos && lineEnd < text.size())
		{
			++lineEnd;
		}

		const auto lineText = text.substr(pos, lineEnd - pos);
		drawText({rect.x() + padX, curY}, lineText, color, fontSize);
		curY += lineH;

		pos = lineEnd;
		if (pos < text.size() && (text[pos] == ' ' || text[pos] == '\n'))
		{
			++pos;
		}
	}
}

inline void mitiru::Screen::drawTextHQ(const sgc::Vec2f& position, std::string_view text,
                                        const sgc::Colorf& color, float fontSize)
{
	{
		const float tw = static_cast<float>(text.size()) * fontSize;
		const float th = fontSize;
		validateTextDraw(position, tw, th, "drawTextHQ", color);
	}
	const int scale = std::max(1, static_cast<int>(fontSize) / 8);
	const int cellW = 8 * scale;
	const int cellH = 8 * scale;
	float x = position.x;
	static_cast<void>(cellH);

	for (char ch : text)
	{
		if (ch < 32 || ch > 126)
		{
			x += static_cast<float>(cellW);
			continue;
		}

		const auto glyph = render::BitmapFont::glyph(ch);
		for (int gy = 0; gy < 8; ++gy)
		{
			for (int gx = 0; gx < 8; ++gx)
			{
				if (glyph[static_cast<std::size_t>(gy)] & (0x80 >> gx))
				{
					emitRect(
						sgc::Rectf{
							x + static_cast<float>(gx * scale),
							position.y + static_cast<float>(gy * scale),
							static_cast<float>(scale),
							static_cast<float>(scale)},
						color);
				}
			}
		}
		x += static_cast<float>(cellW);
	}
	++m_drawCallCount;
}

inline void mitiru::Screen::drawTextWithShadow(const sgc::Rectf& rect, std::string_view text,
                                                const sgc::Colorf& color, float fontSize,
                                                const sgc::Colorf& shadowColor,
                                                float shadowOffsetX, float shadowOffsetY,
                                                TextAlignH alignH,
                                                TextAlignV alignV)
{
	const sgc::Rectf shadowRect{rect.x() + shadowOffsetX, rect.y() + shadowOffsetY,
	                             rect.width(), rect.height()};
	drawTextInRect(shadowRect, text, shadowColor, fontSize, alignH, alignV);
	drawTextInRect(rect, text, color, fontSize, alignH, alignV);
}

inline void mitiru::Screen::drawTextOutlined(const sgc::Rectf& rect, std::string_view text,
                                              const sgc::Colorf& color, const sgc::Colorf& outlineColor,
                                              float outlineWidth, float fontSize,
                                              TextAlignH alignH,
                                              TextAlignV alignV)
{
	const float offsets[][2] = {
		{-outlineWidth, 0}, {outlineWidth, 0}, {0, -outlineWidth}, {0, outlineWidth},
		{-outlineWidth, -outlineWidth}, {outlineWidth, -outlineWidth},
		{-outlineWidth, outlineWidth}, {outlineWidth, outlineWidth}
	};
	for (const auto& off : offsets)
	{
		drawTextInRect({rect.x() + off[0], rect.y() + off[1], rect.width(), rect.height()},
		               text, outlineColor, fontSize, alignH, alignV);
	}
	drawTextInRect(rect, text, color, fontSize, alignH, alignV);
}

inline void mitiru::Screen::drawTextStrikethrough(const sgc::Rectf& rect, std::string_view text,
                                                   const sgc::Colorf& color, float fontSize,
                                                   TextAlignH alignH,
                                                   TextAlignV alignV)
{
	drawTextInRect(rect, text, color, fontSize, alignH, alignV);
	const auto textSize = measureText(text, fontSize);
	float textX = rect.x() + 4.0f;
	if (alignH == TextAlignH::Center) textX = rect.x() + (rect.width() - textSize.x) * 0.5f;
	else if (alignH == TextAlignH::Right) textX = rect.x() + rect.width() - 4.0f - textSize.x;
	const float lineY = rect.y() + rect.height() * 0.5f;
	drawLine({textX, lineY}, {textX + textSize.x, lineY}, color, 1.5f);
}

inline void mitiru::Screen::drawTextBold(const sgc::Rectf& rect, std::string_view text,
                                          const sgc::Colorf& color, float fontSize,
                                          TextAlignH alignH,
                                          TextAlignV alignV)
{
	drawTextInRect(rect, text, color, fontSize, alignH, alignV);
	drawTextInRect({rect.x() + 1.0f, rect.y(), rect.width(), rect.height()},
	               text, color, fontSize, alignH, alignV);
}

inline void mitiru::Screen::drawTextSpaced(const sgc::Vec2f& position, std::string_view text,
                                            const sgc::Colorf& color, float fontSize,
                                            float letterSpacing)
{
	if ((m_activeFont && m_activeFont->font && m_activeFont->drawFn) ||
	    (m_sdfFont && m_sdfDrawFunc) ||
	    (m_ttFont && m_ttDrawFunc))
	{
		float cursorX = position.x;
		for (std::size_t i = 0; i < text.size(); ++i)
		{
			const auto ch = text.substr(i, 1);
			drawText({cursorX, position.y}, ch, color, fontSize);
			const auto charW = measureText(ch, fontSize).x;
			cursorX += charW + letterSpacing;
		}
		return;
	}
	const float scale = fontSize / static_cast<float>(render::BitmapFont::GLYPH_HEIGHT);
	render::TextRenderer::drawTextFloat(*this, text,
	                                    position.x, position.y,
	                                    scale, color, letterSpacing);
}
