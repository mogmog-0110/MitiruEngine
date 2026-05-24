#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/physics/RigidBody2D.hpp"

using namespace sgc;
using namespace sgc::physics;
using Catch::Approx;

TEST_CASE("RigidBody2D - applyForce changes acceleration", "[physics][RigidBody2D]")
{
	RigidBody2Df body;
	body.mass = 2.0f;
	body.applyForce({10.0f, 0.0f});

	REQUIRE(body.acceleration.x == Approx(5.0f));  // F/m = 10/2
	REQUIRE(body.acceleration.y == Approx(0.0f));
}

TEST_CASE("RigidBody2D - applyForce accumulates", "[physics][RigidBody2D]")
{
	RigidBody2Df body;
	body.mass = 1.0f;
	body.applyForce({3.0f, 0.0f});
	body.applyForce({2.0f, 5.0f});

	REQUIRE(body.acceleration.x == Approx(5.0f));
	REQUIRE(body.acceleration.y == Approx(5.0f));
}

TEST_CASE("RigidBody2D - applyImpulse changes velocity", "[physics][RigidBody2D]")
{
	auto body = RigidBody2Df::createDynamic(2.0f);
	body.applyImpulse({10.0f, 0.0f});

	REQUIRE(body.velocity.x == Approx(5.0f));  // impulse * inverseMass = 10 * 0.5
	REQUIRE(body.velocity.y == Approx(0.0f));
}

TEST_CASE("RigidBody2D - integrate updates position", "[physics][RigidBody2D]")
{
	RigidBody2Df body;
	body.velocity = {10.0f, 0.0f};

	body.integrate(0.5f);

	REQUIRE(body.position.x == Approx(5.0f));  // v * dt = 10 * 0.5
	REQUIRE(body.position.y == Approx(0.0f));
}

TEST_CASE("RigidBody2D - integrate applies acceleration then resets", "[physics][RigidBody2D]")
{
	RigidBody2Df body;
	body.acceleration = {10.0f, 0.0f};

	body.integrate(1.0f);

	// velocity = acc * dt = 10
	REQUIRE(body.velocity.x == Approx(10.0f));
	// position = vel * dt = 10
	REQUIRE(body.position.x == Approx(10.0f));
	// acceleration should be reset
	REQUIRE(body.acceleration.x == Approx(0.0f));
	REQUIRE(body.acceleration.y == Approx(0.0f));
}

TEST_CASE("RigidBody2D - static body does not move", "[physics][RigidBody2D]")
{
	RigidBody2Df body;
	body.isStatic = true;
	body.position = {5.0f, 5.0f};

	body.applyForce({100.0f, 100.0f});
	body.applyImpulse({100.0f, 100.0f});
	body.integrate(1.0f);

	REQUIRE(body.position.x == Approx(5.0f));
	REQUIRE(body.position.y == Approx(5.0f));
	REQUIRE(body.velocity.x == Approx(0.0f));
	REQUIRE(body.velocity.y == Approx(0.0f));
}

TEST_CASE("RigidBody2D - drag reduces velocity", "[physics][RigidBody2D]")
{
	RigidBody2Df body;
	body.velocity = {100.0f, 0.0f};
	body.drag = 0.1f;

	body.integrate(1.0f);

	// velocity *= (1 - drag) = 100 * 0.9 = 90
	REQUIRE(body.velocity.x == Approx(90.0f));
}

TEST_CASE("RigidBody2D - bounds returns correct AABB", "[physics][RigidBody2D]")
{
	RigidBody2Df body;
	body.position = {10.0f, 20.0f};

	const Vec2f halfSize{5.0f, 3.0f};
	const auto aabb = body.bounds(halfSize);

	REQUIRE(aabb.min.x == Approx(5.0f));
	REQUIRE(aabb.min.y == Approx(17.0f));
	REQUIRE(aabb.max.x == Approx(15.0f));
	REQUIRE(aabb.max.y == Approx(23.0f));
}

TEST_CASE("RigidBody2D - zero mass ignores forces", "[physics][RigidBody2D]")
{
	RigidBody2Df body;
	body.mass = 0.0f;

	body.applyForce({10.0f, 10.0f});
	body.applyImpulse({10.0f, 10.0f});

	REQUIRE(body.acceleration.x == Approx(0.0f));
	REQUIRE(body.velocity.x == Approx(0.0f));
}

TEST_CASE("resolveRigidBodyCollision - dynamic vs static", "[physics][RigidBody2D]")
{
	RigidBody2Df dynamic_body;
	dynamic_body.position = {5.0f, 0.0f};
	dynamic_body.velocity = {10.0f, 0.0f};
	dynamic_body.mass = 1.0f;

	RigidBody2Df static_body;
	static_body.position = {10.0f, 0.0f};
	static_body.isStatic = true;

	CollisionResult2Df collision;
	collision.colliding = true;
	collision.normal = {1.0f, 0.0f};
	collision.penetration = 2.0f;

	const auto oldStaticPos = static_body.position;

	resolveRigidBodyCollision(dynamic_body, static_body, collision);

	// Dynamic body should be pushed back
	REQUIRE(dynamic_body.position.x == Approx(3.0f));
	// Static body should not move
	REQUIRE(static_body.position.x == Approx(oldStaticPos.x));
}

TEST_CASE("resolveRigidBodyCollision - two dynamic bodies", "[physics][RigidBody2D]")
{
	RigidBody2Df a;
	a.position = {0.0f, 0.0f};
	a.velocity = {5.0f, 0.0f};
	a.mass = 1.0f;
	a.restitution = 0.0f;

	RigidBody2Df b;
	b.position = {8.0f, 0.0f};
	b.velocity = {0.0f, 0.0f};
	b.mass = 1.0f;
	b.restitution = 0.0f;

	CollisionResult2Df collision;
	collision.colliding = true;
	collision.normal = {1.0f, 0.0f};
	collision.penetration = 2.0f;

	resolveRigidBodyCollision(a, b, collision);

	// Equal mass: each should be pushed half the penetration
	REQUIRE(a.position.x == Approx(-1.0f));
	REQUIRE(b.position.x == Approx(9.0f));
}

TEST_CASE("resolveRigidBodyCollision - restitution affects bounce", "[physics][RigidBody2D]")
{
	RigidBody2Df a;
	a.position = {0.0f, 0.0f};
	a.velocity = {10.0f, 0.0f};
	a.mass = 1.0f;
	a.restitution = 1.0f;  // 完全弾性衝突

	RigidBody2Df b;
	b.position = {8.0f, 0.0f};
	b.velocity = {0.0f, 0.0f};
	b.mass = 1.0f;
	b.restitution = 1.0f;

	CollisionResult2Df collision;
	collision.colliding = true;
	collision.normal = {1.0f, 0.0f};
	collision.penetration = 1.0f;

	resolveRigidBodyCollision(a, b, collision);

	// 完全弾性衝突: 速度が交換される（同質量）
	REQUIRE(a.velocity.x == Approx(0.0f));
	REQUIRE(b.velocity.x == Approx(10.0f));
}

TEST_CASE("resolveRigidBodyCollision - no collision does nothing", "[physics][RigidBody2D]")
{
	RigidBody2Df a;
	a.position = {0.0f, 0.0f};
	a.velocity = {5.0f, 0.0f};

	RigidBody2Df b;
	b.position = {10.0f, 0.0f};

	CollisionResult2Df collision;
	collision.colliding = false;

	resolveRigidBodyCollision(a, b, collision);

	REQUIRE(a.position.x == Approx(0.0f));
	REQUIRE(a.velocity.x == Approx(5.0f));
}

TEST_CASE("resolveRigidBodyCollision - both static does nothing", "[physics][RigidBody2D]")
{
	RigidBody2Df a;
	a.isStatic = true;
	a.position = {0.0f, 0.0f};

	RigidBody2Df b;
	b.isStatic = true;
	b.position = {5.0f, 0.0f};

	CollisionResult2Df collision;
	collision.colliding = true;
	collision.normal = {1.0f, 0.0f};
	collision.penetration = 1.0f;

	resolveRigidBodyCollision(a, b, collision);

	REQUIRE(a.position.x == Approx(0.0f));
	REQUIRE(b.position.x == Approx(5.0f));
}
