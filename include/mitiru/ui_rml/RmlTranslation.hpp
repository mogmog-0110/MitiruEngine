#pragma once
// RML の文言を LocalizationManager の表で訳す。RmlUi は文書を読む時と、{{ }} の値で文が変わる時に
// SystemInterface::TranslateString を呼ぶので、そこからこれを呼ぶ。
//
// 書き方は "[menu.start]" (訳す) と "[coins:3]" (3 を数として複数形を選び、{0} に入れる)。数は data binding で
// "[coins:{{ coins }}]" と書けば、値が変わるたびに訳し直される。表に無いキーと、キーの形でない "[...]" は
// そのまま残す ("[A] で決定" のような地の文を壊さない)。

#include <mitiru/core/Localization.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace mitiru::ui_rml
{

namespace detail
{

[[nodiscard]] constexpr bool isKeyChar(char c) noexcept
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
}

[[nodiscard]] inline std::optional<int> parseCount(std::string_view s) noexcept
{
	if (s.empty() || s.size() > 9) { return std::nullopt; }
	const bool negative = s.front() == '-';
	if (negative) { s.remove_prefix(1); }
	if (s.empty()) { return std::nullopt; }
	int n = 0;
	for (const char c : s)
	{
		if (c < '0' || c > '9') { return std::nullopt; }
		n = n * 10 + (c - '0');
	}
	return negative ? -n : n;
}

/// "[...]" の中身 1 つを訳す。訳せなければ nullopt。
[[nodiscard]] inline std::optional<std::string> translateToken(const LocalizationManager& loc, std::string_view token)
{
	const std::size_t colon = token.find(':');
	const std::string_view key = token.substr(0, colon);
	if (key.empty()) { return std::nullopt; }
	for (const char c : key)
	{
		if (!isKeyChar(c)) { return std::nullopt; }
	}
	if (!loc.hasKey(key)) { return std::nullopt; }
	if (colon == std::string_view::npos) { return loc.t(key); }
	const auto count = parseCount(token.substr(colon + 1));
	if (!count) { return std::nullopt; }
	return loc.tp(key, *count);
}

}  // namespace detail

/// @brief in の "[key]" と "[key:N]" を今の言語の訳にして out へ書く。置き換えた数を返す。
inline int translateRmlText(const LocalizationManager& loc, std::string_view in, std::string& out)
{
	out.clear();
	out.reserve(in.size());
	int replaced = 0;
	std::size_t pos = 0;
	while (pos < in.size())
	{
		const std::size_t open = in.find('[', pos);
		const std::size_t close = open == std::string_view::npos ? open : in.find(']', open + 1);
		if (close == std::string_view::npos)
		{
			out.append(in.substr(pos));
			break;
		}
		out.append(in.substr(pos, open - pos));
		if (const auto text = detail::translateToken(loc, in.substr(open + 1, close - open - 1)))
		{
			out.append(*text);
			++replaced;
			pos = close + 1;
		}
		else
		{
			out.push_back('[');
			pos = open + 1;
		}
	}
	return replaced;
}

}  // namespace mitiru::ui_rml
