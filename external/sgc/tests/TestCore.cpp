/// @file TestCore.cpp
/// @brief Core.hpp のユニットテスト — バージョン情報の検証

#include <catch2/catch_test_macros.hpp>

#include "sgc/Core.hpp"

TEST_CASE("Version struct holds correct values", "[core]")
{
	REQUIRE(sgc::LIBRARY_VERSION.major == 0);
	REQUIRE(sgc::LIBRARY_VERSION.minor == 1);
	REQUIRE(sgc::LIBRARY_VERSION.patch == 0);
}

TEST_CASE("Version is constexpr", "[core]")
{
	constexpr auto ver = sgc::LIBRARY_VERSION;
	STATIC_REQUIRE(ver.major == 0);
	STATIC_REQUIRE(ver.minor == 1);
	STATIC_REQUIRE(ver.patch == 0);
}
