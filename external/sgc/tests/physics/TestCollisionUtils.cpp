#include <catch2/catch_test_macros.hpp>

#include "sgc/physics/CollisionUtils.hpp"

using namespace sgc::physics;

TEST_CASE("isGroundContact - detects vertical normals", "[physics][CollisionUtils]")
{
	REQUIRE(isGroundContact({0.0f, 1.0f}));
	REQUIRE(isGroundContact({0.0f, -1.0f}));
	REQUIRE(isGroundContact({0.2f, 0.8f}));
	REQUIRE(isGroundContact({-0.1f, -0.9f}));
}

TEST_CASE("isGroundContact - rejects horizontal normals", "[physics][CollisionUtils]")
{
	REQUIRE_FALSE(isGroundContact({1.0f, 0.0f}));
	REQUIRE_FALSE(isGroundContact({-1.0f, 0.0f}));
	REQUIRE_FALSE(isGroundContact({0.8f, 0.3f}));
}

TEST_CASE("isGroundContact - custom threshold", "[physics][CollisionUtils]")
{
	REQUIRE(isGroundContact({0.0f, 0.4f}, 0.3f));
	REQUIRE_FALSE(isGroundContact({0.0f, 0.4f}, 0.5f));
}

TEST_CASE("isWallContact - detects horizontal normals", "[physics][CollisionUtils]")
{
	REQUIRE(isWallContact({1.0f, 0.0f}));
	REQUIRE(isWallContact({-0.9f, 0.2f}));
}

TEST_CASE("isWallContact - rejects vertical normals", "[physics][CollisionUtils]")
{
	REQUIRE_FALSE(isWallContact({0.0f, 1.0f}));
	REQUIRE_FALSE(isWallContact({0.3f, 0.8f}));
}

TEST_CASE("GroundTracker - initial state is not grounded", "[physics][GroundTracker]")
{
	GroundTracker tracker;
	REQUIRE_FALSE(tracker.isOnGround());
}

TEST_CASE("GroundTracker - onContact sets grounded", "[physics][GroundTracker]")
{
	GroundTracker tracker;

	tracker.onContact({0.0f, 0.8f});
	REQUIRE(tracker.isOnGround());
}

TEST_CASE("GroundTracker - reset clears grounded", "[physics][GroundTracker]")
{
	GroundTracker tracker;

	tracker.onContact({0.0f, 1.0f});
	REQUIRE(tracker.isOnGround());

	tracker.reset();
	REQUIRE_FALSE(tracker.isOnGround());
}

TEST_CASE("GroundTracker - wall contact does not set grounded", "[physics][GroundTracker]")
{
	GroundTracker tracker;

	tracker.onContact({1.0f, 0.0f});
	REQUIRE_FALSE(tracker.isOnGround());
}

TEST_CASE("GroundTracker - reset and recheck pattern", "[physics][GroundTracker]")
{
	GroundTracker tracker;

	// Step 1: on ground
	tracker.reset();
	tracker.onContact({0.0f, 1.0f});
	REQUIRE(tracker.isOnGround());

	// Step 2: in air (no contacts)
	tracker.reset();
	REQUIRE_FALSE(tracker.isOnGround());

	// Step 3: land again
	tracker.reset();
	tracker.onContact({0.1f, -0.9f});
	REQUIRE(tracker.isOnGround());
}

TEST_CASE("GroundTracker - setOnGround forces state", "[physics][GroundTracker]")
{
	GroundTracker tracker;

	tracker.setOnGround(true);
	REQUIRE(tracker.isOnGround());

	tracker.setOnGround(false);
	REQUIRE_FALSE(tracker.isOnGround());
}

TEST_CASE("GroundTracker - multiple contacts in same step", "[physics][GroundTracker]")
{
	GroundTracker tracker;
	tracker.reset();

	tracker.onContact({1.0f, 0.0f});   // wall
	tracker.onContact({0.0f, 0.9f});   // ground
	tracker.onContact({-0.7f, 0.1f});  // another wall

	REQUIRE(tracker.isOnGround());
}

TEST_CASE("isOneWayPassable - falling onto platform blocks", "[physics][collision]")
{
	// 落下中 (velocity.y > 0) + 上向き法線 (0, -1) → dot < 0 → 着地可能
	REQUIRE_FALSE(sgc::physics::isOneWayPassable({0.0f, 5.0f}, {0.0f, -1.0f}));
}

TEST_CASE("isOneWayPassable - jumping up passes through", "[physics][collision]")
{
	// 上昇中 (velocity.y < 0) + 上向き法線 (0, -1) → dot > 0 → 通過
	REQUIRE(sgc::physics::isOneWayPassable({0.0f, -5.0f}, {0.0f, -1.0f}));
}

TEST_CASE("isCeilingContact - upward normal is ceiling", "[physics][collision]")
{
	REQUIRE(sgc::physics::isCeilingContact({0.0f, -0.8f}));
}

TEST_CASE("isCeilingContact - downward normal is not ceiling", "[physics][collision]")
{
	REQUIRE_FALSE(sgc::physics::isCeilingContact({0.0f, 0.8f}));
}
