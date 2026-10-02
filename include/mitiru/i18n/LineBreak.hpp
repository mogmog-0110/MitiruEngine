#pragma once

/// @file LineBreak.hpp
/// @brief 日本語の禁則と欧文の語区切りに沿って、行の折り返し位置を決める。
/// @details 幅は呼び出し側が字形選択後の字送りで測る。カーニングと合字を含む。

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <mitiru/i18n/Utf8Text.hpp>

namespace mitiru::i18n
{

class KinsokuRules
{
public:
	[[nodiscard]] static bool isLineStartProhibited(std::uint32_t cp) noexcept
	{
		// 閉じ括弧、句読点、小書きかな
		switch (cp)
		{
		// 句読点
		case U'\u3001': // 、
		case U'\u3002': // 。
		case U'\uFF0C': // ，
		case U'\uFF0E': // ．
		case U'\uFF01': // ！
		case U'\uFF1F': // ？
		// 閉じ括弧
		case U'\u3009': // 〉
		case U'\u300B': // 》
		case U'\u300D': // 」
		case U'\u300F': // 』
		case U'\u3011': // 】
		case U'\uFF09': // ）
		case U'\uFF3D': // ］
		case U'\uFF5D': // ｝
		case U')':
		case U']':
		case U'}':
		// 中点、長音
		case U'\u30FB': // ・
		case U'\u30FC': // ー
		// 小書きかな
		case U'\u3041': // ぁ
		case U'\u3043': // ぃ
		case U'\u3045': // ぅ
		case U'\u3047': // ぇ
		case U'\u3049': // ぉ
		case U'\u3063': // っ
		case U'\u3083': // ゃ
		case U'\u3085': // ゅ
		case U'\u3087': // ょ
		case U'\u308E': // ゎ
		case U'\u30A1': // ァ
		case U'\u30A3': // ィ
		case U'\u30A5': // ゥ
		case U'\u30A7': // ェ
		case U'\u30A9': // ォ
		case U'\u30C3': // ッ
		case U'\u30E3': // ャ
		case U'\u30E5': // ュ
		case U'\u30E7': // ョ
		case U'\u30EE': // ヮ
		// ASCII 句読点
		case U',':
		case U'.':
		case U'!':
		case U'?':
		case U':':
		case U';':
			return true;
		default:
			return false;
		}
	}

	[[nodiscard]] static bool isLineEndProhibited(std::uint32_t cp) noexcept
	{
		switch (cp)
		{
		case U'\u3008': // 〈
		case U'\u300A': // 《
		case U'\u300C': // 「
		case U'\u300E': // 『
		case U'\u3010': // 【
		case U'\uFF08': // （
		case U'\uFF3B': // ［
		case U'\uFF5B': // ｛
		case U'(':
		case U'[':
		case U'{':
			return true;
		default:
			return false;
		}
	}

	[[nodiscard]] static bool isCjk(std::uint32_t cp) noexcept
	{
		return (cp >= 0x3000 && cp <= 0x9FFF)    // CJK統合漢字、ひらがな、カタカナ等
			|| (cp >= 0xF900 && cp <= 0xFAFF)    // CJK互換漢字
			|| (cp >= 0xFF00 && cp <= 0xFFEF)    // 半角・全角形
			|| (cp >= 0x20000 && cp <= 0x2FA1F); // CJK統合漢字拡張
	}

	[[nodiscard]] static bool canBreakAfter(std::uint32_t cp, std::uint32_t nextCp) noexcept
	{
		if (isLineEndProhibited(cp))
		{
			return false;
		}

		if (isLineStartProhibited(nextCp))
		{
			return false;
		}

		// CJK 文字の前後では改行できる
		if (isCjk(cp) || isCjk(nextCp))
		{
			return true;
		}

		if (cp == U' ' || cp == U'\u3000') // 半角・全角スペース
		{
			return true;
		}

		return false;
	}
};

/// @brief line の先頭から maxWidth に収まる 1 行の終端をバイト位置で返す。
/// @details 文字の途中では切らない。行頭禁止文字 (。、」っ など) は前の行に残し、
/// 行末禁止文字 (「（ など) は次の行へ送る。
/// 欧文は空白の後で折り返し、1 語が幅を超えるときだけ語の途中で切る。
/// 1 文字も入らない幅でも 1 文字は進める。
/// @param line 改行を含まない 1 段落
/// @param measure std::string_view の幅を返す関数
/// @param cps,offsets 呼び出し側で使い回す作業領域
template <class Measure>
[[nodiscard]] std::size_t fitLine(std::string_view line, float maxWidth, Measure&& measure,
                                  std::vector<std::uint32_t>& cps, std::vector<std::size_t>& offsets)
{
	cps.clear();
	offsets.clear();
	for (std::size_t i = 0; i < line.size();)
	{
		int len = 1;
		cps.push_back(decodeAt(line, i, len));
		offsets.push_back(i);
		i += static_cast<std::size_t>(len);
	}
	offsets.push_back(line.size());
	const std::size_t n = cps.size();
	if (n <= 1 || measure(line) <= maxWidth) { return line.size(); }

	// 幅が文字数に対して単調に増える前提で、収まる最大文字数を二分探索する。
	std::size_t lo = 1, hi = n - 1;
	while (lo < hi)
	{
		const std::size_t mid = (lo + hi + 1) / 2;
		if (measure(line.substr(0, offsets[mid])) <= maxWidth) { lo = mid; }
		else { hi = mid - 1; }
	}
	// カーニングで接頭辞の幅が縮む所を二分探索が飛ばしたときのため、次の文字も入るか確かめる。
	std::size_t fit = lo;
	while (fit + 1 < n && measure(line.substr(0, offsets[fit + 1])) <= maxWidth) { ++fit; }
	if (KinsokuRules::isLineStartProhibited(cps[fit])) { return offsets[fit + 1]; }

	std::size_t at = fit;
	while (at > 0 && !KinsokuRules::canBreakAfter(cps[at - 1], cps[at])) { --at; }
	return offsets[at == 0 ? fit : at];
}

} // namespace mitiru::i18n
