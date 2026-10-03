#pragma once

/// @file Localization.hpp
/// @brief ローカライゼーション/i18n フレームワーク
/// @details JSON 形式の翻訳テーブルを読み込み、言語切り替え・キー検索・
///          フォーマット文字列・複数形選択・フォント自動選択をサポートする。
///          字形が書体にあるかは text/FontCoverage.hpp で確かめる。

#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <sgc/i18n/PluralRules.hpp>

namespace mitiru
{

/// @brief 言語定義
struct Language
{
	std::string code;     ///< 言語コード（例: "ja", "en", "zh"）
	std::string name;     ///< 言語名（例: "Japanese", "English"）
	std::string fontPath; ///< その言語用のフォントパス
};

/// @brief 数と言語から複数形の種類を決める関数 (CLDR の one / few / many など)。
using PluralRuleFn = std::function<sgc::i18n::PluralCategory(int count, std::string_view language)>;

/// @brief 言語コードの基本部分 ("pt-BR" → "pt")。
[[nodiscard]] inline std::string_view baseLanguage(std::string_view code) noexcept
{
	const auto cut = code.find_first_of("-_");
	return cut == std::string_view::npos ? code : code.substr(0, cut);
}

/// @brief よく使う言語の複数形の決め方 (CLDR の整数の規則)。表に無い言語は英語と同じ扱い。
[[nodiscard]] inline sgc::i18n::PluralCategory defaultPluralCategory(int count, std::string_view language) noexcept
{
	using sgc::i18n::PluralCategory;
	const std::string_view base = baseLanguage(language);
	// INT_MIN の符号を反転すると int に収まらないので、桁の判定は 64 bit で行う
	const long long n = count < 0 ? -static_cast<long long>(count) : count;
	if (base == "ja" || base == "zh" || base == "ko" || base == "th" || base == "vi" || base == "id")
	{
		return PluralCategory::Other;
	}
	const long long m10 = n % 10, m100 = n % 100;
	if (base == "ru" || base == "uk")
	{
		if (m10 == 1 && m100 != 11) { return PluralCategory::One; }
		return (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) ? PluralCategory::Few : PluralCategory::Many;
	}
	if (base == "pl")
	{
		if (n == 1) { return PluralCategory::One; }
		return (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) ? PluralCategory::Few : PluralCategory::Many;
	}
	if (base == "fr" || base == "pt") { return n <= 1 ? PluralCategory::One : PluralCategory::Other; }
	if (base == "ar")
	{
		if (n == 0) { return PluralCategory::Zero; }
		if (n == 1) { return PluralCategory::One; }
		if (n == 2) { return PluralCategory::Two; }
		if (m100 >= 3 && m100 <= 10) { return PluralCategory::Few; }
		return m100 >= 11 ? PluralCategory::Many : PluralCategory::Other;
	}
	return n == 1 ? PluralCategory::One : PluralCategory::Other;
}

[[nodiscard]] inline std::string_view pluralSuffix(sgc::i18n::PluralCategory c) noexcept
{
	switch (c)
	{
	case sgc::i18n::PluralCategory::Zero:  return "_zero";
	case sgc::i18n::PluralCategory::One:   return "_one";
	case sgc::i18n::PluralCategory::Two:   return "_two";
	case sgc::i18n::PluralCategory::Few:   return "_few";
	case sgc::i18n::PluralCategory::Many:  return "_many";
	case sgc::i18n::PluralCategory::Other: return "_other";
	}
	return "_other";
}

/// @brief 翻訳テーブル型
/// @details キー → (言語コード → 翻訳テキスト) のマッピング
using TranslationTable = std::map<std::string, std::map<std::string, std::string>>;

/// @brief ローカライゼーション管理クラス
/// @details JSON 翻訳ファイルの読み込み、言語切り替え、テキスト取得、
///          フォーマット置換、複数形処理を提供する。
///
/// @code
/// mitiru::LocalizationManager loc;
/// loc.loadTranslationsFromString(R"({
///     "languages": [
///         {"code":"en","name":"English","font":"fonts/default.ttf"},
///         {"code":"ja","name":"Japanese","font":"fonts/noto-jp.ttf"}
///     ],
///     "strings": {
///         "menu.start": {"en":"Start Game","ja":"ゲーム開始"},
///         "item.count": {"en":"{0} items","ja":"{0}個のアイテム"},
///         "item.count_one": {"en":"{0} item"}
///     }
/// })");
///
/// loc.setLanguage("ja");
/// std::string text = loc.t("menu.start");  // "ゲーム開始"
/// std::string fmt = loc.tf("item.count", 5);  // "5個のアイテム"
/// @endcode
class LocalizationManager
{
public:
	/// @brief JSON 文字列から翻訳データを読み込む
	/// @param jsonString JSON 文字列
	/// @return 読み込みに成功した場合 true
	bool loadTranslationsFromString(std::string_view jsonString)
	{
		try
		{
			auto j = nlohmann::json::parse(jsonString);
			return parseJson(j);
		}
		catch (const nlohmann::json::exception&)
		{
			return false;
		}
	}

	/// @brief JSON ファイルから翻訳データを読み込む
	/// @param jsonPath ファイルパス
	/// @return 読み込みに成功した場合 true
	bool loadTranslations(std::string_view jsonPath)
	{
		const std::string path{jsonPath};
		std::ifstream file(path);
		if (!file.is_open())
		{
			return false;
		}

		try
		{
			nlohmann::json j;
			file >> j;
			return parseJson(j);
		}
		catch (const nlohmann::json::exception&)
		{
			return false;
		}
	}

	/// @brief アクティブ言語を切り替える
	/// @param code 言語コード
	void setLanguage(std::string_view code)
	{
		m_currentLanguage = std::string(code);
	}

	/// @brief 現在のアクティブ言語コードを取得する
	/// @return 言語コード
	[[nodiscard]] const std::string& currentLanguage() const noexcept
	{
		return m_currentLanguage;
	}

	/// @brief 現在の言語で翻訳テキストを取得する
	/// @param key 翻訳キー
	/// @return 翻訳テキスト（フォールバック: en → キー自体）
	[[nodiscard]] std::string t(std::string_view key) const
	{
		return t(key, m_currentLanguage);
	}

	/// @brief 指定言語で翻訳テキストを取得する
	/// @param key 翻訳キー
	/// @param language 言語コード
	/// @return 翻訳テキスト。指定言語 → その基本部分 ("pt-BR" なら "pt") → 予備の言語 → キー自体の順で探す
	[[nodiscard]] std::string t(std::string_view key, std::string_view language) const
	{
		if (auto text = lookup(key, language)) { return *text; }
		return std::string(key);
	}

	/// @brief 訳が無い時に使う言語 (既定 "en")。
	void setFallbackLanguage(std::string_view code) { m_fallbackLanguage = std::string(code); }
	[[nodiscard]] const std::string& fallbackLanguage() const noexcept { return m_fallbackLanguage; }

	/// @brief 複数形の決め方を差し替える (既定は defaultPluralCategory)。
	void setPluralRule(PluralRuleFn rule) { m_pluralRule = std::move(rule); }

	/// @brief language の訳が無いキー (予備の言語に頼っているもの) を並べる。翻訳の抜けを出荷前に見つける用。
	[[nodiscard]] std::vector<std::string> missingTranslations(std::string_view language) const
	{
		std::vector<std::string> out;
		for (const auto& [key, langMap] : m_translations)
		{
			const bool has = langMap.count(std::string(language)) != 0
			              || langMap.count(std::string(baseLanguage(language))) != 0;
			if (!has) { out.push_back(key); }
		}
		return out;
	}

	/// @brief language (とその基本部分) の訳だけを探し、予備の言語には落ちない。元の文言を書いた側が持っている
	/// 台本 (字幕・会話) は、訳が無ければ予備の言語でなく元の文言を出すために使う
	[[nodiscard]] std::optional<std::string> find(std::string_view key, std::string_view language) const
	{
		return lookup(key, language, false);
	}

	/// @brief キーが翻訳テーブルに存在するか確認する
	/// @param key 翻訳キー
	/// @return 存在する場合 true
	[[nodiscard]] bool hasKey(std::string_view key) const
	{
		return m_translations.find(std::string(key)) != m_translations.end();
	}

	/// @brief 利用可能な言語一覧を取得する
	/// @return 言語リスト
	[[nodiscard]] const std::vector<Language>& availableLanguages() const noexcept
	{
		return m_languages;
	}

	/// @brief フォーマット文字列: {0}, {1}, ... を引数で置換する
	/// @tparam Args 可変長引数型
	/// @param key 翻訳キー
	/// @param args 置換引数
	/// @return フォーマット済み文字列
	template <typename... Args>
	[[nodiscard]] std::string tf(std::string_view key, Args&&... args) const
	{
		std::string text = t(key);
		std::vector<std::string> argStrings;
		argStrings.reserve(sizeof...(args));
		(argStrings.push_back(toString(std::forward<Args>(args))), ...);

		for (std::size_t i = 0; i < argStrings.size(); ++i)
		{
			const std::string placeholder = "{" + std::to_string(i) + "}";
			std::string::size_type pos = 0;
			while ((pos = text.find(placeholder, pos)) != std::string::npos)
			{
				text.replace(pos, placeholder.size(), argStrings[i]);
				pos += argStrings[i].size();
			}
		}

		return text;
	}

	/// @brief 複数形選択: 今の言語の規則で count の種類を決め、key_one / key_few などがあればそれを使う
	/// @param key ベース翻訳キー (種類ごとのキーが無い時に使う)
	/// @param count 個数
	/// @return 複数形が適用された翻訳テキスト（{0} に count が入る）
	[[nodiscard]] std::string tp(std::string_view key, int count) const
	{
		const auto category = m_pluralRule ? m_pluralRule(count, m_currentLanguage)
		                                   : defaultPluralCategory(count, m_currentLanguage);
		const std::string variant = std::string(key) + std::string(pluralSuffix(category));
		// 今の言語の種類別のキー → 今の言語の元のキー → 予備の言語、の順。予備の言語の種類別の訳より、
		// 今の言語で書かれた訳を先に出す
		std::string text = lookup(variant, m_currentLanguage, false)
			.value_or(lookup(key, m_currentLanguage, false).value_or(t(key)));
		replacePlaceholder(text, 0, std::to_string(count));
		return text;
	}

	/// @brief 言語コードに対応するフォントパスを取得する
	/// @param code 言語コード
	/// @return フォントパス（見つからない場合は空文字列）
	[[nodiscard]] std::string fontForLanguage(std::string_view code) const
	{
		const std::string codeStr{code};
		const auto it = std::find_if(
			m_languages.begin(), m_languages.end(),
			[&codeStr](const Language& lang) { return lang.code == codeStr; });

		if (it != m_languages.end())
		{
			return it->fontPath;
		}
		return {};
	}

	/// @brief 翻訳テーブルへの参照を取得する
	/// @return 翻訳テーブル
	[[nodiscard]] const TranslationTable& translations() const noexcept
	{
		return m_translations;
	}

private:
	/// @brief JSON オブジェクトを解析して翻訳データを格納する
	/// @param j JSON オブジェクト
	/// @return 解析に成功した場合 true
	bool parseJson(const nlohmann::json& j)
	{
		// 言語定義を読み込み
		if (j.contains("languages") && j["languages"].is_array())
		{
			for (const auto& langObj : j["languages"])
			{
				Language lang;
				lang.code = langObj.value("code", "");
				lang.name = langObj.value("name", "");
				lang.fontPath = langObj.value("font", "");

				if (!lang.code.empty())
				{
					// 既存の同コードの言語を上書きしない（追加のみ）
					const bool exists = std::any_of(
						m_languages.begin(), m_languages.end(),
						[&lang](const Language& existing) { return existing.code == lang.code; });
					if (!exists)
					{
						m_languages.push_back(std::move(lang));
					}
				}
			}
		}

		// 翻訳文字列を読み込み
		if (j.contains("strings") && j["strings"].is_object())
		{
			for (const auto& [key, translations] : j["strings"].items())
			{
				if (translations.is_object())
				{
					auto& langMap = m_translations[key];
					for (const auto& [langCode, text] : translations.items())
					{
						if (text.is_string())
						{
							langMap[langCode] = text.get<std::string>();
						}
					}
				}
			}
		}

		// デフォルト言語が未設定の場合、最初の言語を設定
		if (m_currentLanguage.empty() && !m_languages.empty())
		{
			m_currentLanguage = m_languages.front().code;
		}

		return true;
	}

	/// @brief 値を文字列に変換するヘルパー
	/// @tparam T 値の型
	/// @param value 変換対象
	/// @return 文字列
	template <typename T>
	[[nodiscard]] static std::string toString(const T& value)
	{
		if constexpr (std::is_same_v<std::decay_t<T>, std::string>)
		{
			return value;
		}
		else if constexpr (std::is_same_v<std::decay_t<T>, const char*> ||
		                   std::is_same_v<std::decay_t<T>, char*>)
		{
			return std::string(value);
		}
		else if constexpr (std::is_same_v<std::decay_t<T>, std::string_view>)
		{
			return std::string(value);
		}
		else if constexpr (std::is_arithmetic_v<std::decay_t<T>>)
		{
			if constexpr (std::is_floating_point_v<std::decay_t<T>>)
			{
				std::ostringstream oss;
				oss << value;
				return oss.str();
			}
			else
			{
				return std::to_string(value);
			}
		}
		else
		{
			std::ostringstream oss;
			oss << value;
			return oss.str();
		}
	}

	/// @brief 文字列中のプレースホルダーを置換する
	/// @param text 対象文字列（in-place）
	/// @param index プレースホルダーインデックス
	/// @param replacement 置換テキスト
	static void replacePlaceholder(std::string& text, std::size_t index, const std::string& replacement)
	{
		const std::string placeholder = "{" + std::to_string(index) + "}";
		std::string::size_type pos = 0;
		while ((pos = text.find(placeholder, pos)) != std::string::npos)
		{
			text.replace(pos, placeholder.size(), replacement);
			pos += replacement.size();
		}
	}

	[[nodiscard]] std::optional<std::string> lookup(std::string_view key, std::string_view language,
	                                                bool useFallback = true) const
	{
		const auto it = m_translations.find(std::string(key));
		if (it == m_translations.end()) { return std::nullopt; }
		const std::string_view fallback = useFallback ? std::string_view(m_fallbackLanguage) : language;
		for (const std::string_view code : { language, baseLanguage(language), fallback })
		{
			const auto langIt = it->second.find(std::string(code));
			if (langIt != it->second.end()) { return langIt->second; }
		}
		return std::nullopt;
	}

	std::vector<Language> m_languages;         ///< 利用可能な言語リスト
	TranslationTable m_translations;           ///< 翻訳テーブル
	std::string m_currentLanguage;             ///< 現在のアクティブ言語コード
	std::string m_fallbackLanguage = "en";
	PluralRuleFn m_pluralRule;
};

} // namespace mitiru
