#pragma once

/// @file JsonDiagnostics.hpp
/// @brief 手で書く JSON (sounds.json・animgraph・world.json 等) の読み込みの誤りを、行・列・欄の名前つきで返す。

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace mitiru::data
{

/// @brief text の byteOffset に対応する行と列。どちらも 1 始まり。
struct TextPosition
{
	std::size_t line = 1;
	std::size_t column = 1;
};

[[nodiscard]] inline TextPosition textPositionAt(std::string_view text, std::size_t byteOffset) noexcept
{
	TextPosition p;
	const std::size_t end = std::min(byteOffset, text.size());
	for (std::size_t i = 0; i < end; ++i)
	{
		if (text[i] == '\n') { ++p.line; p.column = 1; }
		else { ++p.column; }
	}
	return p;
}

/// @brief text を JSON として読む。読めなければ discarded を返し、error に「what の L 行 C 列」と誤りの内容を書く。
[[nodiscard]] inline nlohmann::json parseJsonText(std::string_view text, std::string_view what, std::string& error)
{
	try
	{
		return nlohmann::json::parse(text.begin(), text.end());
	}
	catch (const nlohmann::json::parse_error& e)
	{
		// e.byte は誤りを見つけた文字の次を指す。1 始まり。
		const TextPosition p = textPositionAt(text, e.byte > 0 ? e.byte - 1 : 0);
		std::string detail = e.what();
		if (const std::size_t colon = detail.rfind(": "); colon != std::string::npos) { detail = detail.substr(colon + 2); }
		error = std::string(what) + " の " + std::to_string(p.line) + " 行 " + std::to_string(p.column)
		      + " 列を JSON として読めません (" + detail + ")。末尾のカンマと、括弧や引用符の閉じ忘れを確かめてください";
		return nlohmann::json(nlohmann::json::value_t::discarded);
	}
}

namespace detail
{
[[nodiscard]] inline std::size_t editDistanceIgnoreCase(std::string_view a, std::string_view b)
{
	std::vector<std::size_t> row(b.size() + 1, 0), last(b.size() + 1, 0);
	for (std::size_t j = 0; j <= b.size(); ++j) { last[j] = j; }
	for (std::size_t i = 1; i <= a.size(); ++i)
	{
		row[0] = i;
		for (std::size_t j = 1; j <= b.size(); ++j)
		{
			const bool same = std::tolower(static_cast<unsigned char>(a[i - 1])) == std::tolower(static_cast<unsigned char>(b[j - 1]));
			row[j] = std::min({last[j] + 1, row[j - 1] + 1, last[j - 1] + (same ? 0u : 1u)});
		}
		std::swap(row, last);
	}
	return last[b.size()];
}
}  // namespace detail

/// @brief obj に known にない欄があれば「where: 知らない欄 'x'」を返す (近い名前があれば添える)。無ければ空。
/// @details 綴りを間違えた欄は読み飛ばされて既定値が使われるため、読み込み時にエラーにする。
[[nodiscard]] inline std::string unknownKeyError(const nlohmann::json& obj, std::initializer_list<std::string_view> known,
                                                 std::string_view where)
{
	if (!obj.is_object()) { return {}; }
	for (const auto& [key, value] : obj.items())
	{
		(void)value;
		if (std::find(known.begin(), known.end(), std::string_view(key)) != known.end()) { continue; }
		std::string msg = std::string(where) + ": 知らない欄 '" + key + "'";
		std::string_view nearest;
		std::size_t best = 3;
		for (std::string_view k : known)
		{
			const std::size_t d = detail::editDistanceIgnoreCase(key, k);
			if (d < best) { best = d; nearest = k; }
		}
		if (!nearest.empty()) { msg += " ('" + std::string(nearest) + "' のつもりなら直す)"; }
		return msg;
	}
	return {};
}

}  // namespace mitiru::data
