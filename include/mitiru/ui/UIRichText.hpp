#pragma once
/// @file UIRichText.hpp
/// @brief リッチテキストパーサー・計測・描画システム

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <sgc/math/Rect.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>

namespace mitiru::ui
{

/// @brief リッチテキストの1区間
struct RichTextSegment
{
	std::string text;
	sgc::Colorf color{1.0f, 1.0f, 1.0f, 1.0f};
	bool bold = false;
	bool italic = false;
	float fontSize = 16.0f;
	std::string link;   ///< リンクURL（空なら非リンク）
	std::string emoji;  ///< 絵文字名（空なら通常テキスト）
};

/// @brief リッチテキストの計測結果
struct RichTextMeasurement
{
	float width = 0.0f;
	float height = 0.0f;
	int lineCount = 0;
};

/// @brief リッチテキストパーサー・計測・描画
/// @code
///   UIRichText rt;
///   auto segs = rt.parse("<b>Hello</b> <color=#FF0000>World</color>");
///   auto m = rt.measure(segs, 400.0f);
///   rt.render(screen, segs, {10, 10, 400, m.height});
/// @endcode
class UIRichText
{
	float m_defaultFontSize = 16.0f;
	sgc::Colorf m_defaultColor{1.0f, 1.0f, 1.0f, 1.0f};
	static float charWidth(float fs) noexcept { return fs * 0.5f; }
	static float lineH(float fs) noexcept { return fs * 1.2f; }

	struct StyleFrame
	{
		sgc::Colorf color{1.0f, 1.0f, 1.0f, 1.0f};
		float fontSize = 16.0f;
		bool bold = false;
		bool italic = false;
		std::string link;
	};

public:
	void setDefaultFontSize(float s) noexcept { m_defaultFontSize = s; }
	void setDefaultColor(const sgc::Colorf& c) noexcept { m_defaultColor = c; }
	[[nodiscard]] float defaultFontSize() const noexcept { return m_defaultFontSize; }
	[[nodiscard]] const sgc::Colorf& defaultColor() const noexcept { return m_defaultColor; }

	/// @brief マークアップをセグメント列にパースする
	[[nodiscard]] std::vector<RichTextSegment> parse(std::string_view markup) const
	{
		std::vector<RichTextSegment> segs;
		std::vector<StyleFrame> stack;
		StyleFrame cur;
		cur.color = m_defaultColor;
		cur.fontSize = m_defaultFontSize;
		std::string buf;
		std::size_t i = 0;
		auto flush = [&]() { if (!buf.empty()) { segs.push_back({buf, cur.color, cur.bold, cur.italic, cur.fontSize, cur.link, {}}); buf.clear(); } };
		auto pop = [&]() { if (!stack.empty()) { cur = stack.back(); stack.pop_back(); } };
		while (i < markup.size()) {
			if (markup[i] != '<') { buf += markup[i]; ++i; continue; }
			flush();
			const auto end = markup.find('>', i);
			if (end == std::string_view::npos) { buf += markup[i]; ++i; continue; }
			const auto tag = markup.substr(i + 1, end - i - 1);
			i = end + 1;
			if (tag == "b") { stack.push_back(cur); cur.bold = true; }
			else if (tag == "/b") { pop(); }
			else if (tag == "i") { stack.push_back(cur); cur.italic = true; }
			else if (tag == "/i") { pop(); }
			else if (tag.substr(0, 6) == "color=") { stack.push_back(cur); cur.color = parseHexColor(tag.substr(6)); }
			else if (tag == "/color") { pop(); }
			else if (tag.substr(0, 5) == "size=") { stack.push_back(cur); cur.fontSize = toFloat(tag.substr(5), m_defaultFontSize); }
			else if (tag == "/size") { pop(); }
			else if (tag.substr(0, 5) == "link=") { stack.push_back(cur); cur.link = std::string(tag.substr(5)); }
			else if (tag == "/link") { pop(); }
			else if (tag.substr(0, 6) == "emoji=") { RichTextSegment es; es.emoji = std::string(tag.substr(6)); es.fontSize = cur.fontSize; es.color = cur.color; segs.push_back(std::move(es)); }
			else { buf += '<'; buf += std::string(tag); buf += '>'; }
		}
		flush();
		return segs;
	}

	/// @brief セグメント列の描画サイズを計測する
	[[nodiscard]] RichTextMeasurement measure(const std::vector<RichTextSegment>& segs, float maxW) const noexcept
	{
		if (segs.empty()) { return {}; }
		float cx = 0.0f, maxLW = 0.0f;
		int lines = 1;
		for (const auto& s : segs) {
			if (!s.emoji.empty()) {
				if (cx + s.fontSize > maxW && cx > 0.0f) { maxLW = std::max(maxLW, cx); cx = 0.0f; ++lines; }
				cx += s.fontSize;
				continue;
			}
			const float cw = charWidth(s.fontSize);
			for (char ch : s.text) {
				if (ch == '\n') { maxLW = std::max(maxLW, cx); cx = 0.0f; ++lines; continue; }
				if (cx + cw > maxW && cx > 0.0f) { maxLW = std::max(maxLW, cx); cx = 0.0f; ++lines; }
				cx += cw;
			}
		}
		return {std::max(maxLW, cx), static_cast<float>(lines) * lineH(m_defaultFontSize), lines};
	}

	/// @brief セグメント列をScreen上に描画する
	template <typename ScreenT>
	void render(ScreenT& screen, const std::vector<RichTextSegment>& segs, const sgc::Rectf& rect) const
	{
		float cx = rect.x(), cy = rect.y();
		const float right = rect.x() + rect.width();
		const float bottom = rect.y() + rect.height();
		for (const auto& s : segs) {
			if (!s.emoji.empty()) {
				if (cx + s.fontSize > right && cx > rect.x()) { cx = rect.x(); cy += lineH(s.fontSize); }
				screen.drawRect(sgc::Rectf{cx, cy, s.fontSize, s.fontSize}, s.color);
				cx += s.fontSize;
				continue;
			}
			const float cw = charWidth(s.fontSize), lh = lineH(s.fontSize);
			for (std::size_t ci = 0; ci < s.text.size(); ++ci) {
				if (s.text[ci] == '\n') { cx = rect.x(); cy += lh; continue; }
				if (cx + cw > right && cx > rect.x()) { cx = rect.x(); cy += lh; }
				if (cy + lh > bottom) { return; }
				screen.drawText(sgc::Vec2f{cx, cy}, std::string_view{&s.text[ci], 1}, s.color, s.fontSize);
				cx += cw;
			}
		}
	}

private:
	[[nodiscard]] static sgc::Colorf parseHexColor(std::string_view s) noexcept
	{
		if (s.empty()) { return {1.0f, 1.0f, 1.0f, 1.0f}; }
		if (s[0] == '#') { s = s.substr(1); }
		auto hb = [&](std::size_t off) -> float {
			if (off + 2 > s.size()) { return 1.0f; }
			unsigned v = 0;
			for (int j = 0; j < 2; ++j) {
				char c = s[off + static_cast<std::size_t>(j)]; v <<= 4;
				if (c >= '0' && c <= '9') v += static_cast<unsigned>(c - '0');
				else if (c >= 'a' && c <= 'f') v += static_cast<unsigned>(c - 'a' + 10);
				else if (c >= 'A' && c <= 'F') v += static_cast<unsigned>(c - 'A' + 10);
			}
			return static_cast<float>(v) / 255.0f;
		};
		return {hb(0), hb(2), hb(4), s.size() >= 8 ? hb(6) : 1.0f};
	}

	[[nodiscard]] static float toFloat(std::string_view s, float def) noexcept
	{
		float r = 0.0f; bool dot = false; float d = 0.1f; bool ok = false;
		for (char c : s) {
			if (c >= '0' && c <= '9') { ok = true; if (dot) { r += (c - '0') * d; d *= 0.1f; } else { r = r * 10.0f + (c - '0'); } }
			else if (c == '.' && !dot) { dot = true; }
		}
		return ok ? r : def;
	}
};

} // namespace mitiru::ui
