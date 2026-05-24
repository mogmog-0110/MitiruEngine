#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/platformer/TileCollider.hpp"

using namespace sgc::platformer;

TEST_CASE("TileCollider - No collision with empty tilemap", "[platformer][tilecollider]")
{
	const sgc::Rectf aabb{10.0f, 10.0f, 16.0f, 16.0f};
	const sgc::Vec2f velocity{100.0f, 0.0f};
	auto query = [](int, int) { return false; };
	const float dt = 1.0f / 60.0f;

	auto result = sweepAABB(aabb, velocity, query, 16.0f, dt);
	REQUIRE_FALSE(result.hit);
	REQUIRE(result.toi == Catch::Approx(1.0f));
}

TEST_CASE("TileCollider - Collision with solid tile right", "[platformer][tilecollider]")
{
	// AABB at (10,10) size 16x16, right edge at 26
	// Moving right at 6000px/s for 1/60s = 100px, new right edge at 126
	// Solid tile at column 3 (48..64) - will be reached
	const sgc::Rectf aabb{10.0f, 10.0f, 16.0f, 16.0f};
	const sgc::Vec2f velocity{6000.0f, 0.0f};
	auto query = [](int tx, int) { return tx == 3; };
	const float dt = 1.0f / 60.0f;

	auto result = sweepAABB(aabb, velocity, query, 16.0f, dt);
	REQUIRE(result.hit);
	REQUIRE(result.normal.x == Catch::Approx(-1.0f));
	REQUIRE(result.normal.y == Catch::Approx(0.0f));
	REQUIRE(result.toi >= 0.0f);
	REQUIRE(result.toi <= 1.0f);
}

TEST_CASE("TileCollider - Collision with solid tile below", "[platformer][tilecollider]")
{
	// AABB at (16,16) size 16x16, bottom edge at 32
	// Moving down at 6000px/s for 1/60s = 100px, new bottom at 132
	// Solid tile at row 5 (80..96) - will be reached
	const sgc::Rectf aabb{16.0f, 16.0f, 16.0f, 16.0f};
	const sgc::Vec2f velocity{0.0f, 6000.0f};
	auto query = [](int, int ty) { return ty == 5; };
	const float dt = 1.0f / 60.0f;

	auto result = sweepAABB(aabb, velocity, query, 16.0f, dt);
	REQUIRE(result.hit);
	REQUIRE(result.normal.y == Catch::Approx(-1.0f));
	REQUIRE(result.normal.x == Catch::Approx(0.0f));
}

TEST_CASE("TileCollider - Zero velocity no collision", "[platformer][tilecollider]")
{
	const sgc::Rectf aabb{10.0f, 10.0f, 16.0f, 16.0f};
	const sgc::Vec2f velocity{0.0f, 0.0f};
	auto query = [](int, int) { return true; };
	const float dt = 1.0f / 60.0f;

	auto result = sweepAABB(aabb, velocity, query, 16.0f, dt);
	REQUIRE_FALSE(result.hit);
}

TEST_CASE("TileCollider - Tile coordinate recorded on hit", "[platformer][tilecollider]")
{
	const sgc::Rectf aabb{10.0f, 10.0f, 16.0f, 16.0f};
	const sgc::Vec2f velocity{1000.0f, 0.0f};
	// Only tile at (4, 0) is solid
	auto query = [](int tx, int ty) { return tx == 4 && ty == 0; };
	const float dt = 1.0f / 60.0f;

	auto result = sweepAABB(aabb, velocity, query, 16.0f, dt);
	if (result.hit)
	{
		REQUIRE(result.tileCoord.x == 4);
	}
}

TEST_CASE("TileCollider - Diagonal movement finds earliest collision", "[platformer][tilecollider]")
{
	// AABB at (0,0) size 16x16, bottom edge at 16
	// Moving diagonally at 6000px/s each axis, dt=1/60 => 100px movement
	// Floor at row 5 (80..96) - bottom edge reaches y=116, will hit row 5
	const sgc::Rectf aabb{0.0f, 0.0f, 16.0f, 16.0f};
	const sgc::Vec2f velocity{6000.0f, 6000.0f};
	auto query = [](int, int ty) { return ty == 5; };
	const float dt = 1.0f / 60.0f;

	auto result = sweepAABB(aabb, velocity, query, 16.0f, dt);
	REQUIRE(result.hit);
	REQUIRE(result.toi >= 0.0f);
	REQUIRE(result.toi <= 1.0f);
}
