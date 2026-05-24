/// @file TestAssert.cpp
/// @brief Assert.hpp のユニットテスト — アサーションマクロの正常パスを検証

#include <catch2/catch_test_macros.hpp>

#include "sgc/core/Assert.hpp"

TEST_CASE("SGC_ASSERT does not abort on true condition", "[core][assert]")
{
	SGC_ASSERT(true, "this should not fire");
	SGC_ASSERT(1 + 1 == 2, "basic math works");
	REQUIRE(true);
}

TEST_CASE("SGC_ENSURE does not abort on true condition", "[core][assert]")
{
	SGC_ENSURE(true, "this should not fire");
	SGC_ENSURE(42 > 0, "42 is positive");
	REQUIRE(true);
}
