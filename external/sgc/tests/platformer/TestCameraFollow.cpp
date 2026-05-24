#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/platformer/CameraFollow.hpp"

using namespace sgc::platformer;

TEST_CASE("CameraFollow - Target inside dead zone no movement", "[platformer][camera]")
{
	CameraFollowConfig config;
	config.smoothing = 1.0f;  // Instant smoothing
	config.deadZone = {-50.0f, -50.0f, 100.0f, 100.0f};

	const sgc::Vec2f camPos{100.0f, 100.0f};
	const sgc::Vec2f target{110.0f, 110.0f};  // Within dead zone
	const float dt = 1.0f / 60.0f;

	auto result = updateCamera(camPos, target, config, dt);
	// Camera should barely move when target is inside dead zone
	REQUIRE(result.x == Catch::Approx(100.0f).margin(1.0f));
	REQUIRE(result.y == Catch::Approx(100.0f).margin(1.0f));
}

TEST_CASE("CameraFollow - Target outside dead zone moves camera", "[platformer][camera]")
{
	CameraFollowConfig config;
	config.smoothing = 1.0f;
	config.deadZone = {-10.0f, -10.0f, 20.0f, 20.0f};

	const sgc::Vec2f camPos{100.0f, 100.0f};
	const sgc::Vec2f target{200.0f, 100.0f};  // Far outside dead zone right
	const float dt = 1.0f / 60.0f;

	auto result = updateCamera(camPos, target, config, dt);
	REQUIRE(result.x > camPos.x);
}

TEST_CASE("CameraFollow - World bounds clamp camera position", "[platformer][camera]")
{
	CameraFollowConfig config;
	config.smoothing = 1.0f;
	config.deadZone = {-5.0f, -5.0f, 10.0f, 10.0f};
	config.worldBounds = sgc::Rectf{0.0f, 0.0f, 500.0f, 300.0f};

	const sgc::Vec2f camPos{10.0f, 10.0f};
	const sgc::Vec2f target{-100.0f, -100.0f};  // Way outside bounds
	const float dt = 1.0f / 60.0f;

	auto result = updateCamera(camPos, target, config, dt);
	REQUIRE(result.x >= 0.0f);
	REQUIRE(result.y >= 0.0f);
}

TEST_CASE("CameraFollow - Smoothing delays camera movement", "[platformer][camera]")
{
	CameraFollowConfig config;
	config.smoothing = 0.01f;  // Very slow smoothing
	config.deadZone = {-5.0f, -5.0f, 10.0f, 10.0f};

	const sgc::Vec2f camPos{0.0f, 0.0f};
	const sgc::Vec2f target{500.0f, 0.0f};
	const float dt = 1.0f / 60.0f;

	auto result = updateCamera(camPos, target, config, dt);
	// Should move towards target but not reach it instantly
	REQUIRE(result.x > 0.0f);
	REQUIRE(result.x < 500.0f);
}

TEST_CASE("CameraFollow - Vertical bias offsets camera", "[platformer][camera]")
{
	CameraFollowConfig config;
	config.smoothing = 1.0f;
	config.deadZone = {-5.0f, -5.0f, 10.0f, 10.0f};
	config.verticalBias = 50.0f;

	const sgc::Vec2f camPos{100.0f, 100.0f};
	const sgc::Vec2f target{200.0f, 100.0f};  // Outside dead zone
	const float dt = 1.0f / 60.0f;

	auto result = updateCamera(camPos, target, config, dt);
	// Y should be biased downward
	REQUIRE(result.y > 100.0f);
}

TEST_CASE("CameraFollow - No world bounds means no clamp", "[platformer][camera]")
{
	CameraFollowConfig config;
	config.smoothing = 1.0f;
	config.deadZone = {-5.0f, -5.0f, 10.0f, 10.0f};
	// worldBounds is nullopt by default

	const sgc::Vec2f camPos{0.0f, 0.0f};
	const sgc::Vec2f target{-500.0f, -500.0f};
	const float dt = 1.0f / 60.0f;

	auto result = updateCamera(camPos, target, config, dt);
	REQUIRE(result.x < 0.0f);
	REQUIRE(result.y < 0.0f);
}

TEST_CASE("CameraFollow - LookAhead offsets in movement direction", "[platformer][camera]")
{
	CameraFollowConfig config;
	config.smoothing = 1.0f;
	config.deadZone = {-5.0f, -5.0f, 10.0f, 10.0f};
	config.lookAhead = 30.0f;

	const sgc::Vec2f camPos{0.0f, 0.0f};
	const sgc::Vec2f targetRight{100.0f, 0.0f};
	const float dt = 1.0f / 60.0f;

	auto resultR = updateCamera(camPos, targetRight, config, dt);

	// Reset and go left
	const sgc::Vec2f targetLeft{-100.0f, 0.0f};
	auto resultL = updateCamera(camPos, targetLeft, config, dt);

	// Right target should have camera further right than left target
	REQUIRE(resultR.x > resultL.x);
}
