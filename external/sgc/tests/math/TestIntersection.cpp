/// @file TestIntersection.cpp
/// @brief Intersection.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/math/Intersection.hpp"

using Catch::Approx;
using namespace sgc;
using namespace sgc::intersection;

// ── Ray3 vs Plane ────────────────────────────────────────────────

TEST_CASE("ray3VsPlane hits horizontal plane", "[math][intersection]")
{
	const Ray3f ray{{0, 10, 0}, {0, -1, 0}};
	const auto plane = Planef::fromNormalAndPoint({0, 1, 0}, {0, 0, 0});
	const auto t = ray3VsPlane(ray, plane);
	REQUIRE(t.has_value());
	REQUIRE(*t == Approx(10.0f));
}

TEST_CASE("ray3VsPlane parallel ray misses", "[math][intersection]")
{
	const Ray3f ray{{0, 5, 0}, {1, 0, 0}};
	const auto plane = Planef::fromNormalAndPoint({0, 1, 0}, {0, 0, 0});
	const auto t = ray3VsPlane(ray, plane);
	REQUIRE_FALSE(t.has_value());
}

// ── Ray3 vs Sphere ───────────────────────────────────────────────

TEST_CASE("ray3VsSphere hits unit sphere at origin", "[math][intersection]")
{
	const Ray3f ray{{0, 0, 5}, {0, 0, -1}};
	const Spheref sphere{{0, 0, 0}, 1.0f};
	const auto t = ray3VsSphere(ray, sphere);
	REQUIRE(t.has_value());
	REQUIRE(*t == Approx(4.0f));
}

TEST_CASE("ray3VsSphere misses", "[math][intersection]")
{
	const Ray3f ray{{10, 10, 10}, {0, 0, -1}};
	const Spheref sphere{{0, 0, 0}, 1.0f};
	const auto t = ray3VsSphere(ray, sphere);
	REQUIRE_FALSE(t.has_value());
}

TEST_CASE("ray3VsSphere origin inside sphere", "[math][intersection]")
{
	const Ray3f ray{{0, 0, 0}, {0, 0, 1}};
	const Spheref sphere{{0, 0, 0}, 5.0f};
	const auto t = ray3VsSphere(ray, sphere);
	REQUIRE(t.has_value());
	REQUIRE(*t == Approx(5.0f));
}

// ── Ray3 vs AABB3 ────────────────────────────────────────────────

TEST_CASE("ray3VsAABB3 hits unit box", "[math][intersection]")
{
	const Ray3f ray{{-5, 0.5f, 0.5f}, {1, 0, 0}};
	const AABB3f aabb{{0, 0, 0}, {1, 1, 1}};
	const auto t = ray3VsAABB3(ray, aabb);
	REQUIRE(t.has_value());
	REQUIRE(*t == Approx(5.0f));
}

TEST_CASE("ray3VsAABB3 misses", "[math][intersection]")
{
	const Ray3f ray{{-5, 5, 0}, {1, 0, 0}};
	const AABB3f aabb{{0, 0, 0}, {1, 1, 1}};
	const auto t = ray3VsAABB3(ray, aabb);
	REQUIRE_FALSE(t.has_value());
}

TEST_CASE("ray3VsAABB3 origin inside box", "[math][intersection]")
{
	const Ray3f ray{{0.5f, 0.5f, 0.5f}, {1, 0, 0}};
	const AABB3f aabb{{0, 0, 0}, {1, 1, 1}};
	const auto t = ray3VsAABB3(ray, aabb);
	REQUIRE(t.has_value());
	REQUIRE(*t == Approx(0.0f));
}

// ── Ray3 vs Triangle ─────────────────────────────────────────────

TEST_CASE("ray3VsTriangle hits face", "[math][intersection]")
{
	const Ray3f ray{{0.25f, 0.25f, -1}, {0, 0, 1}};
	const Vec3f v0{0, 0, 0}, v1{1, 0, 0}, v2{0, 1, 0};
	const auto t = ray3VsTriangle(ray, v0, v1, v2);
	REQUIRE(t.has_value());
	REQUIRE(*t == Approx(1.0f));
}

TEST_CASE("ray3VsTriangle misses outside", "[math][intersection]")
{
	const Ray3f ray{{5, 5, -1}, {0, 0, 1}};
	const Vec3f v0{0, 0, 0}, v1{1, 0, 0}, v2{0, 1, 0};
	const auto t = ray3VsTriangle(ray, v0, v1, v2);
	REQUIRE_FALSE(t.has_value());
}

// ── Ray2 vs AABB2 ────────────────────────────────────────────────

TEST_CASE("ray2VsAABB2 hits box", "[math][intersection]")
{
	const Ray2f ray{{-5, 0.5f}, {1, 0}};
	const AABB2f aabb{{0, 0}, {1, 1}};
	const auto t = ray2VsAABB2(ray, aabb);
	REQUIRE(t.has_value());
	REQUIRE(*t == Approx(5.0f));
}

TEST_CASE("ray2VsAABB2 misses", "[math][intersection]")
{
	const Ray2f ray{{-5, 5}, {1, 0}};
	const AABB2f aabb{{0, 0}, {1, 1}};
	const auto t = ray2VsAABB2(ray, aabb);
	REQUIRE_FALSE(t.has_value());
}

// ── Ray2 vs Circle ───────────────────────────────────────────────

TEST_CASE("ray2VsCircle hits", "[math][intersection]")
{
	const Ray2f ray{{-5, 0}, {1, 0}};
	const Circlef circle{{0, 0}, 1.0f};
	const auto t = ray2VsCircle(ray, circle);
	REQUIRE(t.has_value());
	REQUIRE(*t == Approx(4.0f));
}

TEST_CASE("ray2VsCircle misses", "[math][intersection]")
{
	const Ray2f ray{{-5, 5}, {1, 0}};
	const Circlef circle{{0, 0}, 1.0f};
	const auto t = ray2VsCircle(ray, circle);
	REQUIRE_FALSE(t.has_value());
}

// ── Sphere vs AABB3 ─────────────────────────────────────────────

TEST_CASE("sphereVsAABB3 overlapping", "[math][intersection]")
{
	const Spheref sphere{{0.5f, 0.5f, 0.5f}, 0.5f};
	const AABB3f aabb{{0, 0, 0}, {1, 1, 1}};
	REQUIRE(sphereVsAABB3(sphere, aabb));
}

TEST_CASE("sphereVsAABB3 separated", "[math][intersection]")
{
	const Spheref sphere{{10, 10, 10}, 1.0f};
	const AABB3f aabb{{0, 0, 0}, {1, 1, 1}};
	REQUIRE_FALSE(sphereVsAABB3(sphere, aabb));
}

TEST_CASE("sphereVsAABB3 touching corner", "[math][intersection]")
{
	// 球の中心が AABB の角からちょうど半径の距離
	const Spheref sphere{{2, 1, 1}, 1.0f};
	const AABB3f aabb{{0, 0, 0}, {1, 1, 1}};
	REQUIRE(sphereVsAABB3(sphere, aabb));
}
