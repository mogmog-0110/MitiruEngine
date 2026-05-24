#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <sgc/procedural/TerrainGenerator.hpp>

using namespace sgc::procedural;

TEST_CASE("TerrainGenerator - generates correct dimensions", "[procedural]")
{
	TerrainConfig config;
	config.width = 32;
	config.height = 24;
	const auto result = generateTerrain(config);

	REQUIRE(result.width == 32);
	REQUIRE(result.height == 24);
	REQUIRE(result.heightmap.size() == 24);
	REQUIRE(result.heightmap[0].size() == 32);
	REQUIRE(result.moisture.size() == 24);
	REQUIRE(result.biomes.size() == 24);
}

TEST_CASE("TerrainGenerator - heightmap values in [0,1]", "[procedural]")
{
	TerrainConfig config;
	config.width = 32;
	config.height = 32;
	const auto result = generateTerrain(config);

	for (const auto& row : result.heightmap)
	{
		for (const float h : row)
		{
			REQUIRE(h >= 0.0f);
			REQUIRE(h <= 1.0f);
		}
	}
}

TEST_CASE("TerrainGenerator - moisture values in [0,1]", "[procedural]")
{
	TerrainConfig config;
	config.width = 16;
	config.height = 16;
	const auto result = generateTerrain(config);

	for (const auto& row : result.moisture)
	{
		for (const float m : row)
		{
			REQUIRE(m >= 0.0f);
			REQUIRE(m <= 1.0f);
		}
	}
}

TEST_CASE("TerrainGenerator - classifyBiome returns correct types", "[procedural]")
{
	REQUIRE(classifyBiome(0.1f, 0.5f) == BiomeType::Water);
	REQUIRE(classifyBiome(0.35f, 0.5f) == BiomeType::Sand);
	REQUIRE(classifyBiome(0.5f, 0.3f) == BiomeType::Grass);
	REQUIRE(classifyBiome(0.5f, 0.7f) == BiomeType::Forest);
	REQUIRE(classifyBiome(0.85f, 0.5f) == BiomeType::Mountain);
	REQUIRE(classifyBiome(0.95f, 0.5f) == BiomeType::Snow);
}

TEST_CASE("TerrainGenerator - same seed is deterministic", "[procedural]")
{
	TerrainConfig config;
	config.width = 16;
	config.height = 16;
	config.seed = 42;

	const auto result1 = generateTerrain(config);
	const auto result2 = generateTerrain(config);

	for (size_t y = 0; y < 16; ++y)
	{
		for (size_t x = 0; x < 16; ++x)
		{
			REQUIRE(result1.heightmap[y][x] == result2.heightmap[y][x]);
		}
	}
}

TEST_CASE("TerrainGenerator - different seeds produce different terrain", "[procedural]")
{
	TerrainConfig config;
	config.width = 16;
	config.height = 16;

	config.seed = 1;
	const auto result1 = generateTerrain(config);

	config.seed = 9999;
	const auto result2 = generateTerrain(config);

	bool different = false;
	for (size_t y = 0; y < 16 && !different; ++y)
	{
		for (size_t x = 0; x < 16 && !different; ++x)
		{
			if (result1.heightmap[y][x] != result2.heightmap[y][x])
			{
				different = true;
			}
		}
	}
	REQUIRE(different);
}

TEST_CASE("TerrainGenerator - biome map matches heightmap", "[procedural]")
{
	TerrainConfig config;
	config.width = 16;
	config.height = 16;
	const auto result = generateTerrain(config);

	for (size_t y = 0; y < 16; ++y)
	{
		for (size_t x = 0; x < 16; ++x)
		{
			const auto expected = classifyBiome(
				result.heightmap[y][x], result.moisture[y][x], config);
			REQUIRE(result.biomes[y][x] == expected);
		}
	}
}
