#include <catch2/catch_test_macros.hpp>

#include "sgc/i18n/StringTable.hpp"

using sgc::i18n::StringTable;

TEST_CASE("StringTable - add and get entries", "[i18n][StringTable]")
{
	StringTable table;
	table.addEntry("greeting", "Hello!");
	table.addEntry("farewell", "Goodbye!");

	REQUIRE(table.get("greeting") == "Hello!");
	REQUIRE(table.get("farewell") == "Goodbye!");
	REQUIRE(table.size() == 2);
}

TEST_CASE("StringTable - missing key returns MISSING marker", "[i18n][StringTable]")
{
	StringTable table;
	REQUIRE(table.get("nonexistent") == "[MISSING]");
}

TEST_CASE("StringTable - hasKey", "[i18n][StringTable]")
{
	StringTable table;
	table.addEntry("title", "My Game");

	REQUIRE(table.hasKey("title"));
	REQUIRE_FALSE(table.hasKey("subtitle"));
}

TEST_CASE("StringTable - format with placeholders", "[i18n][StringTable]")
{
	StringTable table;
	table.addEntry("welcome", "Welcome, {0}!");
	table.addEntry("score", "Player {0}: {1} pts");

	REQUIRE(table.format("welcome", {"Alice"}) == "Welcome, Alice!");
	REQUIRE(table.format("score", {"Bob", "9999"}) == "Player Bob: 9999 pts");
}

TEST_CASE("StringTable - format with repeated placeholder", "[i18n][StringTable]")
{
	StringTable table;
	table.addEntry("repeat", "{0} and {0}");

	REQUIRE(table.format("repeat", {"X"}) == "X and X");
}

TEST_CASE("StringTable - load from DataNode", "[i18n][StringTable]")
{
	auto node = sgc::data::DataNode::parse(R"({
		"title": "RPG Quest",
		"start": "New Game",
		"continue": "Continue"
	})");

	StringTable table;
	table.load(node);

	REQUIRE(table.size() == 3);
	REQUIRE(table.get("title") == "RPG Quest");
	REQUIRE(table.get("start") == "New Game");
	REQUIRE(table.get("continue") == "Continue");
}

TEST_CASE("StringTable - clear removes all entries", "[i18n][StringTable]")
{
	StringTable table;
	table.addEntry("a", "1");
	table.addEntry("b", "2");
	REQUIRE(table.size() == 2);

	table.clear();
	REQUIRE(table.size() == 0);
	REQUIRE_FALSE(table.hasKey("a"));
}
