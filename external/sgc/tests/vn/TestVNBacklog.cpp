#include <catch2/catch_test_macros.hpp>

#include "sgc/vn/VNBacklog.hpp"

using namespace sgc::vn;

TEST_CASE("VNBacklog - Empty backlog", "[vn][vnbacklog]")
{
	VNBacklog log;
	REQUIRE(log.size() == 0);
	REQUIRE(log.empty());
	REQUIRE(log.maxSize() == VNBacklog::DEFAULT_MAX_SIZE);
}

TEST_CASE("VNBacklog - Add entry increases size", "[vn][vnbacklog]")
{
	VNBacklog log;
	log.addEntry({"Sakura", "Hello!", std::nullopt, 1.0f});
	REQUIRE(log.size() == 1);
	REQUIRE_FALSE(log.empty());
}

TEST_CASE("VNBacklog - GetEntries returns all entries in order", "[vn][vnbacklog]")
{
	VNBacklog log;
	log.addEntry({"A", "First", std::nullopt, 1.0f});
	log.addEntry({"B", "Second", std::nullopt, 2.0f});
	log.addEntry({"C", "Third", std::nullopt, 3.0f});

	auto entries = log.getEntries();
	REQUIRE(entries.size() == 3);
	REQUIRE(entries[0].speaker == "A");
	REQUIRE(entries[1].speaker == "B");
	REQUIRE(entries[2].speaker == "C");
}

TEST_CASE("VNBacklog - Max size enforced", "[vn][vnbacklog]")
{
	VNBacklog log(3);

	log.addEntry({"A", "1", std::nullopt, 1.0f});
	log.addEntry({"B", "2", std::nullopt, 2.0f});
	log.addEntry({"C", "3", std::nullopt, 3.0f});
	log.addEntry({"D", "4", std::nullopt, 4.0f});

	REQUIRE(log.size() == 3);
	auto entries = log.getEntries();
	REQUIRE(entries[0].speaker == "B");
	REQUIRE(entries[2].speaker == "D");
}

TEST_CASE("VNBacklog - GetRecentEntries returns last N", "[vn][vnbacklog]")
{
	VNBacklog log;
	log.addEntry({"A", "1", std::nullopt, 1.0f});
	log.addEntry({"B", "2", std::nullopt, 2.0f});
	log.addEntry({"C", "3", std::nullopt, 3.0f});

	auto recent = log.getRecentEntries(2);
	REQUIRE(recent.size() == 2);
	REQUIRE(recent[0].speaker == "B");
	REQUIRE(recent[1].speaker == "C");
}

TEST_CASE("VNBacklog - GetRecentEntries with large count returns all", "[vn][vnbacklog]")
{
	VNBacklog log;
	log.addEntry({"A", "1", std::nullopt, 1.0f});

	auto recent = log.getRecentEntries(100);
	REQUIRE(recent.size() == 1);
}

TEST_CASE("VNBacklog - Search finds matching entries", "[vn][vnbacklog]")
{
	VNBacklog log;
	log.addEntry({"Sakura", "Hello world", std::nullopt, 1.0f});
	log.addEntry({"Taro", "Goodbye", std::nullopt, 2.0f});
	log.addEntry({"Sakura", "Hello again", std::nullopt, 3.0f});

	auto results = log.search("Hello");
	REQUIRE(results.size() == 2);
	REQUIRE(results[0].text == "Hello world");
	REQUIRE(results[1].text == "Hello again");
}

TEST_CASE("VNBacklog - Search by speaker name", "[vn][vnbacklog]")
{
	VNBacklog log;
	log.addEntry({"Sakura", "Text 1", std::nullopt, 1.0f});
	log.addEntry({"Taro", "Text 2", std::nullopt, 2.0f});

	auto results = log.search("Sakura");
	REQUIRE(results.size() == 1);
	REQUIRE(results[0].speaker == "Sakura");
}

TEST_CASE("VNBacklog - Choice text stored correctly", "[vn][vnbacklog]")
{
	VNBacklog log;
	log.addEntry({"", "", "Yes", 1.0f});

	auto entries = log.getEntries();
	REQUIRE(entries[0].choiceText.has_value());
	REQUIRE(entries[0].choiceText.value() == "Yes");
}

TEST_CASE("VNBacklog - Clear empties backlog", "[vn][vnbacklog]")
{
	VNBacklog log;
	log.addEntry({"A", "1", std::nullopt, 1.0f});
	log.addEntry({"B", "2", std::nullopt, 2.0f});
	log.clear();
	REQUIRE(log.empty());
}
