#include <catch2/catch_test_macros.hpp>
#include <sgc/tilemap/Tilemap.hpp>

using namespace sgc::tilemap;

TEST_CASE("TileLayer - construct with size and fill", "[tilemap]")
{
	TileLayer layer(8, 6, TileId{3});
	REQUIRE(layer.width() == 8);
	REQUIRE(layer.height() == 6);
	REQUIRE(layer.getTile(0, 0).value() == 3);
	REQUIRE(layer.getTile(7, 5).value() == 3);
}

TEST_CASE("TileLayer - default construct is empty", "[tilemap]")
{
	TileLayer layer;
	REQUIRE(layer.width() == 0);
	REQUIRE(layer.height() == 0);
	REQUIRE_FALSE(layer.getTile(0, 0).has_value());
}

TEST_CASE("TileLayer - setTile and getTile", "[tilemap]")
{
	TileLayer layer(4, 4);
	REQUIRE(layer.setTile(2, 3, TileId{42}));
	REQUIRE(layer.getTile(2, 3).value() == 42);
}

TEST_CASE("TileLayer - out of bounds returns nullopt/false", "[tilemap]")
{
	TileLayer layer(4, 4);
	REQUIRE_FALSE(layer.setTile(4, 0, TileId{1}));
	REQUIRE_FALSE(layer.setTile(0, 4, TileId{1}));
	REQUIRE_FALSE(layer.getTile(10, 10).has_value());
}

TEST_CASE("TileLayer - isInBounds", "[tilemap]")
{
	TileLayer layer(3, 5);
	REQUIRE(layer.isInBounds(0, 0));
	REQUIRE(layer.isInBounds(2, 4));
	REQUIRE_FALSE(layer.isInBounds(3, 0));
	REQUIRE_FALSE(layer.isInBounds(0, 5));
}

TEST_CASE("TileLayer - fill overwrites all tiles", "[tilemap]")
{
	TileLayer layer(3, 3, TileId{1});
	layer.fill(TileId{9});
	for (size_t y = 0; y < 3; ++y)
	{
		for (size_t x = 0; x < 3; ++x)
		{
			REQUIRE(layer.getTile(x, y).value() == 9);
		}
	}
}

TEST_CASE("TileLayer - resize preserves existing data", "[tilemap]")
{
	TileLayer layer(3, 3);
	layer.setTile(1, 1, TileId{7});
	layer.resize(5, 5, TileId{99});
	REQUIRE(layer.width() == 5);
	REQUIRE(layer.height() == 5);
	REQUIRE(layer.getTile(1, 1).value() == 7);
	REQUIRE(layer.getTile(4, 4).value() == 99);
}

TEST_CASE("TileLayer - resize shrink clips data", "[tilemap]")
{
	TileLayer layer(4, 4, TileId{5});
	layer.setTile(3, 3, TileId{10});
	layer.resize(2, 2);
	REQUIRE(layer.width() == 2);
	REQUIRE(layer.height() == 2);
	REQUIRE_FALSE(layer.getTile(3, 3).has_value());
}

TEST_CASE("Tilemap - addLayer and layerCount", "[tilemap]")
{
	Tilemap map;
	REQUIRE(map.layerCount() == 0);
	const auto idx = map.addLayer(10, 10);
	REQUIRE(idx == 0);
	REQUIRE(map.layerCount() == 1);
	map.addLayer(10, 10);
	REQUIRE(map.layerCount() == 2);
}

TEST_CASE("Tilemap - setTile and getTile through layers", "[tilemap]")
{
	Tilemap map;
	map.addLayer(8, 8);
	map.addLayer(8, 8);
	REQUIRE(map.setTile(0, 3, 4, TileId{11}));
	REQUIRE(map.setTile(1, 3, 4, TileId{22}));
	REQUIRE(map.getTile(0, 3, 4).value() == 11);
	REQUIRE(map.getTile(1, 3, 4).value() == 22);
}

TEST_CASE("Tilemap - invalid layer index returns nullopt", "[tilemap]")
{
	Tilemap map;
	map.addLayer(4, 4);
	REQUIRE_FALSE(map.getTile(5, 0, 0).has_value());
	REQUIRE_FALSE(map.setTile(5, 0, 0, TileId{1}));
}

TEST_CASE("Tilemap - clearColumn resets all layers", "[tilemap]")
{
	Tilemap map;
	map.addLayer(4, 4, TileId{1});
	map.addLayer(4, 4, TileId{2});
	map.clearColumn(2, 2);
	REQUIRE(map.getTile(0, 2, 2).value() == EMPTY_TILE);
	REQUIRE(map.getTile(1, 2, 2).value() == EMPTY_TILE);
}

TEST_CASE("Tilemap - getLayer returns valid pointer", "[tilemap]")
{
	Tilemap map;
	map.addLayer(5, 5, TileId{3});
	const auto* layer = map.getLayer(0);
	REQUIRE(layer != nullptr);
	REQUIRE(layer->width() == 5);
	REQUIRE(map.getLayer(1) == nullptr);
}
