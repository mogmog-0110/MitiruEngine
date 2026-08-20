#pragma once

/// @file Utf8Text.hpp (UTF-8 テキストを文字単位で扱う共通ユーティリティ)
/// @details 日本語は 1 文字が複数バイトなので、バイト数の操作 (strlen / substr) では
///          「何文字目まで」を扱えず、途中で切ると文字が壊れる。字送り・折り返し・
///          文字数制限など、文字単位が要る場面はここを使う。
///          render/sdf の Utf8Decoder はグリフ描画専用 (codepoint 列へ落とす)。ここは
///          バイト列のまま文字境界だけを見る軽い操作を集める。

#include <cstddef>
#include <string_view>

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

}  // namespace mitiru::i18n
