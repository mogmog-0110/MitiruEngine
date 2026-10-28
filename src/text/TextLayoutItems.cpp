// 文字列を Item に分け、禁則に沿って行に割る組版前半の実装。

#include "TextLayoutItems.hpp"

#include <algorithm>

#include <mitiru/i18n/LineBreak.hpp>
#include <mitiru/i18n/Utf8Text.hpp>
#include <mitiru/text/RubyMarkup.hpp>
#include <mitiru/text/VerticalOrientation.hpp>

namespace mitiru::text::detail
{

namespace
{

constexpr float kFitTolerance = 0.01f;

struct Cluster
{
	std::size_t begin = 0;
	std::size_t end = 0;
	std::uint32_t cp = 0;
	VerticalOrientation orientation = VerticalOrientation::Upright;
	bool enclosed = false;
};

[[nodiscard]] bool isAsciiDigit(std::uint32_t cp) noexcept { return cp >= '0' && cp <= '9'; }
[[nodiscard]] bool isAsciiLetter(std::uint32_t cp) noexcept { return (cp | 0x20u) >= 'a' && (cp | 0x20u) <= 'z'; }
[[nodiscard]] bool isAsciiAlnum(std::uint32_t cp) noexcept { return isAsciiDigit(cp) || isAsciiLetter(cp); }
[[nodiscard]] bool isExclaim(std::uint32_t cp) noexcept { return cp == '!' || cp == '?'; }

/// 向きは結合文字を含む塊ごとに決める。\r は読み飛ばす。
void scanClusters(std::string_view text, std::vector<Cluster>& out)
{
	out.clear();
	for (std::size_t i = 0; i < text.size();)
	{
		int len = 1;
		Cluster c;
		c.begin = i;
		c.cp = i18n::decodeAt(text, i, len);
		i += static_cast<std::size_t>(len);
		if (c.cp == '\r') { continue; }
		while (i < text.size() && c.cp != '\n')
		{
			const std::uint32_t next = i18n::decodeAt(text, i, len);
			if (!continuesCluster(next)) { break; }
			c.enclosed = c.enclosed || isEnclosingMark(next);
			i += static_cast<std::size_t>(len);
		}
		c.end = i;
		c.orientation = c.enclosed ? VerticalOrientation::Upright : verticalOrientation(c.cp);
		out.push_back(c);
	}
}

class Builder
{
public:
	Builder(FontFace& face, const LayoutOptions& options, std::vector<Cluster>& clusters)
		: m_face(face), m_options(options), m_clusters(clusters) {}

	void build(std::string_view text, ItemSet& set)
	{
		static thread_local std::vector<MarkupSegment> segments;
		parseRubyMarkup(text, segments);
		for (const MarkupSegment& seg : segments)
		{
			if (seg.ruby.empty()) { plain(seg.text, m_options.fontSize, set.items); }
			else { rubyGroup(seg, set); }
		}
	}

private:
	[[nodiscard]] float verticalAdvance(std::string_view s, float size)
	{
		float sum = 0.0f;
		for (const ShapedGlyph& g : m_face.shape(s, size, TextDirection::TopToBottom)) { sum += g.advanceY; }
		return sum > 0.0f ? sum : m_face.metrics(size).em;
	}

	[[nodiscard]] float horizontalAdvance(std::string_view s, float size) { return m_face.measureText(s, size); }

	[[nodiscard]] bool hasVerticalForm(std::string_view s, float size)
	{
		const auto vertical = m_face.shape(s, size, TextDirection::TopToBottom);
		const std::uint32_t verticalId = vertical.empty() ? 0 : vertical.front().glyphId;
		const auto horizontal = m_face.shape(s, size);
		return !horizontal.empty() && horizontal.front().glyphId != verticalId;
	}

	void plain(std::string_view text, float size, std::vector<Item>& out)
	{
		scanClusters(text, m_clusters);
		if (isVertical(m_options.writing)) { plainVertical(text, size, out); }
		else { plainHorizontal(text, size, out); }
	}

	[[nodiscard]] static Item newline()
	{
		Item it;
		it.kind = Item::Kind::Newline;
		return it;
	}

	[[nodiscard]] bool isCombinePair(std::size_t i) const
	{
		const auto single = [&](std::size_t k) { return m_clusters[k].end - m_clusters[k].begin == 1; };
		if (i + 1 >= m_clusters.size() || !single(i) || !single(i + 1)) { return false; }
		const std::uint32_t a = m_clusters[i].cp;
		const std::uint32_t b = m_clusters[i + 1].cp;
		const auto cpAt = [&](std::ptrdiff_t k) -> std::uint32_t {
			return (k < 0 || k >= static_cast<std::ptrdiff_t>(m_clusters.size())) ? 0u : m_clusters[static_cast<std::size_t>(k)].cp;
		};
		const auto ii = static_cast<std::ptrdiff_t>(i);
		if (isAsciiDigit(a) && isAsciiDigit(b)) { return !isAsciiAlnum(cpAt(ii - 1)) && !isAsciiAlnum(cpAt(ii + 2)); }
		if (isExclaim(a) && isExclaim(b)) { return !isExclaim(cpAt(ii - 1)) && !isExclaim(cpAt(ii + 2)); }
		return false;
	}

	/// 既定の向きを書体と前後の文字で補正する。Tr は縦組みの字形が無いときに寝かせ、1 桁の数字は立てる。
	void resolveOrientation(std::string_view text, float size)
	{
		for (std::size_t i = 0; i < m_clusters.size(); ++i)
		{
			Cluster& c = m_clusters[i];
			if (c.orientation == VerticalOrientation::TransformRotated)
			{
				c.orientation = hasVerticalForm(text.substr(c.begin, c.end - c.begin), size)
					? VerticalOrientation::Upright : VerticalOrientation::Rotated;
			}
			else if (c.orientation == VerticalOrientation::TransformUpright)
			{
				c.orientation = VerticalOrientation::Upright;
			}
			if (m_options.tateChuYoko && isAsciiDigit(c.cp) && c.end - c.begin == 1)
			{
				const std::uint32_t before = i == 0 ? 0u : m_clusters[i - 1].cp;
				const std::uint32_t after = i + 1 < m_clusters.size() ? m_clusters[i + 1].cp : 0u;
				if (!isAsciiAlnum(before) && !isAsciiAlnum(after)) { c.orientation = VerticalOrientation::Upright; }
			}
		}
	}

	[[nodiscard]] Item makeCell(std::string_view text, std::uint32_t cp, float size, bool vertical)
	{
		Item it;
		it.kind = Item::Kind::Cell;
		it.text = text;
		it.firstCp = it.lastCp = cp;
		it.length = vertical ? verticalAdvance(text, size) : horizontalAdvance(text, size);
		return it;
	}

	[[nodiscard]] Item makeRun(std::string_view text, std::uint32_t firstCp, std::uint32_t lastCp, float size)
	{
		Item it;
		it.kind = Item::Kind::Run;
		it.text = text;
		it.firstCp = firstCp;
		it.lastCp = lastCp;
		it.length = horizontalAdvance(text, size);
		return it;
	}

	[[nodiscard]] Item makeCombined(std::string_view text, float size)
	{
		Item it;
		it.kind = Item::Kind::Combined;
		it.text = text;
		it.firstCp = it.lastCp = static_cast<unsigned char>(text.front());
		const float em = m_face.metrics(size).em;
		const float advance = horizontalAdvance(text, size);
		it.shrink = advance > em ? em / advance : 1.0f;
		it.length = em;
		return it;
	}

	[[nodiscard]] std::size_t sidewaysWordEnd(std::size_t i) const
	{
		std::size_t j = i;
		const auto joinable = [&](std::size_t k) {
			return k < m_clusters.size() && m_clusters[k].orientation == VerticalOrientation::Rotated
			    && m_clusters[k].cp != '\n' && !(m_options.tateChuYoko && isCombinePair(k));
		};
		while (joinable(j) && m_clusters[j].cp != ' ') { ++j; }
		while (joinable(j) && m_clusters[j].cp == ' ') { ++j; }
		return j == i ? i + 1 : j;
	}

	void plainVertical(std::string_view text, float size, std::vector<Item>& out)
	{
		resolveOrientation(text, size);
		for (std::size_t i = 0; i < m_clusters.size();)
		{
			const Cluster& c = m_clusters[i];
			if (c.cp == '\n') { out.push_back(newline()); ++i; continue; }
			if (m_options.tateChuYoko && isCombinePair(i))
			{
				out.push_back(makeCombined(text.substr(c.begin, m_clusters[i + 1].end - c.begin), size));
				i += 2;
				continue;
			}
			if (c.orientation == VerticalOrientation::Rotated && m_options.rotateSideways)
			{
				const std::size_t j = sidewaysWordEnd(i);
				out.push_back(makeRun(text.substr(c.begin, m_clusters[j - 1].end - c.begin), c.cp, m_clusters[j - 1].cp, size));
				i = j;
				continue;
			}
			out.push_back(makeCell(text.substr(c.begin, c.end - c.begin), c.cp, size, true));
			++i;
		}
	}

	void plainHorizontal(std::string_view text, float size, std::vector<Item>& out)
	{
		const auto latin = [&](std::size_t k) {
			return k < m_clusters.size() && m_clusters[k].cp != '\n' && !i18n::KinsokuRules::isCjk(m_clusters[k].cp);
		};
		for (std::size_t i = 0; i < m_clusters.size();)
		{
			const Cluster& c = m_clusters[i];
			if (c.cp == '\n') { out.push_back(newline()); ++i; continue; }
			if (!latin(i))
			{
				out.push_back(makeCell(text.substr(c.begin, c.end - c.begin), c.cp, size, false));
				++i;
				continue;
			}
			std::size_t j = i;
			while (latin(j) && m_clusters[j].cp != ' ') { ++j; }
			while (latin(j) && m_clusters[j].cp == ' ') { ++j; }
			j = std::max(j, i + 1);
			out.push_back(makeRun(text.substr(c.begin, m_clusters[j - 1].end - c.begin), c.cp, m_clusters[j - 1].cp, size));
			i = j;
		}
	}

	void rubyGroup(const MarkupSegment& seg, ItemSet& set)
	{
		Item it;
		it.kind = Item::Kind::Ruby;
		it.text = seg.text;
		it.ruby = seg.ruby;
		it.baseBegin = static_cast<std::uint32_t>(set.baseItems.size());
		plain(seg.text, m_options.fontSize, set.baseItems);
		it.baseCount = static_cast<std::uint32_t>(set.baseItems.size()) - it.baseBegin;

		const float rubySize = m_options.fontSize * m_options.rubyScale;
		it.rubyBegin = static_cast<std::uint32_t>(set.rubyCells.size());
		scanClusters(seg.ruby, m_clusters);
		for (const Cluster& c : m_clusters)
		{
			set.rubyCells.push_back(makeCell(seg.ruby.substr(c.begin, c.end - c.begin), c.cp, rubySize,
			                                 isVertical(m_options.writing)));
		}
		it.rubyCount = static_cast<std::uint32_t>(set.rubyCells.size()) - it.rubyBegin;

		for (std::uint32_t k = 0; k < it.baseCount; ++k) { it.baseLength += set.baseItems[it.baseBegin + k].length; }
		for (std::uint32_t k = 0; k < it.rubyCount; ++k) { it.rubyLength += set.rubyCells[it.rubyBegin + k].length; }
		it.length = std::max(it.baseLength, it.rubyLength);
		if (it.baseCount > 0)
		{
			it.firstCp = set.baseItems[it.baseBegin].firstCp;
			it.lastCp = set.baseItems[it.baseBegin + it.baseCount - 1].lastCp;
		}
		set.hasRuby = true;
		set.items.push_back(it);
	}

	FontFace& m_face;
	const LayoutOptions& m_options;
	std::vector<Cluster>& m_clusters;
};

[[nodiscard]] bool startsBreakable(const Item& it)
{
	return it.kind == Item::Kind::Cell || it.kind == Item::Kind::Ruby;
}

[[nodiscard]] bool prohibitedAtStart(const Item& it)
{
	return startsBreakable(it) && i18n::KinsokuRules::isLineStartProhibited(it.firstCp);
}

[[nodiscard]] bool prohibitedAtEnd(const Item& it)
{
	return startsBreakable(it) && i18n::KinsokuRules::isLineEndProhibited(it.lastCp);
}

/// 句読点は行の外へぶら下げてよい。ほかの行頭禁止の字は、前の字と一緒に次の行へ送る。
[[nodiscard]] bool mayHang(const Item& it)
{
	if (it.kind != Item::Kind::Cell) { return false; }
	return it.firstCp == 0x3001 || it.firstCp == 0x3002 || it.firstCp == 0xFF0C || it.firstCp == 0xFF0E;
}

} // namespace

void buildItems(FontFace& face, std::string_view text, const LayoutOptions& options, ItemSet& out)
{
	out.clear();
	// 毎フレーム呼ぶので、塊の作業領域は呼び出しをまたいで使い回す
	static thread_local std::vector<Cluster> clusters;
	Builder(face, options, clusters).build(text, out);
}

void breakLines(const ItemSet& set, float extent, std::vector<LineRange>& lines)
{
	lines.clear();
	const auto& items = set.items;
	const bool wrap = extent > 0.0f;
	std::size_t lineStart = 0;
	float used = 0.0f;
	const auto closeLine = [&](std::size_t end) {
		lines.push_back(LineRange{lineStart, end - lineStart});
		lineStart = end;
		used = 0.0f;
	};
	for (std::size_t k = 0; k < items.size();)
	{
		if (items[k].kind == Item::Kind::Newline)
		{
			closeLine(k);
			lineStart = ++k;
			continue;
		}
		const float len = items[k].length;
		if (!wrap || k == lineStart || used + len <= extent + kFitTolerance)
		{
			used += len;
			++k;
			continue;
		}
		if (mayHang(items[k]))
		{
			++k;
			closeLine(k);
			continue;
		}
		std::size_t b = k;
		while (b > lineStart + 1 && prohibitedAtStart(items[b])) { --b; }
		while (b > lineStart + 1 && prohibitedAtEnd(items[b - 1])) { --b; }
		closeLine(b);
		for (std::size_t m = b; m < k; ++m) { used += items[m].length; }
	}
	if (lineStart < items.size() || lines.empty())
	{
		lines.push_back(LineRange{lineStart, items.size() - lineStart});
	}
}

void distribute(const std::vector<float>& lengths, float total, std::vector<float>& starts)
{
	starts.clear();
	if (lengths.empty()) { return; }
	float sum = 0.0f;
	for (const float l : lengths) { sum += l; }
	const float pad = std::max(0.0f, total - sum) / static_cast<float>(lengths.size());
	float prefix = 0.0f;
	for (std::size_t i = 0; i < lengths.size(); ++i)
	{
		starts.push_back(prefix + pad * (static_cast<float>(i) + 0.5f));
		prefix += lengths[i];
	}
}

} // namespace mitiru::text::detail
