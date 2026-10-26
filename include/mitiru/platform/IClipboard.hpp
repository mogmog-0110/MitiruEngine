#pragma once

/// @file IClipboard.hpp
/// @brief テキストのクリップボードの窓口と、改行の写し方

#include <optional>
#include <string>
#include <string_view>

namespace mitiru::platform
{

class IClipboard
{
public:
	virtual ~IClipboard() = default;

	/// UTF-8 の文字列を置く。改行は LF のままで渡し、OS の流儀への直しは実装がする
	virtual bool setText(std::string_view utf8) = 0;

	/// 文字列を LF 改行の UTF-8 で返す。文字が入っていなければ空、読めなければ nullopt
	[[nodiscard]] virtual std::optional<std::string> getText() = 0;
};

/// LF と単独の CR を CRLF にする (Windows のクリップボードの改行)
[[nodiscard]] inline std::string toCrlf(std::string_view text)
{
	std::string out;
	out.reserve(text.size() + text.size() / 8);
	for (std::size_t i = 0; i < text.size(); ++i)
	{
		const char c = text[i];
		if (c == '\r')
		{
			out += "\r\n";
			if (i + 1 < text.size() && text[i + 1] == '\n') { ++i; }
		}
		else if (c == '\n') { out += "\r\n"; }
		else { out += c; }
	}
	return out;
}

/// CRLF と単独の CR を LF にする (RmlUi の入力欄は LF を改行として持つ)
[[nodiscard]] inline std::string toLf(std::string_view text)
{
	std::string out;
	out.reserve(text.size());
	for (std::size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] == '\r')
		{
			out += '\n';
			if (i + 1 < text.size() && text[i + 1] == '\n') { ++i; }
		}
		else { out += text[i]; }
	}
	return out;
}

} // namespace mitiru::platform
