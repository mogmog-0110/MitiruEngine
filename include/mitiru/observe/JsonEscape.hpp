#pragma once

/// @file JsonEscape.hpp
/// @brief JSON文字列エスケープユーティリティ

#include <string>
#include <string_view>

namespace mitiru::observe
{

/// @brief JSON文字列値に含まれる特殊文字をエスケープする
/// @param input エスケープ対象の文字列
/// @return エスケープ済み文字列（引用符は含まない）
[[nodiscard]] inline std::string jsonEscape(std::string_view input)
{
	std::string result;
	result.reserve(input.size() + input.size() / 8);
	for (const char ch : input)
	{
		switch (ch)
		{
		case '"':  result += "\\\""; break;
		case '\\': result += "\\\\"; break;
		case '\b': result += "\\b";  break;
		case '\f': result += "\\f";  break;
		case '\n': result += "\\n";  break;
		case '\r': result += "\\r";  break;
		case '\t': result += "\\t";  break;
		default:
			if (static_cast<unsigned char>(ch) < 0x20)
			{
				constexpr char hex[] = "0123456789abcdef";
				result += "\\u00";
				result += hex[(static_cast<unsigned char>(ch) >> 4) & 0xF];
				result += hex[static_cast<unsigned char>(ch) & 0xF];
			}
			else
			{
				result += ch;
			}
			break;
		}
	}
	return result;
}

} // namespace mitiru::observe
