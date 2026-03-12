#include <catch2/catch_test_macros.hpp>

#include <mitiru/editor/AssetBrowser.hpp>

TEST_CASE("AssetBrowser default state", "[mitiru][editor]")
{
	mitiru::editor::AssetBrowser browser;
	REQUIRE(browser.rootPath().empty());
	REQUIRE(browser.currentPath().empty());
	REQUIRE(browser.entries().empty());
	REQUIRE(browser.searchQuery().empty());
}

TEST_CASE("AssetBrowser setRootPath sets both root and current", "[mitiru][editor]")
{
	mitiru::editor::AssetBrowser browser;
	browser.setRootPath("/assets");
	REQUIRE(browser.rootPath() == "/assets");
	REQUIRE(browser.currentPath() == "/assets");
}

TEST_CASE("AssetBrowser navigate appends subdir", "[mitiru][editor]")
{
	mitiru::editor::AssetBrowser browser;
	browser.setRootPath("/assets");
	browser.navigate("textures");
	REQUIRE(browser.currentPath() == "/assets/textures");
}

TEST_CASE("AssetBrowser navigate multiple levels", "[mitiru][editor]")
{
	mitiru::editor::AssetBrowser browser;
	browser.setRootPath("/assets");
	browser.navigate("textures");
	browser.navigate("characters");
	REQUIRE(browser.currentPath() == "/assets/textures/characters");
}

TEST_CASE("AssetBrowser goUp returns to parent", "[mitiru][editor]")
{
	mitiru::editor::AssetBrowser browser;
	browser.setRootPath("/assets");
	browser.navigate("textures");
	browser.navigate("characters");

	REQUIRE(browser.goUp());
	REQUIRE(browser.currentPath() == "/assets/textures");

	REQUIRE(browser.goUp());
	REQUIRE(browser.currentPath() == "/assets");
}

TEST_CASE("AssetBrowser goUp at root returns false", "[mitiru][editor]")
{
	mitiru::editor::AssetBrowser browser;
	browser.setRootPath("/assets");
	REQUIRE_FALSE(browser.goUp());
	REQUIRE(browser.currentPath() == "/assets");
}

TEST_CASE("AssetBrowser type filter", "[mitiru][editor]")
{
	mitiru::editor::AssetBrowser browser;
	browser.setRootPath("/assets");

	std::vector<mitiru::editor::AssetEntry> entries;
	entries.push_back({"/assets/hero.png", mitiru::editor::AssetType::Texture, 1024, 0, false});
	entries.push_back({"/assets/bgm.ogg", mitiru::editor::AssetType::Audio, 2048, 0, false});
	entries.push_back({"/assets/sword.mesh", mitiru::editor::AssetType::Mesh, 512, 0, false});
	browser.setEntries(std::move(entries));

	browser.setTypeFilter(mitiru::editor::AssetType::Texture);
	auto filtered = browser.getFilteredEntries();
	REQUIRE(filtered.size() == 1);
	REQUIRE(filtered[0].path == "/assets/hero.png");
}

TEST_CASE("AssetBrowser clear type filter shows all", "[mitiru][editor]")
{
	mitiru::editor::AssetBrowser browser;
	browser.setRootPath("/assets");

	std::vector<mitiru::editor::AssetEntry> entries;
	entries.push_back({"/assets/a.png", mitiru::editor::AssetType::Texture, 100, 0, false});
	entries.push_back({"/assets/b.ogg", mitiru::editor::AssetType::Audio, 200, 0, false});
	browser.setEntries(std::move(entries));

	browser.setTypeFilter(mitiru::editor::AssetType::Texture);
	REQUIRE(browser.getFilteredEntries().size() == 1);

	browser.clearTypeFilter();
	REQUIRE(browser.getFilteredEntries().size() == 2);
}

TEST_CASE("AssetBrowser search filter", "[mitiru][editor]")
{
	mitiru::editor::AssetBrowser browser;
	browser.setRootPath("/assets");

	std::vector<mitiru::editor::AssetEntry> entries;
	entries.push_back({"/assets/hero_walk.png", mitiru::editor::AssetType::Texture, 100, 0, false});
	entries.push_back({"/assets/hero_idle.png", mitiru::editor::AssetType::Texture, 100, 0, false});
	entries.push_back({"/assets/enemy_walk.png", mitiru::editor::AssetType::Texture, 100, 0, false});
	browser.setEntries(std::move(entries));

	browser.setSearchQuery("hero");
	auto filtered = browser.getFilteredEntries();
	REQUIRE(filtered.size() == 2);
}

TEST_CASE("AssetBrowser filteredCount", "[mitiru][editor]")
{
	mitiru::editor::AssetBrowser browser;
	browser.setRootPath("/assets");

	std::vector<mitiru::editor::AssetEntry> entries;
	entries.push_back({"/assets/a.png", mitiru::editor::AssetType::Texture, 100, 0, false});
	entries.push_back({"/assets/b.png", mitiru::editor::AssetType::Texture, 200, 0, false});
	browser.setEntries(std::move(entries));

	REQUIRE(browser.filteredCount() == 2);
}

TEST_CASE("AssetBrowser path-based filtering", "[mitiru][editor]")
{
	mitiru::editor::AssetBrowser browser;
	browser.setRootPath("/assets");

	std::vector<mitiru::editor::AssetEntry> entries;
	entries.push_back({"/assets/textures/a.png", mitiru::editor::AssetType::Texture, 100, 0, false});
	entries.push_back({"/assets/audio/b.ogg", mitiru::editor::AssetType::Audio, 200, 0, false});
	browser.setEntries(std::move(entries));

	browser.navigate("textures");
	auto filtered = browser.getFilteredEntries();
	REQUIRE(filtered.size() == 1);
	REQUIRE(filtered[0].path == "/assets/textures/a.png");
}

TEST_CASE("AssetEntry default values", "[mitiru][editor]")
{
	mitiru::editor::AssetEntry entry;
	REQUIRE(entry.path.empty());
	REQUIRE(entry.type == mitiru::editor::AssetType::Unknown);
	REQUIRE(entry.sizeBytes == 0);
	REQUIRE(entry.lastModified == 0);
	REQUIRE(entry.isDirectory == false);
}
