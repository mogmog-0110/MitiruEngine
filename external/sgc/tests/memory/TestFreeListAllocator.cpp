/// @file TestFreeListAllocator.cpp
/// @brief FreeListAllocator.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/memory/FreeListAllocator.hpp"

TEST_CASE("FreeListAllocator basic allocate and deallocate", "[memory][freelist]")
{
	sgc::FreeListAllocator alloc(1024);
	REQUIRE(alloc.capacity() == 1024);
	REQUIRE(alloc.used() == 0);

	auto* ptr = alloc.allocate(64);
	REQUIRE(ptr != nullptr);
	REQUIRE(alloc.used() > 0);

	alloc.deallocate(ptr);
	REQUIRE(alloc.used() == 0);
}

TEST_CASE("FreeListAllocator create constructs object", "[memory][freelist]")
{
	sgc::FreeListAllocator alloc(1024);
	auto* val = alloc.create<int>(42);
	REQUIRE(val != nullptr);
	REQUIRE(*val == 42);
}

TEST_CASE("FreeListAllocator multiple allocations", "[memory][freelist]")
{
	sgc::FreeListAllocator alloc(1024);
	auto* a = alloc.create<int>(1);
	auto* b = alloc.create<int>(2);
	auto* c = alloc.create<int>(3);

	REQUIRE(a != nullptr);
	REQUIRE(b != nullptr);
	REQUIRE(c != nullptr);
	REQUIRE(*a == 1);
	REQUIRE(*b == 2);
	REQUIRE(*c == 3);
}

TEST_CASE("FreeListAllocator reuses freed memory", "[memory][freelist]")
{
	sgc::FreeListAllocator alloc(256);

	auto* a = alloc.allocate(64);
	REQUIRE(a != nullptr);

	alloc.deallocate(a);

	// 解放後に再割り当て可能
	auto* b = alloc.allocate(64);
	REQUIRE(b != nullptr);
}

TEST_CASE("FreeListAllocator coalescing adjacent blocks", "[memory][freelist]")
{
	sgc::FreeListAllocator alloc(1024);

	auto* a = alloc.allocate(64);
	auto* b = alloc.allocate(64);
	auto* c = alloc.allocate(64);

	// 連続する3ブロックを解放 → 結合されるはず
	alloc.deallocate(a);
	alloc.deallocate(c);
	alloc.deallocate(b);

	REQUIRE(alloc.used() == 0);

	// 結合後、大きなブロックを割り当て可能
	auto* big = alloc.allocate(256);
	REQUIRE(big != nullptr);
}

TEST_CASE("FreeListAllocator BestFit policy", "[memory][freelist]")
{
	sgc::FreeListAllocator alloc(1024, sgc::FitPolicy::BestFit);

	auto* a = alloc.allocate(100);
	auto* b = alloc.allocate(200);
	auto* c = alloc.allocate(50);

	REQUIRE(a != nullptr);
	REQUIRE(b != nullptr);
	REQUIRE(c != nullptr);

	alloc.deallocate(a);
	alloc.deallocate(c);

	// BestFitでは小さい方のブロック(c跡)に小さな割り当てが入る
	auto* small = alloc.allocate(32);
	REQUIRE(small != nullptr);
}

TEST_CASE("FreeListAllocator reset clears all", "[memory][freelist]")
{
	sgc::FreeListAllocator alloc(1024);
	[[maybe_unused]] auto* p1 = alloc.allocate(100);
	[[maybe_unused]] auto* p2 = alloc.allocate(200);
	REQUIRE(alloc.used() > 0);

	alloc.reset();
	REQUIRE(alloc.used() == 0);

	// リセット後に全容量を使える
	auto* ptr = alloc.allocate(512);
	REQUIRE(ptr != nullptr);
}
