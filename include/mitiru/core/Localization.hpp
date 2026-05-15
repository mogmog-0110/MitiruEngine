#pragma once

/// @file Localization.hpp
/// @brief ローカライゼーション/i18nフレームワーク
/// @details JSON形式の翻訳テーブルを読み込み、言語切り替え・キー検索・
///          フォーマット文字列・複数形選択・フォント自動選択をサポートする。

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace mitiru
{

/// @brief 言語定義
struct Language
{
	std::string code;     ///< 言語コード（例: "ja", "en", "zh"）
	std::string name;     ///< 言語名（例: "Japanese", "English"）
	std::string fontPath; ///< その言語用のフォントパス
};

/// @brief 翻訳テーブル型
/// @details キー → (言語コード → 翻訳テキスト) のマッピング
using TranslationTable = std::map<std::string, std::map<std::string, std::string>>;

/// @brief ローカライゼーション管理クラス
/// @details JSON翻訳ファイルの読み込み、言語切り替え、テキスト取得、
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
	/// @brief JSON文字列から翻訳データを読み込む
	/// @param jsonString JSON文字列
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

	/// @brief JSONファイルから翻訳データを読み込む
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
	/// @return 翻訳テキスト（フォールバック: en → キー自体）
	[[nodiscard]] std::string t(std::string_view key, std::string_view language) const
	{
		const std::string keyStr{key};
		const auto it = m_translations.find(keyStr);
		if (it == m_translations.end())
		{
			return keyStr;
		}

		const auto& langMap = it->second;

		// 指定言語で検索
		const std::string langStr{language};
		const auto langIt = langMap.find(langStr);
		if (langIt != langMap.end())
		{
			return langIt->second;
		}

		// フォールバック: "en" を試行
		if (langStr != "en")
		{
			const auto enIt = langMap.find("en");
			if (enIt != langMap.end())
			{
				return enIt->second;
			}
		}

		// 最終フォールバック: キー自体を返す
		return keyStr;
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

	/// @brief 複数形選択: countが1なら key_one、それ以外は key を使用する
	/// @param key ベース翻訳キー
	/// @param count 個数
	/// @return 複数形が適用された翻訳テキスト（{0}にcountが入る）
	[[nodiscard]] std::string tp(std::string_view key, int count) const
	{
		const std::string baseKey{key};

		// count == 1 の場合、_one サフィックスを試行
		if (count == 1)
		{
			const std::string singularKey = baseKey + "_one";
			if (hasKey(singularKey))
			{
				std::string text = t(singularKey);
				replacePlaceholder(text, 0, std::to_string(count));
				return text;
			}
		}

		// デフォルト（複数形）
		std::string text = t(key);
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
	/// @brief JSONオブジェクトを解析して翻訳データを格納する
	/// @param j JSONオブジェクト
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

	std::vector<Language> m_languages;         ///< 利用可能な言語リスト
	TranslationTable m_translations;           ///< 翻訳テーブル
	std::string m_currentLanguage;             ///< 現在のアクティブ言語コード
};

} // namespace mitiru
