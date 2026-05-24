/// @file TestInterpolation.cpp
/// @brief Interpolation.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/math/Interpolation.hpp"

TEST_CASE("lerp interpolates correctly", "[math][interpolation]")
{
	REQUIRE(sgc::lerp(0.0f, 10.0f, 0.0f) == 0.0f);
	REQUIRE(sgc::lerp(0.0f, 10.0f, 1.0f) == 10.0f);
	REQUIRE(sgc::lerp(0.0f, 10.0f, 0.5f) == 5.0f);
	REQUIRE(sgc::lerp(10.0f, 20.0f, 0.25f) == 12.5f);
}

TEST_CASE("inverseLerp returns normalized position", "[math][interpolation]")
{
	REQUIRE(sgc::inverseLerp(0.0f, 10.0f, 5.0f) == Catch::Approx(0.5f));
	REQUIRE(sgc::inverseLerp(0.0f, 10.0f, 0.0f) == Catch::Approx(0.0f));
	REQUIRE(sgc::inverseLerp(0.0f, 10.0f, 10.0f) == Catch::Approx(1.0f));
}

TEST_CASE("remap converts between ranges", "[math][interpolation]")
{
	REQUIRE(sgc::remap(0.0f, 100.0f, 0.0f, 1.0f, 50.0f) == Catch::Approx(0.5f));
	REQUIRE(sgc::remap(0.0f, 10.0f, 0.0f, 100.0f, 3.0f) == Catch::Approx(30.0f));
}

TEST_CASE("smoothstep produces smooth transition", "[math][interpolation]")
{
	REQUIRE(sgc::smoothstep(0.0f, 1.0f, 0.0f) == 0.0f);
	REQUIRE(sgc::smoothstep(0.0f, 1.0f, 1.0f) == 1.0f);
	REQUIRE(sgc::smoothstep(0.0f, 1.0f, 0.5f) == Catch::Approx(0.5f));
	REQUIRE(sgc::smoothstep(0.0f, 1.0f, -1.0f) == 0.0f);
	REQUIRE(sgc::smoothstep(0.0f, 1.0f, 2.0f) == 1.0f);
}

TEST_CASE("smootherstep produces smoother transition", "[math][interpolation]")
{
	REQUIRE(sgc::smootherstep(0.0f, 1.0f, 0.0f) == 0.0f);
	REQUIRE(sgc::smootherstep(0.0f, 1.0f, 1.0f) == 1.0f);
	REQUIRE(sgc::smootherstep(0.0f, 1.0f, 0.5f) == Catch::Approx(0.5f));
}

TEST_CASE("moveTowards approaches target", "[math][interpolation]")
{
	REQUIRE(sgc::moveTowards(0.0f, 10.0f, 3.0f) == 3.0f);
	REQUIRE(sgc::moveTowards(0.0f, 10.0f, 100.0f) == 10.0f);
	REQUIRE(sgc::moveTowards(10.0f, 5.0f, 3.0f) == 7.0f);
	REQUIRE(sgc::moveTowards(5.0f, 5.0f, 1.0f) == 5.0f);
}
