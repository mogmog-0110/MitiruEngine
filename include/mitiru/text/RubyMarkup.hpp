#pragma once

/// @file RubyMarkup.hpp
/// @brief 文字列内のルビ記法を、地の文と「親文字 + ルビ」の列に分ける宣言。
/// @details 青空文庫や小説投稿サイトの書き方に合わせる。
/// `｜親文字《るび》` では、全角 ｜ または半角 | から 《 の手前までを親文字とする。
/// `漢字《かんじ》` では、《 の直前に続く漢字を親文字とする。
/// 直前が漢字でなく ｜ も無い 《》 は、書名の 《》 を壊さないよう、そのまま残す。
/// 結果の string_view はすべて入力文字列を指す。

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <mitiru/i18n/Utf8Text.hpp>

namespace mitiru::text
{

struct MarkupSegment
{
	std::string_view text; ///< 地の文、またはルビの親文字
	std::string_view ruby; ///< 空なら地の文
};

namespace detail
{

constexpr std::string_view kRubyOpen = "\xE3\x80\x8A";  // 《
constexpr std::string_view kRubyClose = "\xE3\x80\x8B"; // 》
constexpr std::string_view kFullwidthBar = "\xEF\xBD\x9C"; // ｜

[[nodiscard]] constexpr bool isRubyBaseKanji(std::uint32_t cp) noexcept
{
	return (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0xF900 && cp <= 0xFAFF)
	    || (cp >= 0x20000 && cp <= 0x2FA1F) || cp == 0x3005 || cp == 0x3006 || cp == 0x3007 || cp == 0x30F6;
}

[[nodiscard]] constexpr std::size_t barLengthAt(std::string_view s, std::size_t at) noexcept
{
	if (s.compare(at, kFullwidthBar.size(), kFullwidthBar) == 0) { return kFullwidthBar.size(); }
	return (at < s.size() && s[at] == '|') ? 1 : 0;
}

/// @brief open の 《 に対応する 》 を探す。途中に改行、別の 《、｜ があれば npos を返す。
[[nodiscard]] constexpr std::size_t findRubyClose(std::string_view s, std::size_t open) noexcept
{
	for (std::size_t i = open + kRubyOpen.size(); i < s.size(); ++i)
	{
		if (s[i] == '\n') { return std::string_view::npos; }
		if (s.compare(i, kRubyClose.size(), kRubyClose) == 0) { return i; }
		if (s.compare(i, kRubyOpen.size(), kRubyOpen) == 0) { return std::string_view::npos; }
	}
	return std::string_view::npos;
}

} // namespace detail

/// @brief ルビ記法を分解する。ルビが無ければ、文字列全体を 1 つの地の文とする。
inline void parseRubyMarkup(std::string_view s, std::vector<MarkupSegment>& out)
{
	out.clear();
	std::size_t plainStart = 0;
	const auto flushPlain = [&](std::size_t end) {
		if (end > plainStart) { out.push_back({s.substr(plainStart, end - plainStart), {}}); }
	};

	for (std::size_t i = 0; i < s.size();)
	{
		const std::size_t barLen = detail::barLengthAt(s, i);
		if (barLen != 0)
		{
			// 親文字の途中に改行や別の ｜ があれば、`｜親《るび》` の記法として扱わない。
			std::size_t open = std::string_view::npos;
			for (std::size_t k = i + barLen; k < s.size(); ++k)
			{
				if (s[k] == '\n' || detail::barLengthAt(s, k) != 0) { break; }
				if (s.compare(k, detail::kRubyOpen.size(), detail::kRubyOpen) == 0) { open = k; break; }
			}
			const std::size_t close = open == std::string_view::npos ? open : detail::findRubyClose(s, open);
			const std::size_t rubyBegin = open == std::string_view::npos ? 0 : open + detail::kRubyOpen.size();
			if (close != std::string_view::npos && open > i + barLen && close > rubyBegin)
			{
				flushPlain(i);
				out.push_back({s.substr(i + barLen, open - (i + barLen)), s.substr(rubyBegin, close - rubyBegin)});
				i = close + detail::kRubyClose.size();
				plainStart = i;
				continue;
			}
			i += barLen;
			continue;
		}
		if (s.compare(i, detail::kRubyOpen.size(), detail::kRubyOpen) == 0)
		{
			const std::size_t close = detail::findRubyClose(s, i);
			// 親文字は、現在の地の文の末尾に続く漢字。
			std::size_t base = i;
			while (base > plainStart)
			{
				std::size_t prev = base - 1;
				while (prev > plainStart && !i18n::isUtf8Lead(s[prev])) { --prev; }
				int len = 1;
				if (!detail::isRubyBaseKanji(i18n::decodeAt(s, prev, len))) { break; }
				base = prev;
			}
			const std::size_t rubyBegin = i + detail::kRubyOpen.size();
			if (close != std::string_view::npos && base < i && close > rubyBegin)
			{
				flushPlain(base);
				out.push_back({s.substr(base, i - base), s.substr(rubyBegin, close - rubyBegin)});
				i = close + detail::kRubyClose.size();
				plainStart = i;
				continue;
			}
			i += detail::kRubyOpen.size();
			continue;
		}
		++i;
	}
	flushPlain(s.size());
}

[[nodiscard]] inline std::vector<MarkupSegment> parseRubyMarkup(std::string_view s)
{
	std::vector<MarkupSegment> out;
	parseRubyMarkup(s, out);
	return out;
}

} // namespace mitiru::text
