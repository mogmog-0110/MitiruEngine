#pragma once

/// @file Utf8Text.hpp (UTF-8 テキストを文字単位で扱う共通ユーティリティ)
/// @details 日本語は 1 文字が複数バイトなので、バイト数の操作 (strlen / substr) では
///          「何文字目まで」を扱えず、途中で切ると文字が壊れる。字送り・折り返し・
///          文字数制限など、文字単位が要る場面はここを使う。
///          バイト列のまま文字境界だけを見る軽い操作と、コードポイントへの展開を集める。

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace mitiru::i18n
{

/// @brief UTF-8 の先頭バイトか (= 文字の始まり)。続きバイト (0b10xxxxxx) なら false。
[[nodiscard]] constexpr bool isUtf8Lead(char c) noexcept
{
	return (static_cast<unsigned char>(c) & 0xC0u) != 0x80u;
}

/// @brief 文字数を数える。バイト数ではない。
[[nodiscard]] constexpr int glyphCount(std::string_view s) noexcept
{
	int n = 0;
	for (const char c : s) { if (isUtf8Lead(c)) { ++n; } }
	return n;
}

/// @brief 先頭 n 文字を buf へ写す。文字の境目でだけ止まるので、文字が壊れない。
/// @return buf (呼び出し式の中でそのまま使えるように)。
inline const char* glyphPrefix(std::string_view src, int n, char* buf, std::size_t cap)
{
	std::size_t w = 0;
	int seen = 0;
	for (const char c : src)
	{
		if (isUtf8Lead(c))
		{
			if (seen >= n) { break; }
			++seen;
		}
		if (w + 1 >= cap) { break; }
		buf[w++] = c;
	}
	if (cap > 0) { buf[w] = '\0'; }
	return buf;
}

/// @brief i 以下で最も近い文字の境目。バイト位置で切り詰めても文字が壊れないようにする。
[[nodiscard]] constexpr std::size_t floorToCharBoundary(std::string_view s, std::size_t i) noexcept
{
	if (i >= s.size()) { return s.size(); }
	while (i > 0 && !isUtf8Lead(s[i])) { --i; }
	return i;
}

/// @brief 全角スペース (U+3000 = E3 80 80) がこの位置から始まるか。
/// @details 日本語の文を手で書くとき、区切りに全角スペースが混ざる事故が一番多い。
///          空白扱いにしないと「見た目は同じなのに読めない」ファイルができる。
[[nodiscard]] constexpr bool isIdeographicSpaceAt(std::string_view s, std::size_t i) noexcept
{
	return i + 2 < s.size()
	    && static_cast<unsigned char>(s[i])     == 0xE3u
	    && static_cast<unsigned char>(s[i + 1]) == 0x80u
	    && static_cast<unsigned char>(s[i + 2]) == 0x80u;
}

/// @brief 位置 i の文字が空白 (半角 or タブ or 全角) なら、そのバイト数を返す。違えば 0。
[[nodiscard]] constexpr std::size_t spaceLenAt(std::string_view s, std::size_t i) noexcept
{
	if (i >= s.size()) { return 0; }
	if (s[i] == ' ' || s[i] == '\t') { return 1; }
	if (isIdeographicSpaceAt(s, i)) { return 3; }
	return 0;
}

/// @brief 前後の空白 (全角含む) を落とす。CR も末尾から落とす (CRLF のファイル対策)。
/// @details 末尾側は前から走査して「行末まで空白が続く最初の位置」を探す。UTF-8 は
///          後ろ向きに走査できない (続きバイトだけでは何の文字か決まらない)。
[[nodiscard]] inline std::string_view trim(std::string_view s) noexcept
{
	while (!s.empty() && s.back() == '\r') { s.remove_suffix(1); }
	for (std::size_t n = spaceLenAt(s, 0); n != 0; n = spaceLenAt(s, 0)) { s.remove_prefix(n); }

	std::size_t tail = s.size();   // ここから行末まで空白が続く。s.size() = 続いていない
	for (std::size_t i = 0; i < s.size();)
	{
		const std::size_t n = spaceLenAt(s, i);
		if (n == 0)
		{
			tail = s.size();
			i += 1;
			while (i < s.size() && !isUtf8Lead(s[i])) { ++i; }
		}
		else
		{
			if (tail == s.size()) { tail = i; }
			i += n;
		}
	}
	return s.substr(0, tail);
}

/// @brief 位置 i から 1 文字を読んでコードポイントを返し、len にバイト数を入れる。
/// @details 壊れた列 (続きバイトの不足・冗長な長さの符号化) は U+FFFD として 1 バイト進める。
[[nodiscard]] constexpr std::uint32_t decodeAt(std::string_view s, std::size_t i, int& len) noexcept
{
	len = 1;
	const auto lead = static_cast<unsigned char>(s[i]);
	if (lead < 0x80u) { return lead; }
	const int n = (lead & 0xE0u) == 0xC0u ? 2 : (lead & 0xF0u) == 0xE0u ? 3 : (lead & 0xF8u) == 0xF0u ? 4 : 0;
	if (n == 0 || i + static_cast<std::size_t>(n) > s.size()) { return 0xFFFDu; }
	std::uint32_t cp = lead & (0x7Fu >> n);
	for (int k = 1; k < n; ++k)
	{
		const auto c = static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
		if ((c & 0xC0u) != 0x80u) { return 0xFFFDu; }
		cp = (cp << 6) | (c & 0x3Fu);
	}
	constexpr std::uint32_t kMin[5] = {0, 0, 0x80u, 0x800u, 0x10000u};
	if (cp < kMin[n] || cp > 0x10FFFFu) { return 0xFFFDu; }
	len = n;
	return cp;
}

/// @brief コードポイントを UTF-8 で out へ書き、バイト数を返す (out は 4 バイト以上)。
constexpr int encodeUtf8(std::uint32_t cp, char* out) noexcept
{
	if (cp < 0x80u) { out[0] = static_cast<char>(cp); return 1; }
	if (cp < 0x800u)
	{
		out[0] = static_cast<char>(0xC0u | (cp >> 6));
		out[1] = static_cast<char>(0x80u | (cp & 0x3Fu));
		return 2;
	}
	if (cp < 0x10000u)
	{
		out[0] = static_cast<char>(0xE0u | (cp >> 12));
		out[1] = static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
		out[2] = static_cast<char>(0x80u | (cp & 0x3Fu));
		return 3;
	}
	out[0] = static_cast<char>(0xF0u | (cp >> 18));
	out[1] = static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu));
	out[2] = static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
	out[3] = static_cast<char>(0x80u | (cp & 0x3Fu));
	return 4;
}

/// @brief コードポイント列へ展開する。
[[nodiscard]] inline std::vector<std::uint32_t> codepoints(std::string_view s)
{
	std::vector<std::uint32_t> out;
	out.reserve(s.size());
	for (std::size_t i = 0; i < s.size();)
	{
		int len = 1;
		out.push_back(decodeAt(s, i, len));
		i += static_cast<std::size_t>(len);
	}
	return out;
}

}  // namespace mitiru::i18n
