#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/platformer/PlatformerPhysics.hpp"

using namespace sgc::platformer;

TEST_CASE("PlatformerBody - Default state", "[platformer][physics]")
{
	PlatformerBody body;
	REQUIRE(body.position.x == 0.0f);
	REQUIRE(body.position.y == 0.0f);
	REQUIRE(body.grounded == false);
	REQUIRE(body.facingRight == true);
}

TEST_CASE("PlatformerPhysics - Gravity applies when not grounded", "[platformer][physics]")
{
	PlatformerBody body;
	PlatformerConfig config;
	PlatformerInput input;
	const float dt = 1.0f / 60.0f;

	auto next = updatePlatformerBody(body, config, input, dt);
	REQUIRE(next.velocity.y > 0.0f);
	REQUIRE(next.position.y > 0.0f);
}

TEST_CASE("PlatformerPhysics - No gravity when grounded", "[platformer][physics]")
{
	PlatformerBody body;
	body.grounded = true;
	PlatformerConfig config;
	PlatformerInput input;
	const float dt = 1.0f / 60.0f;

	auto next = updatePlatformerBody(body, config, input, dt);
	REQUIRE(next.velocity.y == 0.0f);
}

TEST_CASE("PlatformerPhysics - Horizontal movement", "[platformer][physics]")
{
	PlatformerBody body;
	body.grounded = true;
	PlatformerConfig config;
	config.walkSpeed = 200.0f;
	PlatformerInput input;
	input.moveX = 1.0f;
	const float dt = 1.0f / 60.0f;

	auto next = updatePlatformerBody(body, config, input, dt);
	REQUIRE(next.velocity.x == Catch::Approx(200.0f));
	REQUIRE(next.position.x > 0.0f);
	REQUIRE(next.facingRight == true);
}

TEST_CASE("PlatformerPhysics - Facing direction updates", "[platformer][physics]")
{
	PlatformerBody body;
	body.grounded = true;
	PlatformerConfig config;
	PlatformerInput input;
	input.moveX = -1.0f;
	const float dt = 1.0f / 60.0f;

	auto next = updatePlatformerBody(body, config, input, dt);
	REQUIRE(next.facingRight == false);
}

TEST_CASE("PlatformerPhysics - Jump when grounded", "[platformer][physics]")
{
	PlatformerBody body;
	body.grounded = true;
	PlatformerConfig config;
	config.jumpForce = 400.0f;
	PlatformerInput input;
	input.jumpPressed = true;
	input.jumpHeld = true;
	const float dt = 1.0f / 60.0f;

	auto next = updatePlatformerBody(body, config, input, dt);
	REQUIRE(next.velocity.y < 0.0f);
	REQUIRE(next.jumping == true);
	REQUIRE(next.grounded == false);
}

TEST_CASE("PlatformerPhysics - Coyote time allows late jump", "[platformer][physics]")
{
	PlatformerBody body;
	body.grounded = false;
	body.coyoteTimer = 0.05f;  // Still within coyote time
	PlatformerConfig config;
	config.jumpForce = 400.0f;
	PlatformerInput input;
	input.jumpPressed = true;
	input.jumpHeld = true;
	const float dt = 1.0f / 60.0f;

	auto next = updatePlatformerBody(body, config, input, dt);
	REQUIRE(next.velocity.y < 0.0f);
	REQUIRE(next.jumping == true);
}

TEST_CASE("PlatformerPhysics - Jump buffer allows early jump", "[platformer][physics]")
{
	// First: press jump while in air (buffer it)
	PlatformerBody body;
	body.grounded = false;
	body.jumpBufferTimer = 0.05f;  // Jump was pressed recently
	body.coyoteTimer = 0.0f;
	PlatformerConfig config;
	PlatformerInput input;  // No jump pressed this frame
	const float dt = 1.0f / 60.0f;

	// Not grounded, no coyote => no jump
	auto next = updatePlatformerBody(body, config, input, dt);
	REQUIRE(next.jumping == false);

	// Now land (set grounded + coyote)
	next.grounded = true;
	next.coyoteTimer = config.coyoteTime;
	auto landed = updatePlatformerBody(next, config, input, dt);
	// Buffer still active => jump happens
	REQUIRE(landed.velocity.y < 0.0f);
}

TEST_CASE("PlatformerPhysics - Max fall speed clamped", "[platformer][physics]")
{
	PlatformerBody body;
	body.velocity.y = 10000.0f;
	PlatformerConfig config;
	config.maxFallSpeed = 600.0f;
	PlatformerInput input;
	const float dt = 1.0f / 60.0f;

	auto next = updatePlatformerBody(body, config, input, dt);
	REQUIRE(next.velocity.y <= config.maxFallSpeed);
}

TEST_CASE("PlatformerPhysics - Jump cut reduces velocity", "[platformer][physics]")
{
	PlatformerBody body;
	body.jumping = true;
	body.velocity.y = -300.0f;
	PlatformerConfig config;
	config.jumpCutMultiplier = 0.5f;
	PlatformerInput input;
	input.jumpHeld = false;  // Released jump
	const float dt = 1.0f / 60.0f;

	auto next = updatePlatformerBody(body, config, input, dt);
	// Velocity should be reduced by cut multiplier before gravity
	// -300 * 0.5 = -150, then gravity applied
	REQUIRE(next.velocity.y > -300.0f);
}

TEST_CASE("PlatformerPhysics - Air control factor applied", "[platformer][physics]")
{
	PlatformerBody body;
	body.grounded = false;
	PlatformerConfig config;
	config.walkSpeed = 200.0f;
	config.airControlFactor = 0.5f;
	PlatformerInput input;
	input.moveX = 1.0f;
	const float dt = 1.0f / 60.0f;

	auto next = updatePlatformerBody(body, config, input, dt);
	REQUIRE(next.velocity.x == Catch::Approx(100.0f));
}
