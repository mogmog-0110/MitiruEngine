/// @file TestStlAllocatorAdapter.cpp
/// @brief StlAllocatorAdapter tests

#include <catch2/catch_test_macros.hpp>

#include <list>
#include <vector>

#include "sgc/memory/StlAllocatorAdapter.hpp"
#include "sgc/memory/ArenaAllocator.hpp"
#include "sgc/memory/FreeListAllocator.hpp"
#include "sgc/memory/PoolAllocator.hpp"

TEST_CASE("StlAllocatorAdapter - ArenaAllocator with vector push_back", "[memory][stl-adapter]")
{
	sgc::ArenaAllocator arena(4096);
	auto alloc = sgc::makeStlAllocator<int>(arena);
	std::vector<int, decltype(alloc)> vec(alloc);

	vec.push_back(10);
	vec.push_back(20);
	vec.push_back(30);

	REQUIRE(vec.size() == 3);
	CHECK(vec[0] == 10);
	CHECK(vec[1] == 20);
	CHECK(vec[2] == 30);
}

TEST_CASE("StlAllocatorAdapter - PoolAllocator with list insert/erase", "[memory][stl-adapter]")
{
	// list uses node allocation (one node per element)
	sgc::PoolAllocator pool(64, alignof(std::max_align_t), 100);
	sgc::StlAllocatorAdapter<int, sgc::PoolAllocator> alloc(pool);
	std::list<int, decltype(alloc)> lst(alloc);

	lst.push_back(1);
	lst.push_back(2);
	lst.push_back(3);

	REQUIRE(lst.size() == 3);

	lst.erase(lst.begin());
	REQUIRE(lst.size() == 2);
	CHECK(lst.front() == 2);
}

TEST_CASE("StlAllocatorAdapter - FreeListAllocator with vector", "[memory][stl-adapter]")
{
	sgc::FreeListAllocator freelist(8192);
	auto alloc = sgc::makeStlAllocator<double>(freelist);
	std::vector<double, decltype(alloc)> vec(alloc);

	for (int i = 0; i < 50; ++i)
	{
		vec.push_back(static_cast<double>(i) * 1.5);
	}

	REQUIRE(vec.size() == 50);
	CHECK(vec[0] == 0.0);
	CHECK(vec[49] == 49.0 * 1.5);
}

TEST_CASE("StlAllocatorAdapter - rebind conversion", "[memory][stl-adapter]")
{
	sgc::ArenaAllocator arena(4096);
	sgc::StlAllocatorAdapter<int, sgc::ArenaAllocator> intAlloc(arena);

	// rebind to double
	using ReboundType = sgc::StlAllocatorAdapter<int, sgc::ArenaAllocator>::rebind<double>::other;
	ReboundType doubleAlloc(intAlloc);

	// Both should point to the same underlying allocator
	CHECK(doubleAlloc.getAllocator() == intAlloc.getAllocator());
}

TEST_CASE("StlAllocatorAdapter - equality comparison", "[memory][stl-adapter]")
{
	sgc::ArenaAllocator arena1(1024);
	sgc::ArenaAllocator arena2(1024);

	sgc::StlAllocatorAdapter<int, sgc::ArenaAllocator> alloc1a(arena1);
	sgc::StlAllocatorAdapter<int, sgc::ArenaAllocator> alloc1b(arena1);
	sgc::StlAllocatorAdapter<int, sgc::ArenaAllocator> alloc2(arena2);

	CHECK(alloc1a == alloc1b);   // same allocator
	CHECK_FALSE(alloc1a == alloc2);  // different allocators
}

TEST_CASE("StlAllocatorAdapter - makeStlAllocator helper", "[memory][stl-adapter]")
{
	sgc::FreeListAllocator freelist(4096);
	auto alloc = sgc::makeStlAllocator<int>(freelist);

	// Should be usable directly with STL container
	std::vector<int, decltype(alloc)> vec(alloc);
	vec.push_back(42);
	REQUIRE(vec.size() == 1);
	CHECK(vec[0] == 42);
}

TEST_CASE("StlAllocatorAdapter - allocate failure throws bad_alloc", "[memory][stl-adapter]")
{
	// Tiny arena that will run out of space
	sgc::ArenaAllocator arena(32);
	auto alloc = sgc::makeStlAllocator<int>(arena);

	// Allocating more than arena capacity should throw
	CHECK_THROWS_AS(alloc.allocate(1000), std::bad_alloc);
}

TEST_CASE("StlAllocatorAdapter - ArenaAllocator with vector of structs", "[memory][stl-adapter]")
{
	struct Point
	{
		float x;
		float y;
	};

	sgc::ArenaAllocator arena(4096);
	auto alloc = sgc::makeStlAllocator<Point>(arena);
	std::vector<Point, decltype(alloc)> points(alloc);

	points.push_back({1.0f, 2.0f});
	points.push_back({3.0f, 4.0f});

	REQUIRE(points.size() == 2);
	CHECK(points[0].x == 1.0f);
	CHECK(points[1].y == 4.0f);
}
