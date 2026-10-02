#pragma once

/// @file ImeComposition.hpp
/// @brief IME の変換中の文字列 (UTF-16 とキャレット位置) を、ゲームへ渡す UTF-8 と byte 位置にする。
/// @details Win32 の API に触らない純粋な変換なので、IME の無い環境でもテストできる。

#include <cstddef>
#include <string>
#include <string_view>

namespace mitiru::platform
{

struct ImeCompositionUtf8
{
	std::string text;
	std::size_t cursorBytes = 0;  ///< text の先頭からの byte 数
};

namespace detail
{

inline void appendUtf8(std::string& out, char32_t cp)
{
	if (cp < 0x80) { out += static_cast<char>(cp); return; }
	if (cp < 0x800)
	{
		out += static_cast<char>(0xC0 | (cp >> 6));
	}
	else if (cp < 0x10000)
	{
		out += static_cast<char>(0xE0 | (cp >> 12));
		out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
	}
	else
	{
		out += static_cast<char>(0xF0 | (cp >> 18));
		out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
		out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
	}
	out += static_cast<char>(0x80 | (cp & 0x3F));
}

}  // namespace detail

/// @param cursorUtf16 ImmGetCompositionString(GCS_CURSORPOS) の値 (UTF-16 単位)。範囲外は末尾。
///        サロゲートの間を指していたら、その文字の後ろの位置にする。
inline ImeCompositionUtf8 toUtf8Composition(std::u16string_view s, std::size_t cursorUtf16)
{
	ImeCompositionUtf8 out;
	bool cursorSet = false;
	for (std::size_t i = 0; i < s.size(); ++i)
	{
		if (!cursorSet && i >= cursorUtf16) { out.cursorBytes = out.text.size(); cursorSet = true; }
		char32_t cp = s[i];
		const bool high = cp >= 0xD800 && cp <= 0xDBFF;
		if (high && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF)
		{
			cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00);
			++i;
		}
		else if (cp >= 0xD800 && cp <= 0xDFFF)
		{
			cp = 0xFFFD;  // 対になっていないサロゲート
		}
		detail::appendUtf8(out.text, cp);
	}
	if (!cursorSet) { out.cursorBytes = out.text.size(); }
	return out;
}

/// @brief UTF-8 の s から、maxBytes 以下で文字の途中を切らない長さを返す。
[[nodiscard]] inline std::size_t utf8PrefixWithin(std::string_view s, std::size_t maxBytes) noexcept
{
	if (s.size() <= maxBytes) { return s.size(); }
	std::size_t n = maxBytes;
	while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) { --n; }
	return n;
}

}  // namespace mitiru::platform
