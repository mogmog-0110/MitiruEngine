#include <catch2/catch_test_macros.hpp>
#include <algorithm>

#include "sgc/i18n/LanguageManager.hpp"

using sgc::i18n::LanguageManager;
using sgc::i18n::StringTable;

/// @brief テスト用ヘルパ: 英語テーブルを作成する
static StringTable makeEnglishTable()
{
	StringTable t;
	t.addEntry("greeting", "Hello!");
	t.addEntry("farewell", "Goodbye!");
	t.addEntry("score", "Score: {0}");
	return t;
}

/// @brief テスト用ヘルパ: 日本語テーブルを作成する
static StringTable makeJapaneseTable()
{
	StringTable t;
	t.addEntry("greeting", "こんにちは！");
	t.addEntry("farewell", "さようなら！");
	t.addEntry("score", "スコア: {0}");
	return t;
}

TEST_CASE("LanguageManager - no language set returns MISSING", "[i18n][LanguageManager]")
{
	LanguageManager mgr;
	REQUIRE(mgr.currentLanguage().empty());
	REQUIRE(mgr.get("greeting") == "[MISSING]");
	REQUIRE(mgr.currentTable() == nullptr);
}

TEST_CASE("LanguageManager - register and set language", "[i18n][LanguageManager]")
{
	LanguageManager mgr;
	mgr.registerLanguage("en", makeEnglishTable());
	REQUIRE(mgr.setLanguage("en"));

	REQUIRE(mgr.currentLanguage() == "en");
	REQUIRE(mgr.get("greeting") == "Hello!");
	REQUIRE(mgr.get("farewell") == "Goodbye!");
}

TEST_CASE("LanguageManager - switch between languages", "[i18n][LanguageManager]")
{
	LanguageManager mgr;
	mgr.registerLanguage("en", makeEnglishTable());
	mgr.registerLanguage("ja", makeJapaneseTable());

	mgr.setLanguage("en");
	REQUIRE(mgr.get("greeting") == "Hello!");

	mgr.setLanguage("ja");
	REQUIRE(mgr.get("greeting") == "こんにちは！");
	REQUIRE(mgr.currentLanguage() == "ja");
}

TEST_CASE("LanguageManager - set unknown language returns false", "[i18n][LanguageManager]")
{
	LanguageManager mgr;
	mgr.registerLanguage("en", makeEnglishTable());
	mgr.setLanguage("en");

	REQUIRE_FALSE(mgr.setLanguage("fr"));
	// Current language should remain unchanged
	REQUIRE(mgr.currentLanguage() == "en");
}

TEST_CASE("LanguageManager - format delegates to current table", "[i18n][LanguageManager]")
{
	LanguageManager mgr;
	mgr.registerLanguage("en", makeEnglishTable());
	mgr.setLanguage("en");

	REQUIRE(mgr.format("score", {"42"}) == "Score: 42");
}

TEST_CASE("LanguageManager - availableLanguages", "[i18n][LanguageManager]")
{
	LanguageManager mgr;
	mgr.registerLanguage("en", makeEnglishTable());
	mgr.registerLanguage("ja", makeJapaneseTable());

	auto langs = mgr.availableLanguages();
	std::sort(langs.begin(), langs.end());

	REQUIRE(langs.size() == 2);
	REQUIRE(langs[0] == "en");
	REQUIRE(langs[1] == "ja");
}

TEST_CASE("LanguageManager - currentTable returns valid pointer", "[i18n][LanguageManager]")
{
	LanguageManager mgr;
	mgr.registerLanguage("en", makeEnglishTable());
	mgr.setLanguage("en");

	const auto* table = mgr.currentTable();
	REQUIRE(table != nullptr);
	REQUIRE(table->get("greeting") == "Hello!");
}
