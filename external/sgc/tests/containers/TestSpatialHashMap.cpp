#include <catch2/catch_test_macros.hpp>

#include "sgc/containers/SpatialHashMap.hpp"

#include <algorithm>
#include <vector>

using namespace sgc::containers;

TEST_CASE("SpatialHashMap - insert and query single object", "[containers][SpatialHashMap]")
{
	SpatialHashMap<int> shm(10.0f);
	SpatialHashMap<int>::AABB box{5.0f, 5.0f, 15.0f, 15.0f};
	shm.insert(box, 42);

	auto results = shm.query(box);
	REQUIRE(results.size() == 1);
	REQUIRE(results[0] == 42);
}

TEST_CASE("SpatialHashMap - query returns empty for no overlap", "[containers][SpatialHashMap]")
{
	SpatialHashMap<int> shm(10.0f);
	shm.insert({0.0f, 0.0f, 5.0f, 5.0f}, 1);

	auto results = shm.query({100.0f, 100.0f, 110.0f, 110.0f});
	REQUIRE(results.empty());
}

TEST_CASE("SpatialHashMap - multiple objects in same cell", "[containers][SpatialHashMap]")
{
	SpatialHashMap<int> shm(100.0f);
	shm.insert({1.0f, 1.0f, 2.0f, 2.0f}, 1);
	shm.insert({3.0f, 3.0f, 4.0f, 4.0f}, 2);
	shm.insert({5.0f, 5.0f, 6.0f, 6.0f}, 3);

	auto results = shm.query({0.0f, 0.0f, 10.0f, 10.0f});
	REQUIRE(results.size() == 3);
}

TEST_CASE("SpatialHashMap - query deduplicates multi-cell objects", "[containers][SpatialHashMap]")
{
	SpatialHashMap<int> shm(10.0f);
	shm.insert({-5.0f, -5.0f, 15.0f, 15.0f}, 99);

	auto results = shm.query({-10.0f, -10.0f, 20.0f, 20.0f});
	REQUIRE(results.size() == 1);
	REQUIRE(results[0] == 99);
}

TEST_CASE("SpatialHashMap - negative coordinates work correctly", "[containers][SpatialHashMap]")
{
	SpatialHashMap<int> shm(10.0f);
	shm.insert({-20.0f, -20.0f, -10.0f, -10.0f}, 1);

	auto results = shm.query({-25.0f, -25.0f, -5.0f, -5.0f});
	REQUIRE(results.size() == 1);
	REQUIRE(results[0] == 1);
}

TEST_CASE("SpatialHashMap - clear removes all entries", "[containers][SpatialHashMap]")
{
	SpatialHashMap<int> shm(10.0f);
	shm.insert({0.0f, 0.0f, 5.0f, 5.0f}, 1);
	shm.insert({10.0f, 10.0f, 15.0f, 15.0f}, 2);

	shm.clear();
	REQUIRE(shm.empty());
	REQUIRE(shm.size() == 0);

	auto results = shm.query({-100.0f, -100.0f, 100.0f, 100.0f});
	REQUIRE(results.empty());
}

TEST_CASE("SpatialHashMap - size tracks insertions", "[containers][SpatialHashMap]")
{
	SpatialHashMap<int> shm(10.0f);
	REQUIRE(shm.size() == 0);
	REQUIRE(shm.empty());

	shm.insert({0.0f, 0.0f, 1.0f, 1.0f}, 1);
	REQUIRE(shm.size() == 1);
	shm.insert({2.0f, 2.0f, 3.0f, 3.0f}, 2);
	REQUIRE(shm.size() == 2);
}

TEST_CASE("SpatialHashMap - cellSize accessor returns configured value", "[containers][SpatialHashMap]")
{
	SpatialHashMap<int> shm(32.0f);
	REQUIRE(shm.cellSize() == 32.0f);
}

TEST_CASE("SpatialHashMap - double precision coordinates", "[containers][SpatialHashMap]")
{
	SpatialHashMap<int, double> shm(1.0);
	shm.insert({0.1, 0.1, 0.9, 0.9}, 1);
	shm.insert({1.1, 1.1, 1.9, 1.9}, 2);

	auto results = shm.query({0.0, 0.0, 0.5, 0.5});
	REQUIRE(results.size() == 1);
	REQUIRE(results[0] == 1);
}

TEST_CASE("SpatialHashMap - broad query finds all objects", "[containers][SpatialHashMap]")
{
	SpatialHashMap<int> shm(10.0f);
	for (int i = 0; i < 20; ++i)
	{
		float x = static_cast<float>(i * 15);
		shm.insert({x, 0.0f, x + 5.0f, 5.0f}, i);
	}

	auto results = shm.query({-10.0f, -10.0f, 500.0f, 50.0f});
	REQUIRE(results.size() == 20);
}
