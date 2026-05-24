/// @file TestGeometry.cpp
/// @brief Geometry.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/math/Geometry.hpp"

TEST_CASE("AABB2 contains point", "[math][geometry]")
{
	constexpr sgc::AABB2f box{{0.0f, 0.0f}, {10.0f, 10.0f}};
	REQUIRE(box.contains({5.0f, 5.0f}));
	REQUIRE(box.contains({0.0f, 0.0f}));
	REQUIRE_FALSE(box.contains({-1.0f, 5.0f}));
	REQUIRE_FALSE(box.contains({5.0f, 11.0f}));
}

TEST_CASE("AABB2 intersection", "[math][geometry]")
{
	constexpr sgc::AABB2f a{{0.0f, 0.0f}, {10.0f, 10.0f}};
	constexpr sgc::AABB2f b{{5.0f, 5.0f}, {15.0f, 15.0f}};
	constexpr sgc::AABB2f c{{20.0f, 20.0f}, {30.0f, 30.0f}};

	REQUIRE(a.intersects(b));
	REQUIRE_FALSE(a.intersects(c));
}

TEST_CASE("AABB2 center and size", "[math][geometry]")
{
	constexpr sgc::AABB2f box{{0.0f, 0.0f}, {10.0f, 20.0f}};
	constexpr auto c = box.center();
	constexpr auto s = box.size();

	REQUIRE(c.x == 5.0f);
	REQUIRE(c.y == 10.0f);
	REQUIRE(s.x == 10.0f);
	REQUIRE(s.y == 20.0f);
}

TEST_CASE("Circle contains point", "[math][geometry]")
{
	constexpr sgc::Circlef circle{{5.0f, 5.0f}, 3.0f};
	REQUIRE(circle.contains({5.0f, 5.0f}));
	REQUIRE(circle.contains({7.0f, 5.0f}));
	REQUIRE_FALSE(circle.contains({9.0f, 5.0f}));
}

TEST_CASE("Circle-circle intersection", "[math][geometry]")
{
	constexpr sgc::Circlef a{{0.0f, 0.0f}, 5.0f};
	constexpr sgc::Circlef b{{8.0f, 0.0f}, 5.0f};
	constexpr sgc::Circlef c{{20.0f, 0.0f}, 2.0f};

	REQUIRE(a.intersects(b));
	REQUIRE_FALSE(a.intersects(c));
}

TEST_CASE("Circle-AABB intersection", "[math][geometry]")
{
	constexpr sgc::Circlef circle{{0.0f, 0.0f}, 5.0f};
	constexpr sgc::AABB2f box{{3.0f, 3.0f}, {10.0f, 10.0f}};
	constexpr sgc::AABB2f farBox{{20.0f, 20.0f}, {30.0f, 30.0f}};

	REQUIRE(circle.intersects(box));
	REQUIRE_FALSE(circle.intersects(farBox));
}

TEST_CASE("Ray2 pointAt", "[math][geometry]")
{
	constexpr sgc::Ray2f ray{{0.0f, 0.0f}, {1.0f, 0.0f}};
	constexpr auto p = ray.pointAt(5.0f);
	REQUIRE(p.x == 5.0f);
	REQUIRE(p.y == 0.0f);
}

TEST_CASE("Sphere intersection", "[math][geometry]")
{
	constexpr sgc::Spheref a{{0.0f, 0.0f, 0.0f}, 5.0f};
	constexpr sgc::Spheref b{{8.0f, 0.0f, 0.0f}, 5.0f};
	constexpr sgc::Spheref c{{20.0f, 0.0f, 0.0f}, 2.0f};

	REQUIRE(a.intersects(b));
	REQUIRE_FALSE(a.intersects(c));
}
