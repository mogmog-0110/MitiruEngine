#include <catch2/catch_test_macros.hpp>
#include <sgc/tilemap/AutoTile.hpp>

using namespace sgc::tilemap;

TEST_CASE("AutoTileSet - addRule and resolve", "[tilemap]")
{
	AutoTileSet tileSet;
	tileSet.addRule(0b00000000, TileId{1});
	tileSet.addRule(0b11111111, TileId{10});
	REQUIRE(tileSet.resolve(0b00000000) == 1);
	REQUIRE(tileSet.resolve(0b11111111) == 10);
}

TEST_CASE("AutoTileSet - unmatched mask returns default", "[tilemap]")
{
	AutoTileSet tileSet;
	tileSet.setDefaultTile(TileId{99});
	tileSet.addRule(0b00001111, TileId{5});
	REQUIRE(tileSet.resolve(0b11110000) == 99);
}

TEST_CASE("AutoTileSet - toCardinalMask extracts 4 directions", "[tilemap]")
{
	// N=bit0, E=bit2, S=bit4, W=bit6
	const NeighborMask full = 0b01010101; // N, E, S, W set
	const auto cardinal = AutoTileSet::toCardinalMask(full);
	REQUIRE(cardinal == 0b1111); // all 4 cardinal directions
}

TEST_CASE("computeNeighborMask - isolated tile has zero mask", "[tilemap]")
{
	TileLayer layer(3, 3, EMPTY_TILE);
	layer.setTile(1, 1, TileId{5});
	const auto mask = computeNeighborMask(layer, 1, 1, TileId{5});
	REQUIRE(mask == 0);
}

TEST_CASE("computeNeighborMask - surrounded tile has full mask", "[tilemap]")
{
	TileLayer layer(3, 3, TileId{5});
	const auto mask = computeNeighborMask(layer, 1, 1, TileId{5});
	REQUIRE(mask == 0b11111111);
}

TEST_CASE("resolveAutoTile - applies correct tile variant", "[tilemap]")
{
	Tilemap map;
	map.addLayer(3, 3, TileId{5});
	// 中央タイルは全方向隣接
	AutoTileSet tileSet;
	tileSet.addRule(0b11111111, TileId{50});
	tileSet.setDefaultTile(TileId{1});

	const auto resolved = resolveAutoTile(map, 0, 1, 1, TileId{5}, tileSet);
	REQUIRE(resolved == 50);
}

TEST_CASE("resolveAutoTile - non-target tile unchanged", "[tilemap]")
{
	Tilemap map;
	map.addLayer(3, 3, TileId{5});
	map.setTile(0, 1, 1, TileId{7}); // 中央を別IDに
	AutoTileSet tileSet;
	tileSet.addRule(0b11111111, TileId{50});

	const auto resolved = resolveAutoTile(map, 0, 1, 1, TileId{5}, tileSet);
	REQUIRE(resolved == 7); // 変換対象でないのでそのまま
}

TEST_CASE("applyAutoTiling - applies to entire layer", "[tilemap]")
{
	Tilemap map;
	map.addLayer(3, 3, TileId{5});
	AutoTileSet tileSet;
	tileSet.setDefaultTile(TileId{1});
	tileSet.addRule(0b11111111, TileId{50});

	const auto count = applyAutoTiling(map, 0, TileId{5}, tileSet);
	REQUIRE(count == 9);
	// 中央は全方向隣接→50、角は3方向隣接→default(1)
	REQUIRE(map.getTile(0, 1, 1).value() == 50);
}

TEST_CASE("AutoTileSet - ruleCount tracks additions", "[tilemap]")
{
	AutoTileSet tileSet;
	REQUIRE(tileSet.ruleCount() == 0);
	tileSet.addRule(0x00, TileId{1});
	tileSet.addRule(0xFF, TileId{2});
	REQUIRE(tileSet.ruleCount() == 2);
}
