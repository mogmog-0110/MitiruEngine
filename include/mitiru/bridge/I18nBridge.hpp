#pragma once

/// @file I18nBridge.hpp
/// @brief sgc 多言語対応統合ブリッジ
/// @details sgcのLanguageManagerとStringTableをMitiruエンジンに統合する。
///          言語の登録・切り替え・翻訳テキスト取得を管理する。

#include <string>
#include <vector>

#include <sgc/i18n/LanguageManager.hpp>
#include <sgc/i18n/StringTable.hpp>

namespace mitiru::bridge
{

/// @brief sgc 多言語対応統合ブリッジ
/// @details 複数言語の文字列テーブルを管理し、動的な言語切り替えを提供する。
///
/// @code
/// mitiru::bridge::I18nBridge i18n;
///
/// sgc::i18n::StringTable en;
/// en.addEntry("greeting", "Hello!");
/// i18n.loadLanguage("en", std::move(en));
///
/// sgc::i18n::StringTable ja;
/// ja.addEntry("greeting", "こんにちは！");
/// i18n.loadLanguage("ja", std::move(ja));
///
/// i18n.setCurrentLanguage("ja");
/// auto text = i18n.translate("greeting");  // => "こんにちは！"
/// @endcode
class I18nBridge
{
public:
	/// @brief 言語を登録する
	/// @param langCode 言語コード（例: "en", "ja"）
	/// @param table 文字列テーブル
	void loadLanguage(const std::string& langCode, sgc::i18n::StringTable table)
	{
		m_manager.registerLanguage(langCode, std::move(table));
	}

	/// @brief 現在の言語を切り替える
	/// @param langCode 言語コード
	void setCurrentLanguage(const std::string& langCode)
	{
		m_manager.setLanguage(langCode);
	}

	/// @brief キーに対応する翻訳テキストを取得する
	/// @param key キー文字列
	/// @return 翻訳テキスト（言語未設定またはキー不在なら"[MISSING]"）
	[[nodiscard]] std::string translate(const std::string& key) const
	{
		return m_manager.get(key);
	}

	/// @brief 現在の言語コードを取得する
	/// @return 言語コード（未設定なら空文字列）
	[[nodiscard]] std::string currentLanguage() const
	{
		return m_manager.currentLanguage();
	}

	/// @brief 登録済み言語コード一覧を取得する
	/// @return 言語コードのベクター
	[[nodiscard]] std::vector<std::string> availableLanguages() const
	{
		return m_manager.availableLanguages();
	}

	// ── シリアライズ ──────────────────────────────────────────

	/// @brief 多言語状態をJSON文字列として返す
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		const auto langs = m_manager.availableLanguages();

		std::string json;
		json += "{";
		json += "\"currentLanguage\":\"" + m_manager.currentLanguage() + "\"";
		json += ",\"languageCount\":" + std::to_string(langs.size());
		json += ",\"languages\":[";

		bool first = true;
		for (const auto& lang : langs)
		{
			if (!first) json += ",";
			json += "\"" + lang + "\"";
			first = false;
		}

		json += "]";
		json += "}";
		return json;
	}

private:
	sgc::i18n::LanguageManager m_manager;  ///< 言語マネージャ
};

} // namespace mitiru::bridge
