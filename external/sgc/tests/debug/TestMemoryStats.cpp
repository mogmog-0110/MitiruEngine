/// @file TestMemoryStats.cpp
/// @brief MemoryStats.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/debug/MemoryStats.hpp"

using Catch::Approx;

TEST_CASE("MemoryStats records allocation correctly", "[debug][memorystats]")
{
	sgc::debug::MemoryStats stats;
	stats.recordAllocation(1024);

	REQUIRE(stats.totalAllocated == 1024);
	REQUIRE(stats.currentUsage == 1024);
	REQUIRE(stats.peakUsage == 1024);
	REQUIRE(stats.allocationCount == 1);
}

TEST_CASE("MemoryStats records free correctly", "[debug][memorystats]")
{
	sgc::debug::MemoryStats stats;
	stats.recordAllocation(1024);
	stats.recordFree(512);

	REQUIRE(stats.totalFreed == 512);
	REQUIRE(stats.currentUsage == 512);
	REQUIRE(stats.peakUsage == 1024);
	REQUIRE(stats.freeCount == 1);
	REQUIRE(stats.activeAllocations() == 0);
}

TEST_CASE("MemoryStats tracks peak usage", "[debug][memorystats]")
{
	sgc::debug::MemoryStats stats;
	stats.recordAllocation(1000);
	stats.recordAllocation(2000);
	REQUIRE(stats.peakUsage == 3000);

	stats.recordFree(1500);
	REQUIRE(stats.peakUsage == 3000);
	REQUIRE(stats.currentUsage == 1500);
}

TEST_CASE("MemoryStats average and summary", "[debug][memorystats]")
{
	sgc::debug::MemoryStats stats;
	REQUIRE(stats.averageAllocationSize() == 0.0);

	stats.recordAllocation(100);
	stats.recordAllocation(300);
	REQUIRE(stats.averageAllocationSize() == Approx(200.0));

	auto s = stats.summary();
	REQUIRE_FALSE(s.empty());

	stats.reset();
	REQUIRE(stats.totalAllocated == 0);
	REQUIRE(stats.currentUsage == 0);
	REQUIRE(stats.allocationCount == 0);
}

TEST_CASE("ScopedMemoryTracker RAII allocation tracking", "[debug][memorystats]")
{
	sgc::debug::MemoryStats stats;

	{
		sgc::debug::ScopedMemoryTracker tracker(stats, 2048);
		REQUIRE(stats.currentUsage == 2048);
		REQUIRE(stats.allocationCount == 1);
	}

	// スコープ終了後に解放が記録される
	REQUIRE(stats.currentUsage == 0);
	REQUIRE(stats.freeCount == 1);
	REQUIRE(stats.peakUsage == 2048);
}
