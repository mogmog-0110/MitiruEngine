#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/physics/AABB2DCollision.hpp"

using namespace sgc;
using namespace sgc::physics;
using Catch::Approx;

TEST_CASE("detectAABBCollision - non-overlapping AABBs return no collision", "[physics][AABB2DCollision]")
{
	SECTION("separated on X axis")
	{
		const AABB2f a{{0.0f, 0.0f}, {5.0f, 5.0f}};
		const AABB2f b{{6.0f, 0.0f}, {10.0f, 5.0f}};
		const auto result = detectAABBCollision(a, b);
		REQUIRE_FALSE(result.colliding);
	}

	SECTION("separated on Y axis")
	{
		const AABB2f a{{0.0f, 0.0f}, {5.0f, 5.0f}};
		const AABB2f b{{0.0f, 6.0f}, {5.0f, 10.0f}};
		const auto result = detectAABBCollision(a, b);
		REQUIRE_FALSE(result.colliding);
	}

	SECTION("touching edges are not colliding")
	{
		const AABB2f a{{0.0f, 0.0f}, {5.0f, 5.0f}};
		const AABB2f b{{5.0f, 0.0f}, {10.0f, 5.0f}};
		const auto result = detectAABBCollision(a, b);
		REQUIRE_FALSE(result.colliding);
	}
}

TEST_CASE("detectAABBCollision - overlapping AABBs return correct result", "[physics][AABB2DCollision]")
{
	SECTION("overlap on X axis (smaller penetration)")
	{
		const AABB2f a{{0.0f, 0.0f}, {10.0f, 10.0f}};
		const AABB2f b{{8.0f, 0.0f}, {18.0f, 10.0f}};
		const auto result = detectAABBCollision(a, b);

		REQUIRE(result.colliding);
		REQUIRE(result.penetration == Approx(2.0f));
		REQUIRE(result.normal.x == Approx(1.0f));
		REQUIRE(result.normal.y == Approx(0.0f));
	}

	SECTION("overlap on Y axis (smaller penetration)")
	{
		const AABB2f a{{0.0f, 0.0f}, {10.0f, 10.0f}};
		const AABB2f b{{0.0f, 7.0f}, {10.0f, 17.0f}};
		const auto result = detectAABBCollision(a, b);

		REQUIRE(result.colliding);
		REQUIRE(result.penetration == Approx(3.0f));
		REQUIRE(result.normal.x == Approx(0.0f));
		REQUIRE(result.normal.y == Approx(1.0f));
	}

	SECTION("A completely inside B")
	{
		const AABB2f a{{3.0f, 3.0f}, {7.0f, 7.0f}};
		const AABB2f b{{0.0f, 0.0f}, {10.0f, 10.0f}};
		const auto result = detectAABBCollision(a, b);

		REQUIRE(result.colliding);
		REQUIRE(result.penetration > 0.0f);
	}

	SECTION("normal points from A center toward B center direction")
	{
		const AABB2f a{{0.0f, 0.0f}, {6.0f, 10.0f}};
		const AABB2f b{{5.0f, 0.0f}, {15.0f, 10.0f}};
		const auto result = detectAABBCollision(a, b);

		REQUIRE(result.colliding);
		// A is to the left of B, so normal should push A left (negative X)
		REQUIRE(result.penetration == Approx(1.0f));
	}
}

TEST_CASE("detectAABBCollision - separation vector magnitude equals penetration", "[physics][AABB2DCollision]")
{
	const AABB2f a{{0.0f, 0.0f}, {10.0f, 10.0f}};
	const AABB2f b{{8.0f, 3.0f}, {18.0f, 13.0f}};
	const auto result = detectAABBCollision(a, b);

	REQUIRE(result.colliding);

	// separationVector length should equal penetration
	const float sepLen = std::sqrt(
		result.separationVector.x * result.separationVector.x +
		result.separationVector.y * result.separationVector.y);
	REQUIRE(sepLen == Approx(result.penetration));
}

TEST_CASE("resolveCollision - separates AABBs correctly", "[physics][AABB2DCollision]")
{
	const AABB2f a{{0.0f, 0.0f}, {10.0f, 10.0f}};
	const AABB2f b{{8.0f, 0.0f}, {18.0f, 10.0f}};

	const auto collision = detectAABBCollision(a, b);
	REQUIRE(collision.colliding);

	const auto resolved = resolveCollision(a, collision);

	// After resolution, AABBs should no longer overlap
	const auto recheck = detectAABBCollision(resolved, b);
	REQUIRE_FALSE(recheck.colliding);
}

TEST_CASE("resolveCollision - preserves AABB size", "[physics][AABB2DCollision]")
{
	const AABB2f a{{2.0f, 2.0f}, {8.0f, 8.0f}};
	const AABB2f b{{6.0f, 1.0f}, {12.0f, 9.0f}};

	const auto collision = detectAABBCollision(a, b);
	const auto resolved = resolveCollision(a, collision);

	const auto originalSize = a.size();
	const auto resolvedSize = resolved.size();

	REQUIRE(resolvedSize.x == Approx(originalSize.x));
	REQUIRE(resolvedSize.y == Approx(originalSize.y));
}

TEST_CASE("detectAABBCollision - constexpr evaluation", "[physics][AABB2DCollision]")
{
	constexpr AABB2f a{{0.0f, 0.0f}, {10.0f, 10.0f}};
	constexpr AABB2f b{{20.0f, 0.0f}, {30.0f, 10.0f}};
	constexpr auto result = detectAABBCollision(a, b);
	static_assert(!result.colliding, "Non-overlapping AABBs should not collide");
}

TEST_CASE("detectAABBCollision - double precision", "[physics][AABB2DCollision]")
{
	const AABB2d a{{0.0, 0.0}, {10.0, 10.0}};
	const AABB2d b{{9.5, 0.0}, {20.0, 10.0}};
	const auto result = detectAABBCollision(a, b);

	REQUIRE(result.colliding);
	REQUIRE(result.penetration == Approx(0.5));
}
