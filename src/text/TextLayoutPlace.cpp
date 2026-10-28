// 組版の後半。行に分けた Item を配置し、字形の並びを作る。

#include "TextLayoutItems.hpp"

#include <algorithm>
#include <cmath>

namespace mitiru::text::detail
{

namespace
{

constexpr float kFitTolerance = 0.01f;

class Placer
{
public:
	/// ルビの割り付けの作業領域。毎フレーム確保し直さないよう、呼び出しをまたいで使い回す
	struct Scratch
	{
		std::vector<float> lengths;
		std::vector<float> baseStarts;
		std::vector<float> rubyStarts;
	};

	Placer(FontFace& face, const LayoutOptions& options, const ItemSet& set, TextLayout& out, Scratch& scratch)
		: m_face(face), m_options(options), m_set(set), m_out(out), m_scratch(scratch), m_vertical(isVertical(options.writing))
	{
		const FontMetrics m = face.metrics(options.fontSize);
		m_em = m.em;
		m_rubySize = options.fontSize * options.rubyScale;
		m_rubyEm = face.metrics(m_rubySize).em;
		const float gapRatio = set.hasRuby ? std::max(options.lineGap, options.rubyScale) : options.lineGap;
		m_pitch = m_em * (1.0f + gapRatio);
		// ルビを置く側に、最初の行の外側の空きを取る。縦書きは右、横書きは上。
		m_rubyReserve = set.hasRuby ? m_rubyEm : 0.0f;
	}

	void place(const std::vector<LineRange>& lines)
	{
		const std::size_t fit = fitLines(lines.size());
		m_out.truncated = fit < lines.size();
		m_out.lineCount = static_cast<int>(fit);
		m_out.em = m_em;
		const float content = fit == 0 ? 0.0f : static_cast<float>(fit - 1) * m_pitch + m_em + m_rubyReserve;
		const float cross = m_options.crossExtent > 0.0f ? m_options.crossExtent : content;
		float longest = 0.0f;
		for (std::size_t n = 0; n < fit; ++n)
		{
			longest = std::max(longest, placeLine(lines[n], n, cross));
		}
		m_out.width = m_vertical ? cross : longest;
		m_out.height = m_vertical ? longest : cross;
	}

private:
	/// 入る行数。親文字とルビの置き場を合わせた 1 行分の幅に満たなければ、何も置かず 0 を返す。
	[[nodiscard]] std::size_t fitLines(std::size_t total) const
	{
		if (m_options.crossExtent <= 0.0f) { return total; }
		const float room = m_options.crossExtent - (m_em + m_rubyReserve) + kFitTolerance;
		if (room < 0.0f) { return 0; }
		return std::min(total, static_cast<std::size_t>(std::floor(room / m_pitch)) + 1);
	}

	[[nodiscard]] float centerOffset(float size) const
	{
		const FontMetrics m = m_face.metrics(size);
		return (m.ascent + m.descent) * 0.5f;
	}

	/// 行の基準位置。縦書きは親文字の列の中心の x、横書きは親文字のベースラインの y。
	/// 縦書きの Rl は右端、Lr は左端、横書きは上端に寄せる。
	[[nodiscard]] float lineAnchor(std::size_t line, float cross) const
	{
		const float offset = static_cast<float>(line) * m_pitch;
		if (!m_vertical) { return m_rubyReserve + m_em * 0.5f + centerOffset(m_options.fontSize) + offset; }
		if (m_options.writing == Writing::VerticalRl) { return cross - m_rubyReserve - m_em * 0.5f - offset; }
		return m_em * 0.5f + offset;
	}

	float placeLine(const LineRange& range, std::size_t line, float cross)
	{
		const float anchor = lineAnchor(line, cross);
		float along = 0.0f;
		for (std::size_t k = 0; k < range.count; ++k)
		{
			const Item& item = m_set.items[range.first + k];
			placeItem(item, m_options.fontSize, anchor, along);
			along += item.length;
		}
		return along;
	}

	void placeItem(const Item& item, float size, float anchor, float along)
	{
		switch (item.kind)
		{
		case Item::Kind::Cell: placeCell(item, size, anchor, along); break;
		case Item::Kind::Run: placeRun(item, size, anchor, along); break;
		case Item::Kind::Combined: placeCombined(item, size, anchor, along); break;
		case Item::Kind::Ruby: placeRuby(item, size, anchor, along); break;
		case Item::Kind::Newline: break;
		}
	}

	void pushGlyph(float size, const PlacedGlyph& g)
	{
		auto& runs = m_out.runs;
		const auto index = static_cast<std::uint32_t>(m_out.glyphs.size());
		if (runs.empty() || runs.back().rotated || runs.back().pixelSize != size ||
		    runs.back().first + runs.back().count != index)
		{
			GlyphRun run;
			run.pixelSize = size;
			run.first = index;
			runs.push_back(run);
		}
		m_out.glyphs.push_back(g);
		++runs.back().count;
	}

	void addCell(LayoutCell::Kind kind, float x, float y, float w, float h)
	{
		m_out.cells.push_back(LayoutCell{kind, x, y, w, h});
	}

	/// 立てた 1 字。縦書きは縦組みの字形を列の中心に、横書きは横組みの字形を行の上に置く。
	void placeCell(const Item& item, float size, float anchor, float along)
	{
		const float em = m_face.metrics(size).em;
		float pen = 0.0f;
		if (m_vertical)
		{
			for (const ShapedGlyph& g : m_face.shape(item.text, size, TextDirection::TopToBottom))
			{
				pushGlyph(size, {g.glyphId, anchor + g.offsetX, along + pen + g.offsetY});
				pen += g.advanceY;
			}
			addCell(LayoutCell::Kind::Upright, anchor - em * 0.5f, along, em, item.length);
			return;
		}
		for (const ShapedGlyph& g : m_face.shape(item.text, size))
		{
			pushGlyph(size, {g.glyphId, along + pen + g.offsetX, anchor + g.offsetY});
			pen += g.advanceX;
		}
		addCell(LayoutCell::Kind::Upright, along, anchor - centerOffset(size) - em * 0.5f, item.length, em);
	}

	/// 縦書きでは横向きにする語、横書きでは欧文の語。
	void placeRun(const Item& item, float size, float anchor, float along)
	{
		const auto glyphs = m_face.shape(item.text, size);
		if (!m_vertical)
		{
			float pen = 0.0f;
			for (const ShapedGlyph& g : glyphs)
			{
				pushGlyph(size, {g.glyphId, along + pen + g.offsetX, anchor + g.offsetY});
				pen += g.advanceX;
			}
			const float em = m_face.metrics(size).em;
			addCell(LayoutCell::Kind::Upright, along, anchor - centerOffset(size) - em * 0.5f, item.length, em);
			return;
		}
		// 90 度回すと、字の上が右を向く。字の枠の中心が列の中心に来るよう、原点を左へ寄せる。
		GlyphRun run;
		run.pixelSize = size;
		run.rotated = true;
		run.originX = anchor - centerOffset(size);
		run.originY = along;
		run.first = static_cast<std::uint32_t>(m_out.glyphs.size());
		float pen = 0.0f;
		for (const ShapedGlyph& g : glyphs)
		{
			m_out.glyphs.push_back({g.glyphId, pen + g.offsetX, g.offsetY});
			pen += g.advanceX;
		}
		run.count = static_cast<std::uint32_t>(m_out.glyphs.size()) - run.first;
		m_out.runs.push_back(run);
		const float em = m_face.metrics(size).em;
		addCell(LayoutCell::Kind::Sideways, anchor - em * 0.5f, along, em, item.length);
	}

	/// 縦中横。横に並べた字を 1 字分の枠の中心に置く。
	void placeCombined(const Item& item, float size, float anchor, float along)
	{
		const float em = m_face.metrics(size).em;
		const float shrunk = size * item.shrink;
		const auto glyphs = m_face.shape(item.text, shrunk);
		float width = 0.0f;
		for (const ShapedGlyph& g : glyphs) { width += g.advanceX; }
		const float baseline = along + em * 0.5f + centerOffset(shrunk);
		float pen = anchor - width * 0.5f;
		for (const ShapedGlyph& g : glyphs)
		{
			pushGlyph(shrunk, {g.glyphId, pen + g.offsetX, baseline + g.offsetY});
			pen += g.advanceX;
		}
		addCell(LayoutCell::Kind::Combined, anchor - em * 0.5f, along, em, em);
	}

	void placeRuby(const Item& item, float size, float anchor, float along)
	{
		auto& lengths = m_scratch.lengths;
		lengths.clear();
		for (std::uint32_t k = 0; k < item.baseCount; ++k) { lengths.push_back(m_set.baseItems[item.baseBegin + k].length); }
		distribute(lengths, item.length, m_scratch.baseStarts);
		for (std::uint32_t k = 0; k < item.baseCount; ++k)
		{
			placeItem(m_set.baseItems[item.baseBegin + k], size, anchor, along + m_scratch.baseStarts[k]);
		}

		lengths.clear();
		for (std::uint32_t k = 0; k < item.rubyCount; ++k) { lengths.push_back(m_set.rubyCells[item.rubyBegin + k].length); }
		distribute(lengths, item.length, m_scratch.rubyStarts);
		// 縦書きは親文字の右、横書きは上の行間にルビを置く。
		const float rubyAnchor = m_vertical
			? anchor + m_em * 0.5f + m_rubyEm * 0.5f
			: anchor - centerOffset(size) - m_em * 0.5f - m_rubyEm * 0.5f + centerOffset(m_rubySize);
		for (std::uint32_t k = 0; k < item.rubyCount; ++k)
		{
			const Item& cell = m_set.rubyCells[item.rubyBegin + k];
			placeCell(cell, m_rubySize, rubyAnchor, along + m_scratch.rubyStarts[k]);
			m_out.cells.back().kind = LayoutCell::Kind::Ruby;
		}
	}

	FontFace& m_face;
	const LayoutOptions& m_options;
	const ItemSet& m_set;
	TextLayout& m_out;
	Scratch& m_scratch;
	bool m_vertical;
	float m_em = 0.0f;
	float m_rubySize = 0.0f;
	float m_rubyEm = 0.0f;
	float m_pitch = 0.0f;
	float m_rubyReserve = 0.0f;
};

} // namespace

void placeLines(FontFace& face, const LayoutOptions& options, const ItemSet& set,
                const std::vector<LineRange>& lines, TextLayout& out)
{
	static thread_local Placer::Scratch scratch;
	Placer(face, options, set, out, scratch).place(lines);
}

} // namespace mitiru::text::detail
