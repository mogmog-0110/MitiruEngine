#pragma once

/// @file TranslationCheck.hpp
/// @brief UI の訳の表 (RML の文書の隣の strings.json) を集め、言語ごとに訳の抜けと書体に無い字を並べる
/// @details 言語は表の languages (無ければ訳に出てくる言語の全部)。書体の並びは、RML の UI がその文書のために読む
///          並び (UiFontFiles.hpp) と同じにする。数で選ぶ訳 (key_one など) は元のキーがあれば抜けに数えない
///          (訳が無くても元のキーの訳が出る)。訳の抜けたキーは、出る字が予備の言語の字なので字の点検から外す。
///          mitiru_host --check-i18n がこれを使い、mitiru dist --check が呼ぶ。

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/core/Localization.hpp>
#include <mitiru/text/FontCoverage.hpp>
#include <mitiru/text/FontFace.hpp>
#include <mitiru/text/UiFontFiles.hpp>

namespace mitiru::text
{

struct TranslationIssue
{
	std::string file;                        ///< 点検した根からの相対 (/ 区切り)
	std::string language;
	std::string key;
	std::vector<std::uint32_t> uncovered;    ///< 空なら訳が無い。あれば書体のどれにも無い字
};

struct TranslationReport
{
	std::size_t files = 0;                   ///< 読んだ strings.json の数
	std::vector<TranslationIssue> issues;
	std::vector<std::string> unreadable;     ///< JSON として読めない表 (根からの相対)
};

namespace detail
{

[[nodiscard]] inline std::string utf8Of(const std::filesystem::path& p)
{
	const std::u8string u = p.generic_u8string();
	return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

[[nodiscard]] inline std::vector<std::filesystem::path> stringsFilesUnder(const std::filesystem::path& root)
{
	std::vector<std::filesystem::path> out;
	std::error_code ec;
	for (auto it = std::filesystem::recursive_directory_iterator(root, ec); !ec && it != std::filesystem::recursive_directory_iterator();
	     it.increment(ec))
	{
		if (it->is_regular_file(ec) && it->path().filename() == "strings.json") { out.push_back(it->path()); }
	}
	std::sort(out.begin(), out.end());
	return out;
}

/// @brief 表の languages の code。無ければ訳に出てくる言語の全部
[[nodiscard]] inline std::vector<std::string> languagesOf(const LocalizationManager& loc)
{
	std::set<std::string> codes;
	for (const Language& l : loc.availableLanguages()) { codes.insert(l.code); }
	if (codes.empty())
	{
		for (const auto& [key, langs] : loc.translations())
		{
			for (const auto& [code, text] : langs) { codes.insert(code); }
		}
	}
	return {codes.begin(), codes.end()};
}

inline void appendUtf8(std::string& out, std::uint32_t cp)
{
	if (cp < 0x80) { out += static_cast<char>(cp); return; }
	if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); }
	else
	{
		if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); }
		else
		{
			out += static_cast<char>(0xF0 | (cp >> 18));
			out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
		}
		out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
	}
	out += static_cast<char>(0x80 | (cp & 0x3F));
}

/// @brief key が数で選ぶ訳 (key_one など) で、元のキーが表にあるか
[[nodiscard]] inline bool pluralVariantWithBase(const LocalizationManager& loc, std::string_view key)
{
	for (const std::string_view suffix : {"_zero", "_one", "_two", "_few", "_many", "_other"})
	{
		if (key.size() > suffix.size() && key.substr(key.size() - suffix.size()) == suffix &&
		    loc.hasKey(key.substr(0, key.size() - suffix.size())))
		{
			return true;
		}
	}
	return false;
}

/// @brief 書体のファイルを 1 度だけ読む
class FontCache
{
public:
	[[nodiscard]] std::vector<const FontFace*> chainFor(const std::filesystem::path& engineFonts, const std::filesystem::path& documentDir)
	{
		std::vector<const FontFace*> chain;
		for (const auto& dir : uiFontDirs(engineFonts, documentDir))
		{
			for (const auto& file : fontFilesIn(dir))
			{
				if (const FontFace* f = load(file)) { chain.push_back(f); }
			}
		}
		return chain;
	}

private:
	[[nodiscard]] const FontFace* load(const std::filesystem::path& file)
	{
		const std::string key = utf8Of(file);
		auto it = m_faces.find(key);
		if (it == m_faces.end())
		{
			std::string error;
			it = m_faces.emplace(key, FontFace::loadFile(key, error)).first;
		}
		return it->second.get();
	}

	std::map<std::string, std::unique_ptr<FontFace>> m_faces;
};

inline void checkLanguage(const LocalizationManager& loc, const std::string& file, const std::string& lang,
                          std::span<const FontFace* const> chain, std::vector<TranslationIssue>& out)
{
	std::set<std::string> missing;
	for (const std::string& key : loc.missingTranslations(lang))
	{
		if (pluralVariantWithBase(loc, key)) { continue; }
		missing.insert(key);
		out.push_back({file, lang, key, {}});
	}
	for (auto& [key, chars] : uncoveredTranslations(loc, lang, chain))
	{
		if (missing.count(key) == 0 && loc.find(key, lang)) { out.push_back({file, lang, key, std::move(chars)}); }
	}
}

} // namespace detail

/// @brief root の下の strings.json を全部読み、言語ごとに訳の抜けと書体に無い字を並べる。engineFonts は同梱の書体のフォルダ
[[nodiscard]] inline TranslationReport checkTranslations(const std::filesystem::path& root, const std::filesystem::path& engineFonts)
{
	TranslationReport report;
	detail::FontCache fonts;
	for (const auto& path : detail::stringsFilesUnder(root))
	{
		const std::string rel = detail::utf8Of(path.lexically_relative(root));
		std::ifstream in(path, std::ios::binary);
		const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		const auto j = nlohmann::json::parse(text, nullptr, false);
		LocalizationManager loc;
		if (!j.is_object() || !loc.loadTranslationsFromString(text))
		{
			report.unreadable.push_back(rel);
			continue;
		}
		if (j.contains("fallback") && j["fallback"].is_string()) { loc.setFallbackLanguage(j["fallback"].get<std::string>()); }
		++report.files;
		const auto chain = fonts.chainFor(engineFonts, path.parent_path());
		for (const std::string& lang : detail::languagesOf(loc)) { detail::checkLanguage(loc, rel, lang, chain, report.issues); }
	}
	return report;
}

/// @brief 1 行 1 件の TSV。"i18n\tmissing\t<file>\t<lang>\t<key>"、"i18n\tglyphs\t<file>\t<lang>\t<key>\t<字>"、
///        "i18n\tunreadable\t<file>"、最後に "i18n\tdone\t<表の数>\t<件数>"。終わりの行が無ければ点検が途中で止まった
[[nodiscard]] inline std::string translationReportTsv(const TranslationReport& r)
{
	const auto clean = [](std::string s) {
		std::replace_if(s.begin(), s.end(), [](char c) { return c == '\t' || c == '\n' || c == '\r'; }, ' ');
		return s;
	};
	std::string out;
	for (const std::string& f : r.unreadable) { out += "i18n\tunreadable\t" + clean(f) + "\n"; }
	for (const TranslationIssue& i : r.issues)
	{
		const std::string head = "\t" + clean(i.file) + "\t" + clean(i.language) + "\t" + clean(i.key);
		if (i.uncovered.empty()) { out += "i18n\tmissing" + head + "\n"; continue; }
		std::string chars;
		for (const std::uint32_t cp : i.uncovered) { detail::appendUtf8(chars, cp); }
		out += "i18n\tglyphs" + head + "\t" + clean(chars) + "\n";
	}
	out += "i18n\tdone\t" + std::to_string(r.files) + "\t" + std::to_string(r.issues.size() + r.unreadable.size()) + "\n";
	return out;
}

} // namespace mitiru::text
