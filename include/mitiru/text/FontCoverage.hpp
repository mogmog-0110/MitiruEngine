#pragma once

/// @file FontCoverage.hpp
/// @brief 訳した文字が、使う書体 (と予備の書体) のどれかに入っているかを確かめる。
/// @details 書体に無い文字は豆腐 (.notdef) で出る。画面で気づく前に、翻訳の表を書体の並びと突き合わせて、
///          どのキーのどの文字が出ないかを並べる。書体の並びは前から順に探す (描画の予備の書体と同じ順)。

#include <algorithm>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/core/Localization.hpp>
#include <mitiru/i18n/Utf8Text.hpp>
#include <mitiru/text/FontFace.hpp>

namespace mitiru::text
{

/// @brief 改行・空白・制御文字は字形が無くても困らないので数えない。
[[nodiscard]] constexpr bool needsGlyph(std::uint32_t cp) noexcept
{
	return cp > 0x20 && cp != 0x7F && !(cp >= 0x80 && cp < 0xA0) && cp != 0x200B && cp != 0xFEFF;
}

/// @brief utf8 の中で、書体の並びのどれにも無い文字 (重複なし、出てきた順)。
[[nodiscard]] inline std::vector<std::uint32_t> uncoveredCodepoints(std::string_view utf8,
                                                                   std::span<const FontFace* const> chain)
{
	std::vector<std::uint32_t> out;
	for (const std::uint32_t cp : i18n::codepoints(utf8))
	{
		if (!needsGlyph(cp)) { continue; }
		bool covered = false;
		for (const FontFace* f : chain)
		{
			if (f != nullptr && f->glyphIndex(cp) != 0) { covered = true; break; }
		}
		if (!covered && std::find(out.begin(), out.end(), cp) == out.end()) { out.push_back(cp); }
	}
	return out;
}

/// @brief language の訳 (予備の言語に落ちたものも含む) を全部調べ、出ない文字があるキーを返す。
[[nodiscard]] inline std::map<std::string, std::vector<std::uint32_t>>
uncoveredTranslations(const LocalizationManager& loc, std::string_view language, std::span<const FontFace* const> chain)
{
	std::map<std::string, std::vector<std::uint32_t>> out;
	for (const auto& entry : loc.translations())
	{
		auto missing = uncoveredCodepoints(loc.t(entry.first, language), chain);
		if (!missing.empty()) { out.emplace(entry.first, std::move(missing)); }
	}
	return out;
}

}  // namespace mitiru::text
