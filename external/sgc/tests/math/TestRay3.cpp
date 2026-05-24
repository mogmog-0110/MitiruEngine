/// @file TestRay3.cpp
/// @brief Ray3.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/math/Ray3.hpp"

using Catch::Approx;

TEST_CASE("Ray3 default construction", "[math][ray3]")
{
	constexpr sgc::Ray3f r;
	STATIC_REQUIRE(r.origin.x == 0.0f);
	STATIC_REQUIRE(r.direction.x == 0.0f);
}

TEST_CASE("Ray3 pointAt returns correct position", "[math][ray3]")
{
	constexpr sgc::Ray3f ray{{0, 0, 0}, {0, 0, -1}};
	constexpr auto p = ray.pointAt(5.0f);
	STATIC_REQUIRE(p.x == 0.0f);
	STATIC_REQUIRE(p.y == 0.0f);
	STATIC_REQUIRE(p.z == -5.0f);
}

TEST_CASE("Ray3 pointAt with offset origin", "[math][ray3]")
{
	constexpr sgc::Ray3f ray{{1, 2, 3}, {1, 0, 0}};
	constexpr auto p = ray.pointAt(10.0f);
	STATIC_REQUIRE(p.x == 11.0f);
	STATIC_REQUIRE(p.y == 2.0f);
	STATIC_REQUIRE(p.z == 3.0f);
}

TEST_CASE("Ray3 pointAt at t=0 is origin", "[math][ray3]")
{
	constexpr sgc::Ray3f ray{{5, 6, 7}, {1, 1, 1}};
	constexpr auto p = ray.pointAt(0.0f);
	STATIC_REQUIRE(p.x == 5.0f);
	STATIC_REQUIRE(p.y == 6.0f);
	STATIC_REQUIRE(p.z == 7.0f);
}
