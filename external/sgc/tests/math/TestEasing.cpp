/// @file TestEasing.cpp
/// @brief Easing.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/math/Easing.hpp"

TEST_CASE("Easing functions return 0 at t=0", "[math][easing]")
{
	REQUIRE(sgc::easing::linear(0.0f) == 0.0f);
	REQUIRE(sgc::easing::inQuad(0.0f) == 0.0f);
	REQUIRE(sgc::easing::outQuad(0.0f) == 0.0f);
	REQUIRE(sgc::easing::inCubic(0.0f) == 0.0f);
	REQUIRE(sgc::easing::inQuart(0.0f) == 0.0f);
	REQUIRE(sgc::easing::inQuint(0.0f) == 0.0f);
	REQUIRE(sgc::easing::inBounce(0.0f) == Catch::Approx(0.0f).margin(1e-5f));
}

TEST_CASE("Easing functions return 1 at t=1", "[math][easing]")
{
	REQUIRE(sgc::easing::linear(1.0f) == 1.0f);
	REQUIRE(sgc::easing::inQuad(1.0f) == 1.0f);
	REQUIRE(sgc::easing::outQuad(1.0f) == 1.0f);
	REQUIRE(sgc::easing::outCubic(1.0f) == 1.0f);
	REQUIRE(sgc::easing::outQuart(1.0f) == 1.0f);
	REQUIRE(sgc::easing::outQuint(1.0f) == 1.0f);
	REQUIRE(sgc::easing::outBounce(1.0f) == Catch::Approx(1.0f));
}

TEST_CASE("Easing inOutQuad at t=0.5", "[math][easing]")
{
	REQUIRE(sgc::easing::inOutQuad(0.5f) == Catch::Approx(0.5f));
	REQUIRE(sgc::easing::inOutCubic(0.5f) == Catch::Approx(0.5f));
}

TEST_CASE("InBack goes below zero", "[math][easing]")
{
	float val = sgc::easing::inBack(0.1f);
	REQUIRE(val < 0.0f);
}

TEST_CASE("Sine easing", "[math][easing]")
{
	REQUIRE(sgc::easing::inSine(0.0f) == Catch::Approx(0.0f).margin(1e-6f));
	REQUIRE(sgc::easing::outSine(1.0f) == Catch::Approx(1.0f).margin(1e-6f));
	REQUIRE(sgc::easing::inOutSine(0.5f) == Catch::Approx(0.5f).margin(1e-6f));
}

// ── InOut 追加分 ────────────────────────────────────────

TEST_CASE("InOutExpo boundaries", "[math][easing]")
{
	REQUIRE(sgc::easing::inOutExpo(0.0f) == Catch::Approx(0.0f));
	REQUIRE(sgc::easing::inOutExpo(1.0f) == Catch::Approx(1.0f));
	REQUIRE(sgc::easing::inOutExpo(0.5f) == Catch::Approx(0.5f).margin(0.01f));
}

TEST_CASE("InOutCirc boundaries", "[math][easing]")
{
	REQUIRE(sgc::easing::inOutCirc(0.0f) == Catch::Approx(0.0f).margin(1e-5f));
	REQUIRE(sgc::easing::inOutCirc(1.0f) == Catch::Approx(1.0f).margin(1e-5f));
	REQUIRE(sgc::easing::inOutCirc(0.5f) == Catch::Approx(0.5f).margin(0.01f));
}

TEST_CASE("InOutElastic boundaries", "[math][easing]")
{
	REQUIRE(sgc::easing::inOutElastic(0.0f) == Catch::Approx(0.0f));
	REQUIRE(sgc::easing::inOutElastic(1.0f) == Catch::Approx(1.0f));
}

TEST_CASE("InOutBack boundaries", "[math][easing]")
{
	REQUIRE(sgc::easing::inOutBack(0.0f) == Catch::Approx(0.0f).margin(1e-5f));
	REQUIRE(sgc::easing::inOutBack(1.0f) == Catch::Approx(1.0f).margin(1e-5f));
}

TEST_CASE("InOutBounce boundaries", "[math][easing]")
{
	REQUIRE(sgc::easing::inOutBounce(0.0f) == Catch::Approx(0.0f).margin(1e-5f));
	REQUIRE(sgc::easing::inOutBounce(1.0f) == Catch::Approx(1.0f).margin(1e-5f));
	REQUIRE(sgc::easing::inOutBounce(0.5f) == Catch::Approx(0.5f).margin(0.01f));
}
