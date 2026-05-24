#include <catch2/catch_test_macros.hpp>

#include "sgc/vn/Backlog.hpp"

using namespace sgc::vn;

TEST_CASE("Backlog - Empty backlog has size zero", "[vn][backlog]")
{
	Backlog log;
	REQUIRE(log.size() == 0);
	REQUIRE(log.empty());
}

TEST_CASE("Backlog - Add entry increases size", "[vn][backlog]")
{
	Backlog log;
	log.addEntry({"Sakura", "Hello!", ""});
	REQUIRE(log.size() == 1);
	REQUIRE_FALSE(log.empty());

	log.addEntry({"Taro", "Hi!", ""});
	REQUIRE(log.size() == 2);
}

TEST_CASE("Backlog - Entries returns added entries in order", "[vn][backlog]")
{
	Backlog log;
	log.addEntry({"Sakura", "First", ""});
	log.addEntry({"Taro", "Second", ""});
	log.addEntry({"Sakura", "Third", ""});

	const auto& entries = log.entries();
	REQUIRE(entries.size() == 3);
	REQUIRE(entries[0].speaker == "Sakura");
	REQUIRE(entries[0].text == "First");
	REQUIRE(entries[1].speaker == "Taro");
	REQUIRE(entries[1].text == "Second");
	REQUIRE(entries[2].speaker == "Sakura");
	REQUIRE(entries[2].text == "Third");
}

TEST_CASE("Backlog - Max entries enforced oldest removed", "[vn][backlog]")
{
	Backlog log(3);  // max 3 entries

	log.addEntry({"A", "1", ""});
	log.addEntry({"B", "2", ""});
	log.addEntry({"C", "3", ""});
	REQUIRE(log.size() == 3);

	// Adding a 4th should remove the oldest
	log.addEntry({"D", "4", ""});
	REQUIRE(log.size() == 3);

	const auto& entries = log.entries();
	REQUIRE(entries[0].speaker == "B");
	REQUIRE(entries[0].text == "2");
	REQUIRE(entries[2].speaker == "D");
	REQUIRE(entries[2].text == "4");
}

TEST_CASE("Backlog - Clear empties backlog", "[vn][backlog]")
{
	Backlog log;
	log.addEntry({"Sakura", "Hello!", ""});
	log.addEntry({"Taro", "Hi!", ""});
	REQUIRE(log.size() == 2);

	log.clear();
	REQUIRE(log.size() == 0);
	REQUIRE(log.empty());
}

TEST_CASE("Backlog - Choice is recorded in entry", "[vn][backlog]")
{
	Backlog log;
	log.addEntry({"", "", "Yes"});

	const auto& entries = log.entries();
	REQUIRE(entries[0].choiceMade == "Yes");
}
