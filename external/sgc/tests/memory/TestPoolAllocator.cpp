/// @file TestPoolAllocator.cpp
/// @brief PoolAllocator.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/memory/PoolAllocator.hpp"

TEST_CASE("PoolAllocator allocates blocks", "[memory][pool]")
{
	sgc::PoolAllocator pool(32, 8, 10);

	auto* ptr = pool.allocate(32);
	REQUIRE(ptr != nullptr);
	REQUIRE(pool.freeCount() == 9);
}

TEST_CASE("PoolAllocator deallocate returns block", "[memory][pool]")
{
	sgc::PoolAllocator pool(32, 8, 10);

	auto* ptr = pool.allocate(32);
	pool.deallocate(ptr);
	REQUIRE(pool.freeCount() == 10);
}

TEST_CASE("PoolAllocator returns nullptr when exhausted", "[memory][pool]")
{
	sgc::PoolAllocator pool(16, 8, 2);

	(void)pool.allocate(16);
	(void)pool.allocate(16);
	auto* ptr = pool.allocate(16);
	REQUIRE(ptr == nullptr);
}

TEST_CASE("PoolAllocator rejects oversized requests", "[memory][pool]")
{
	sgc::PoolAllocator pool(16, 8, 10);

	auto* ptr = pool.allocate(32);
	REQUIRE(ptr == nullptr);
}

TEST_CASE("PoolAllocator reset restores all blocks", "[memory][pool]")
{
	sgc::PoolAllocator pool(32, 8, 5);

	(void)pool.allocate(32);
	(void)pool.allocate(32);
	(void)pool.allocate(32);
	pool.reset();

	REQUIRE(pool.freeCount() == 5);
}

TEST_CASE("PoolAllocator info is correct", "[memory][pool]")
{
	sgc::PoolAllocator pool(64, 8, 20);
	REQUIRE(pool.blockCount() == 20);
	REQUIRE(pool.blockSize() == 64);
	REQUIRE(pool.freeCount() == 20);
}
