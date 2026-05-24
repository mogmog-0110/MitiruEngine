#pragma once

/// @file LanguageManager.hpp
/// @brief 多言語管理マネージャ
///
/// 複数言語のStringTableを管理し、動的な言語切り替えを提供する。
///
/// @code
/// sgc::i18n::LanguageManager langMgr;
///
/// sgc::i18n::StringTable en;
/// en.addEntry("greeting", "Hello!");
/// en.addEntry("farewell", "Goodbye!");
///
/// sgc::i18n::StringTable ja;
/// ja.addEntry("greeting", "こんにちは！");
/// ja.addEntry("farewell", "さようなら！");
///
/// langMgr.registerLanguage("en", std::move(en));
/// langMgr.registerLanguage("ja", std::move(ja));
/// langMgr.setLanguage("ja");
///
/// auto text = langMgr.get("greeting");  // => "こんにちは！"
/// @endcode

#include <string>
#include <unordered_map>
#include <vector>

#include "sgc/i18n/StringTable.hpp"

namespace sgc::i18n
{

/// @brief 多言語対応マネージャ
///
/// 言語コード（"en", "ja", "zh"等）でStringTableを管理し、
/// 実行時に言語を切り替えられる。
class LanguageManager
{
public:
	/// @brief 言語を登録する
	/// @param langCode 言語コード（例: "en", "ja"）
	/// @param table 文字列テーブル
	void registerLanguage(const std::string& langCode, StringTable table)
	{
		m_tables[langCode] = std::move(table);
	}

	/// @brief 現在の言語を切り替える
	/// @param langCode 言語コード
	/// @return 該当言語が登録されていればtrue
	bool setLanguage(const std::string& langCode)
	{
		if (m_tables.find(langCode) == m_tables.end())
		{
			return false;
		}
		m_currentLang = langCode;
		return true;
	}

	/// @brief 現在の言語コードを取得する
	/// @return 言語コード（未設定なら空文字列）
	[[nodiscard]] const std::string& currentLanguage() const noexcept
	{
		return m_currentLang;
	}

	/// @brief 現在の言語テーブルからテキストを取得する
	/// @param key キー文字列
	/// @return 翻訳テキスト（言語未設定またはキー不在なら"[MISSING]"）
	[[nodiscard]] const std::string& get(const std::string& key) const
	{
		const auto* table = currentTable();
		if (table == nullptr)
		{
			return MISSING_KEY;
		}
		return table->get(key);
	}

	/// @brief 現在の言語テーブルでフォーマット付きテキストを取得する
	/// @param key キー文字列
	/// @param args 置換引数のリスト
	/// @return フォーマット済みテキスト
	[[nodiscard]] std::string format(const std::string& key,
	                                 std::initializer_list<std::string> args) const
	{
		const auto* table = currentTable();
		if (table == nullptr)
		{
			return MISSING_KEY;
		}
		return table->format(key, args);
	}

	/// @brief 現在の言語テーブルを取得する
	/// @return テーブルへのポインタ（言語未設定ならnullptr）
	[[nodiscard]] const StringTable* currentTable() const
	{
		if (m_currentLang.empty()) return nullptr;
		const auto it = m_tables.find(m_currentLang);
		if (it == m_tables.end()) return nullptr;
		return &it->second;
	}

	/// @brief 登録済み言語コード一覧を取得する
	/// @return 言語コードのベクター
	[[nodiscard]] std::vector<std::string> availableLanguages() const
	{
		std::vector<std::string> langs;
		langs.reserve(m_tables.size());
		for (const auto& [code, table] : m_tables)
		{
			langs.push_back(code);
		}
		return langs;
	}

private:
	std::unordered_map<std::string, StringTable> m_tables;
	std::string m_currentLang;

	/// @brief 言語未設定時のフォールバック文字列
	static inline const std::string MISSING_KEY = "[MISSING]";
};

} // namespace sgc::i18n
