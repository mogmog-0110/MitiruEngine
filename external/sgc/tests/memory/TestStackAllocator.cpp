/// @file TestStackAllocator.cpp
/// @brief StackAllocator.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/memory/StackAllocator.hpp"

TEST_CASE("StackAllocator basic allocate", "[memory][stack]")
{
	sgc::StackAllocator stack(256);
	REQUIRE(stack.capacity() == 256);
	REQUIRE(stack.used() == 0);

	auto* ptr = stack.allocate(64);
	REQUIRE(ptr != nullptr);
	REQUIRE(stack.used() >= 64);
}

TEST_CASE("StackAllocator create constructs object", "[memory][stack]")
{
	sgc::StackAllocator stack(256);
	auto* val = stack.create<int>(42);
	REQUIRE(val != nullptr);
	REQUIRE(*val == 42);
}

TEST_CASE("StackAllocator returns nullptr when full", "[memory][stack]")
{
	sgc::StackAllocator stack(32);
	auto* a = stack.allocate(16);
	REQUIRE(a != nullptr);

	auto* b = stack.allocate(16);
	REQUIRE(b != nullptr);

	// 残り容量なし
	auto* c = stack.allocate(16);
	REQUIRE(c == nullptr);
}

TEST_CASE("StackAllocator getMarker and freeToMarker", "[memory][stack]")
{
	sgc::StackAllocator stack(256);

	auto* a = stack.create<int>(1);
	auto marker = stack.getMarker();

	auto* b = stack.create<int>(2);
	auto* c = stack.create<int>(3);
	REQUIRE(b != nullptr);
	REQUIRE(c != nullptr);

	stack.freeToMarker(marker);
	REQUIRE(stack.used() == marker);

	// マーカー位置から再割り当て可能
	auto* d = stack.create<int>(4);
	REQUIRE(d != nullptr);
	REQUIRE(*d == 4);
	REQUIRE(*a == 1);  // マーカー前のデータは有効
}

TEST_CASE("StackAllocator reset clears all", "[memory][stack]")
{
	sgc::StackAllocator stack(256);
	[[maybe_unused]] auto* p1 = stack.create<int>(1);
	[[maybe_unused]] auto* p2 = stack.create<int>(2);
	REQUIRE(stack.used() > 0);

	stack.reset();
	REQUIRE(stack.used() == 0);
	REQUIRE(stack.remaining() == 256);
}

TEST_CASE("StackAllocator remaining tracks available space", "[memory][stack]")
{
	sgc::StackAllocator stack(128);
	REQUIRE(stack.remaining() == 128);

	[[maybe_unused]] auto* p = stack.allocate(32);
	REQUIRE(stack.remaining() <= 96);
	REQUIRE(stack.used() + stack.remaining() == stack.capacity());
}
