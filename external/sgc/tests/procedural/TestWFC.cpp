#include <catch2/catch_test_macros.hpp>
#include <sgc/procedural/WaveFunctionCollapse.hpp>

#include <set>
#include <unordered_map>

using namespace sgc::procedural;

TEST_CASE("WFC - empty tileset returns failure", "[procedural]")
{
	WFCConfig config;
	config.width = 5;
	config.height = 5;
	// tileSet is empty
	const auto result = solveWFC(config);
	REQUIRE_FALSE(result.success);
}

TEST_CASE("WFC - single tile fills entire grid", "[procedural]")
{
	WFCConfig config;
	config.width = 4;
	config.height = 3;
	config.tileSet = {42};
	// No rules needed: single tile has no constraints
	const auto result = solveWFC(config);
	REQUIRE(result.success);
	REQUIRE(result.grid.size() == 12);
	for (const auto tile : result.grid)
	{
		REQUIRE(tile == 42);
	}
}

TEST_CASE("WFC - two tiles with adjacency rules produce valid grid", "[procedural]")
{
	WFCConfig config;
	config.width = 5;
	config.height = 5;
	config.tileSet = {0, 1};

	// Tile 0 can be right of 0 or 1, tile 1 can be right of 0 or 1
	// (all directions, fully connected)
	for (int dir = 0; dir < 4; ++dir)
	{
		config.rules.push_back({0, 0, dir});
		config.rules.push_back({0, 1, dir});
		config.rules.push_back({1, 0, dir});
		config.rules.push_back({1, 1, dir});
	}
	config.seed = 12345;

	const auto result = solveWFC(config);
	REQUIRE(result.success);
	REQUIRE(result.grid.size() == 25);

	// All tiles should be 0 or 1
	for (const auto tile : result.grid)
	{
		REQUIRE((tile == 0 || tile == 1));
	}
}

TEST_CASE("WFC - seed reproducibility", "[procedural]")
{
	WFCConfig config;
	config.width = 6;
	config.height = 6;
	config.tileSet = {0, 1, 2};

	for (int dir = 0; dir < 4; ++dir)
	{
		for (TileId a = 0; a < 3; ++a)
		{
			for (TileId b = 0; b < 3; ++b)
			{
				config.rules.push_back({a, b, dir});
			}
		}
	}
	config.seed = 99999;

	const auto result1 = solveWFC(config);
	const auto result2 = solveWFC(config);

	REQUIRE(result1.success);
	REQUIRE(result2.success);
	REQUIRE(result1.grid == result2.grid);
}

TEST_CASE("WFC - large grid completes", "[procedural]")
{
	WFCConfig config;
	config.width = 20;
	config.height = 20;
	config.tileSet = {0, 1, 2};

	for (int dir = 0; dir < 4; ++dir)
	{
		for (TileId a = 0; a < 3; ++a)
		{
			for (TileId b = 0; b < 3; ++b)
			{
				config.rules.push_back({a, b, dir});
			}
		}
	}
	config.seed = 42;

	const auto result = solveWFC(config);
	REQUIRE(result.success);
	REQUIRE(result.grid.size() == 400);
}

TEST_CASE("WFC - contradiction returns failure gracefully", "[procedural]")
{
	WFCConfig config;
	config.width = 3;
	config.height = 3;
	config.tileSet = {0, 1};

	// Only tile 0 can be right of tile 0, only tile 1 can be right of tile 1
	// But tile 0 must be above tile 1 and tile 1 must be above tile 0
	// This creates contradictions in most configurations
	config.rules.push_back({0, 0, 0}); // 0 right of 0
	config.rules.push_back({1, 1, 0}); // 1 right of 1
	config.rules.push_back({0, 1, 1}); // 1 above 0
	config.rules.push_back({1, 0, 1}); // 0 above 1
	config.rules.push_back({0, 0, 2}); // 0 left of 0
	config.rules.push_back({1, 1, 2}); // 1 left of 1
	config.rules.push_back({0, 1, 3}); // 1 below 0
	config.rules.push_back({1, 0, 3}); // 0 below 1

	config.seed = 42;

	// This restrictive ruleset likely causes contradiction
	// Either way the function should not hang or crash
	const auto result = solveWFC(config);
	// Result may or may not succeed, but must not crash
	REQUIRE(result.width == 3);
	REQUIRE(result.height == 3);
}

TEST_CASE("WFC - weights bias tile selection", "[procedural]")
{
	WFCConfig config;
	config.width = 10;
	config.height = 10;
	config.tileSet = {0, 1};
	config.weights = {100.0f, 0.01f}; // Strongly prefer tile 0

	for (int dir = 0; dir < 4; ++dir)
	{
		config.rules.push_back({0, 0, dir});
		config.rules.push_back({0, 1, dir});
		config.rules.push_back({1, 0, dir});
		config.rules.push_back({1, 1, dir});
	}
	config.seed = 123;

	const auto result = solveWFC(config);
	REQUIRE(result.success);

	// Count tile occurrences
	int count0 = 0;
	int count1 = 0;
	for (const auto tile : result.grid)
	{
		if (tile == 0)
		{
			++count0;
		}
		else
		{
			++count1;
		}
	}

	// Tile 0 should dominate due to heavy weight
	REQUIRE(count0 > count1);
}

TEST_CASE("WFC - result dimensions match config", "[procedural]")
{
	WFCConfig config;
	config.width = 7;
	config.height = 13;
	config.tileSet = {5};

	const auto result = solveWFC(config);
	REQUIRE(result.width == 7);
	REQUIRE(result.height == 13);
}
