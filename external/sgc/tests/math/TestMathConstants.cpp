/// @file TestMathConstants.cpp
/// @brief MathConstants.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/math/MathConstants.hpp"

TEST_CASE("Math constants have expected values", "[math][constants]")
{
	REQUIRE(sgc::PI_F == Catch::Approx(3.14159265f).margin(1e-6f));
	REQUIRE(sgc::PI == Catch::Approx(3.14159265358979).margin(1e-12));
	REQUIRE(sgc::TAU_F == Catch::Approx(6.28318530f).margin(1e-5f));
	REQUIRE(sgc::SQRT2 == Catch::Approx(1.41421356).margin(1e-7));
}

TEST_CASE("Degree/radian conversion", "[math][constants]")
{
	REQUIRE(sgc::toRadians(180.0f) == Catch::Approx(sgc::PI_F));
	REQUIRE(sgc::toRadians(90.0) == Catch::Approx(sgc::PI / 2.0));
	REQUIRE(sgc::toDegrees(sgc::PI_F) == Catch::Approx(180.0f));
	REQUIRE(sgc::toDegrees(sgc::PI) == Catch::Approx(180.0));
}

TEST_CASE("approxEqual floating point comparison", "[math][constants]")
{
	REQUIRE(sgc::approxEqual(1.0f, 1.0f + 1e-7f));
	REQUIRE_FALSE(sgc::approxEqual(1.0f, 1.1f));
	REQUIRE(sgc::approxEqual(1.0, 1.0 + 1e-10));
}

TEST_CASE("clamp constrains values", "[math][constants]")
{
	REQUIRE(sgc::clamp(5, 0, 10) == 5);
	REQUIRE(sgc::clamp(-5, 0, 10) == 0);
	REQUIRE(sgc::clamp(15, 0, 10) == 10);
	REQUIRE(sgc::clamp(0.5f, 0.0f, 1.0f) == 0.5f);
}
