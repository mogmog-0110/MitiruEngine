/// @file TestArenaAllocator.cpp
/// @brief ArenaAllocator.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/memory/ArenaAllocator.hpp"

TEST_CASE("ArenaAllocator allocates memory", "[memory][arena]")
{
	sgc::ArenaAllocator arena(1024);

	auto* ptr = arena.allocate(64);
	REQUIRE(ptr != nullptr);
	REQUIRE(arena.used() > 0);
}

TEST_CASE("ArenaAllocator returns nullptr when full", "[memory][arena]")
{
	sgc::ArenaAllocator arena(64);

	(void)arena.allocate(64);
	auto* ptr = arena.allocate(1);
	REQUIRE(ptr == nullptr);
}

TEST_CASE("ArenaAllocator reset frees all", "[memory][arena]")
{
	sgc::ArenaAllocator arena(1024);

	(void)arena.allocate(256);
	(void)arena.allocate(256);
	arena.reset();

	REQUIRE(arena.used() == 0);
	REQUIRE(arena.remaining() == 1024);
}

TEST_CASE("ArenaAllocator create constructs objects", "[memory][arena]")
{
	sgc::ArenaAllocator arena(1024);

	struct Point { float x; float y; };
	auto* p = arena.create<Point>(3.0f, 4.0f);

	REQUIRE(p != nullptr);
	REQUIRE(p->x == 3.0f);
	REQUIRE(p->y == 4.0f);
}

TEST_CASE("ArenaAllocator respects alignment", "[memory][arena]")
{
	sgc::ArenaAllocator arena(1024);

	(void)arena.allocate(1);
	auto* ptr = arena.allocate(8, 16);

	REQUIRE(ptr != nullptr);
	auto addr = reinterpret_cast<std::uintptr_t>(ptr);
	REQUIRE(addr % 16 == 0);
}

TEST_CASE("ArenaAllocator capacity reporting", "[memory][arena]")
{
	sgc::ArenaAllocator arena(512);
	REQUIRE(arena.capacity() == 512);
	REQUIRE(arena.remaining() == 512);
	REQUIRE(arena.used() == 0);
}
