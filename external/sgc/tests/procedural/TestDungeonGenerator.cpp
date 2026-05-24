#include <catch2/catch_test_macros.hpp>
#include <sgc/procedural/DungeonGenerator.hpp>

using namespace sgc::procedural;

TEST_CASE("DungeonGenerator - default config produces rooms", "[procedural]")
{
	DungeonConfig config;
	const auto result = generateDungeon(config, 42);

	REQUIRE(result.width == config.mapWidth);
	REQUIRE(result.height == config.mapHeight);
	REQUIRE_FALSE(result.rooms.empty());
}

TEST_CASE("DungeonGenerator - tile grid has correct dimensions", "[procedural]")
{
	DungeonConfig config;
	config.mapWidth = 40;
	config.mapHeight = 30;
	const auto result = generateDungeon(config, 123);

	REQUIRE(result.tiles.size() == 30);
	for (const auto& row : result.tiles)
	{
		REQUIRE(row.size() == 40);
	}
}

TEST_CASE("DungeonGenerator - rooms are within map bounds", "[procedural]")
{
	DungeonConfig config;
	config.mapWidth = 60;
	config.mapHeight = 40;
	const auto result = generateDungeon(config, 99);

	for (const auto& room : result.rooms)
	{
		REQUIRE(room.x >= 0);
		REQUIRE(room.y >= 0);
		REQUIRE(room.x + room.width <= config.mapWidth);
		REQUIRE(room.y + room.height <= config.mapHeight);
	}
}

TEST_CASE("DungeonGenerator - room size respects config limits", "[procedural]")
{
	DungeonConfig config;
	config.minRoomSize = 4;
	config.maxRoomSize = 8;
	const auto result = generateDungeon(config, 77);

	for (const auto& room : result.rooms)
	{
		REQUIRE(room.width >= config.minRoomSize);
		REQUIRE(room.width <= config.maxRoomSize);
		REQUIRE(room.height >= config.minRoomSize);
		REQUIRE(room.height <= config.maxRoomSize);
	}
}

TEST_CASE("DungeonGenerator - corridors connect rooms", "[procedural]")
{
	DungeonConfig config;
	const auto result = generateDungeon(config, 55);

	// 2つ以上の部屋があれば通路も存在するはず
	if (result.rooms.size() >= 2)
	{
		REQUIRE_FALSE(result.corridors.empty());
	}
}

TEST_CASE("DungeonGenerator - floor tiles exist in rooms", "[procedural]")
{
	DungeonConfig config;
	config.mapWidth = 80;
	config.mapHeight = 50;
	const auto result = generateDungeon(config, 33);

	// 各部屋の中心がFloorであることを確認
	for (const auto& room : result.rooms)
	{
		const int cx = room.centerX();
		const int cy = room.centerY();
		REQUIRE(result.tiles[static_cast<size_t>(cy)][static_cast<size_t>(cx)] == TileType::Floor);
	}
}

TEST_CASE("DungeonGenerator - same seed produces same result", "[procedural]")
{
	DungeonConfig config;
	const auto result1 = generateDungeon(config, 42);
	const auto result2 = generateDungeon(config, 42);

	REQUIRE(result1.rooms.size() == result2.rooms.size());
	for (size_t i = 0; i < result1.rooms.size(); ++i)
	{
		REQUIRE(result1.rooms[i].x == result2.rooms[i].x);
		REQUIRE(result1.rooms[i].y == result2.rooms[i].y);
		REQUIRE(result1.rooms[i].width == result2.rooms[i].width);
		REQUIRE(result1.rooms[i].height == result2.rooms[i].height);
	}
}

TEST_CASE("DungeonGenerator - different seeds produce different results", "[procedural]")
{
	DungeonConfig config;
	const auto result1 = generateDungeon(config, 1);
	const auto result2 = generateDungeon(config, 9999);

	// 異なるシードで完全一致する確率は極めて低い
	bool different = (result1.rooms.size() != result2.rooms.size());
	if (!different && !result1.rooms.empty())
	{
		different = (result1.rooms[0].x != result2.rooms[0].x) ||
		            (result1.rooms[0].y != result2.rooms[0].y);
	}
	REQUIRE(different);
}

TEST_CASE("DungeonGenerator - Room center calculation", "[procedural]")
{
	Room room{10, 20, 30, 40};
	REQUIRE(room.centerX() == 25);
	REQUIRE(room.centerY() == 40);
}
