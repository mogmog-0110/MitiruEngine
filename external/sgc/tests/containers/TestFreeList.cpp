#include <catch2/catch_test_macros.hpp>

#include "sgc/containers/FreeList.hpp"

#include <string>

using namespace sgc::containers;

TEST_CASE("FreeList - allocate returns valid handle", "[containers][FreeList]")
{
	FreeList<int, 4> pool;
	auto handle = pool.allocate(42);
	REQUIRE(handle.has_value());
	REQUIRE(pool.get(*handle) == 42);
}

TEST_CASE("FreeList - allocate fills pool to capacity", "[containers][FreeList]")
{
	FreeList<int, 3> pool;
	auto h0 = pool.allocate(10);
	auto h1 = pool.allocate(20);
	auto h2 = pool.allocate(30);
	REQUIRE(h0.has_value());
	REQUIRE(h1.has_value());
	REQUIRE(h2.has_value());
	REQUIRE(pool.full());
	REQUIRE(pool.size() == 3);

	auto h3 = pool.allocate(40);
	REQUIRE_FALSE(h3.has_value());
}

TEST_CASE("FreeList - free releases slot for reuse", "[containers][FreeList]")
{
	FreeList<int, 2> pool;
	auto h0 = pool.allocate(10);
	auto h1 = pool.allocate(20);
	REQUIRE(pool.full());

	REQUIRE(pool.free(*h0));
	REQUIRE_FALSE(pool.isAlive(*h0));
	REQUIRE(pool.size() == 1);

	auto h2 = pool.allocate(30);
	REQUIRE(h2.has_value());
	REQUIRE(pool.get(*h2) == 30);
}

TEST_CASE("FreeList - free invalid handle returns false", "[containers][FreeList]")
{
	FreeList<int, 4> pool;
	REQUIRE_FALSE(pool.free(0));
	REQUIRE_FALSE(pool.free(999));
}

TEST_CASE("FreeList - isAlive tracks liveness", "[containers][FreeList]")
{
	FreeList<int, 4> pool;
	auto h = pool.allocate(1);
	REQUIRE(pool.isAlive(*h));
	pool.free(*h);
	REQUIRE_FALSE(pool.isAlive(*h));
}

TEST_CASE("FreeList - clear destroys all elements", "[containers][FreeList]")
{
	FreeList<std::string, 4> pool;
	auto h0 = pool.allocate("hello");
	auto h1 = pool.allocate("world");
	REQUIRE(pool.size() == 2);

	pool.clear();
	REQUIRE(pool.empty());
	REQUIRE(pool.size() == 0);
	REQUIRE_FALSE(pool.isAlive(*h0));
	REQUIRE_FALSE(pool.isAlive(*h1));
}

TEST_CASE("FreeList - capacity is compile-time constant", "[containers][FreeList]")
{
	FreeList<int, 16> pool;
	REQUIRE(FreeList<int, 16>::capacity() == 16);
}

TEST_CASE("FreeList - multiple alloc-free cycles", "[containers][FreeList]")
{
	FreeList<int, 2> pool;

	for (int cycle = 0; cycle < 5; ++cycle)
	{
		auto h0 = pool.allocate(cycle * 10);
		auto h1 = pool.allocate(cycle * 10 + 1);
		REQUIRE(h0.has_value());
		REQUIRE(h1.has_value());
		REQUIRE(pool.full());

		pool.free(*h0);
		pool.free(*h1);
		REQUIRE(pool.empty());
	}
}

TEST_CASE("FreeList - move construct with string elements", "[containers][FreeList]")
{
	FreeList<std::string, 4> pool;
	auto h0 = pool.allocate("alpha");
	auto h1 = pool.allocate("beta");

	FreeList<std::string, 4> moved(std::move(pool));
	REQUIRE(moved.size() == 2);
	REQUIRE(moved.get(*h0) == "alpha");
	REQUIRE(moved.get(*h1) == "beta");
}

TEST_CASE("FreeList - const get access", "[containers][FreeList]")
{
	FreeList<int, 4> pool;
	auto h = pool.allocate(99);

	const auto& constPool = pool;
	REQUIRE(constPool.get(*h) == 99);
}
