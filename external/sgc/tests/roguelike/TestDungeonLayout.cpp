#include <catch2/catch_test_macros.hpp>

#include "sgc/roguelike/DungeonLayout.hpp"

using namespace sgc::roguelike;

TEST_CASE("DungeonLayout - Empty layout", "[roguelike][dungeon]")
{
	DungeonLayout layout;
	REQUIRE(layout.roomCount() == 0);
	REQUIRE(layout.corridorCount() == 0);
}

TEST_CASE("DungeonLayout - Add room and retrieve", "[roguelike][dungeon]")
{
	DungeonLayout layout;
	Room room;
	room.id = 0;
	room.bounds = {10, 10, 5, 5};
	layout.addRoom(room);

	REQUIRE(layout.roomCount() == 1);
	const auto* r = layout.getRoom(0);
	REQUIRE(r != nullptr);
	REQUIRE(r->bounds.position.x == 10);
	REQUIRE(r->bounds.size.x == 5);
}

TEST_CASE("DungeonLayout - GetRoom returns nullptr for invalid id", "[roguelike][dungeon]")
{
	DungeonLayout layout;
	REQUIRE(layout.getRoom(99) == nullptr);
}

TEST_CASE("DungeonLayout - Room center calculation", "[roguelike][dungeon]")
{
	Room room;
	room.bounds = {10, 20, 6, 4};
	auto c = room.center();
	REQUIRE(c.x == 13);
	REQUIRE(c.y == 22);
}

TEST_CASE("DungeonLayout - Corridor connectivity check", "[roguelike][dungeon]")
{
	DungeonLayout layout;

	Room r0; r0.id = 0; r0.bounds = {0, 0, 5, 5};
	Room r1; r1.id = 1; r1.bounds = {10, 0, 5, 5};
	layout.addRoom(r0);
	layout.addRoom(r1);

	Corridor c;
	c.fromRoomId = 0;
	c.toRoomId = 1;
	c.path = {{5, 2}, {6, 2}, {7, 2}, {8, 2}, {9, 2}};
	layout.addCorridor(c);

	REQUIRE(layout.isConnected(0, 1));
	REQUIRE(layout.isConnected(1, 0));  // Bidirectional
	REQUIRE_FALSE(layout.isConnected(0, 2));
}

TEST_CASE("DungeonLayout - GenerateLayout produces rooms", "[roguelike][dungeon]")
{
	auto layout = generateLayout(80, 50, 8, 42);

	REQUIRE(layout.roomCount() > 0);
	REQUIRE(layout.corridorCount() > 0);

	// All rooms should have valid bounds
	for (const auto& room : layout.rooms())
	{
		REQUIRE(room.bounds.size.x > 0);
		REQUIRE(room.bounds.size.y > 0);
		REQUIRE(room.bounds.position.x >= 0);
		REQUIRE(room.bounds.position.y >= 0);
	}
}

TEST_CASE("DungeonLayout - GenerateLayout rooms within map bounds", "[roguelike][dungeon]")
{
	const int w = 60;
	const int h = 40;
	auto layout = generateLayout(w, h, 6, 123);

	for (const auto& room : layout.rooms())
	{
		REQUIRE(room.bounds.position.x >= 0);
		REQUIRE(room.bounds.position.y >= 0);
		REQUIRE(room.bounds.position.x + room.bounds.size.x <= w);
		REQUIRE(room.bounds.position.y + room.bounds.size.y <= h);
	}
}

TEST_CASE("DungeonLayout - GenerateLayout deterministic with same seed", "[roguelike][dungeon]")
{
	auto layout1 = generateLayout(80, 50, 8, 42);
	auto layout2 = generateLayout(80, 50, 8, 42);

	REQUIRE(layout1.roomCount() == layout2.roomCount());
	REQUIRE(layout1.corridorCount() == layout2.corridorCount());

	for (std::size_t i = 0; i < layout1.roomCount(); ++i)
	{
		REQUIRE(layout1.rooms()[i].bounds == layout2.rooms()[i].bounds);
	}
}

TEST_CASE("DungeonLayout - GenerateLayout different seeds produce different results", "[roguelike][dungeon]")
{
	auto layout1 = generateLayout(80, 50, 8, 42);
	auto layout2 = generateLayout(80, 50, 8, 99);

	// Very unlikely to produce identical layouts
	bool anyDifferent = false;
	if (layout1.roomCount() != layout2.roomCount())
	{
		anyDifferent = true;
	}
	else
	{
		for (std::size_t i = 0; i < layout1.roomCount(); ++i)
		{
			if (!(layout1.rooms()[i].bounds == layout2.rooms()[i].bounds))
			{
				anyDifferent = true;
				break;
			}
		}
	}
	REQUIRE(anyDifferent);
}

TEST_CASE("DungeonLayout - Corridor has non-empty path", "[roguelike][dungeon]")
{
	auto layout = generateLayout(80, 50, 8, 42);

	for (const auto& corridor : layout.corridors())
	{
		REQUIRE_FALSE(corridor.path.empty());
	}
}
