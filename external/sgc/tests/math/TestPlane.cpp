/// @file TestPlane.cpp
/// @brief Plane.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/math/Plane.hpp"

using Catch::Approx;

TEST_CASE("Plane default is Y-up at origin", "[math][plane]")
{
	constexpr sgc::Planef p;
	STATIC_REQUIRE(p.normal.y == 1.0f);
	STATIC_REQUIRE(p.distance == 0.0f);
}

TEST_CASE("Plane fromNormalAndPoint", "[math][plane]")
{
	constexpr auto p = sgc::Planef::fromNormalAndPoint({0, 1, 0}, {0, 5, 0});
	STATIC_REQUIRE(p.normal.y == 1.0f);
	STATIC_REQUIRE(p.distance == -5.0f);
}

TEST_CASE("Plane signedDistanceTo", "[math][plane]")
{
	constexpr auto p = sgc::Planef::fromNormalAndPoint({0, 1, 0}, {0, 5, 0});
	STATIC_REQUIRE(p.signedDistanceTo({0, 10, 0}) == 5.0f);
	STATIC_REQUIRE(p.signedDistanceTo({0, 0, 0}) == -5.0f);
	STATIC_REQUIRE(p.signedDistanceTo({0, 5, 0}) == 0.0f);
}

TEST_CASE("Plane closestPoint", "[math][plane]")
{
	constexpr auto p = sgc::Planef::fromNormalAndPoint({0, 1, 0}, {0, 0, 0});
	constexpr auto cp = p.closestPoint({3, 7, 2});
	STATIC_REQUIRE(cp.x == 3.0f);
	STATIC_REQUIRE(cp.y == 0.0f);
	STATIC_REQUIRE(cp.z == 2.0f);
}

TEST_CASE("Plane classify", "[math][plane]")
{
	const auto p = sgc::Planef::fromNormalAndPoint({0, 1, 0}, {0, 0, 0});
	REQUIRE(p.classify({0, 5, 0}) == sgc::PlaneClassification::Front);
	REQUIRE(p.classify({0, -5, 0}) == sgc::PlaneClassification::Back);
	REQUIRE(p.classify({0, 0, 0}) == sgc::PlaneClassification::On);
}

TEST_CASE("Plane fromThreePoints", "[math][plane]")
{
	const auto p = sgc::Planef::fromThreePoints(
		{0, 0, 0}, {1, 0, 0}, {0, 0, 1});
	// 法線は Y方向（下向き、頂点の巻き方向による）
	REQUIRE(std::abs(p.normal.y) == Approx(1.0f));
	REQUIRE(p.distance == Approx(0.0f));
}
