#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/physics/Collider2D.hpp"
#include "sgc/physics/PhysicsWorld2D.hpp"

using namespace sgc;
using namespace sgc::physics;
using Catch::Approx;

// ── RigidBody2D creation tests ──

TEST_CASE("RigidBody2D - createDynamic sets correct properties", "[physics][Physics2D]")
{
	auto body = RigidBody2D<float>::createDynamic(5.0f, {10.0f, 20.0f});

	REQUIRE(body.mass == Approx(5.0f));
	REQUIRE(body.inverseMass == Approx(0.2f));
	REQUIRE(body.position.x == Approx(10.0f));
	REQUIRE(body.position.y == Approx(20.0f));
	REQUIRE(body.type == RigidBody2DType::Dynamic);
	REQUIRE_FALSE(body.isStatic);
}

TEST_CASE("RigidBody2D - createStatic sets zero mass", "[physics][Physics2D]")
{
	auto body = RigidBody2D<float>::createStatic({5.0f, 5.0f});

	REQUIRE(body.mass == Approx(0.0f));
	REQUIRE(body.inverseMass == Approx(0.0f));
	REQUIRE(body.position.x == Approx(5.0f));
	REQUIRE(body.type == RigidBody2DType::Static);
	REQUIRE(body.isStatic);
}

TEST_CASE("RigidBody2D - createKinematic ignores forces", "[physics][Physics2D]")
{
	auto body = RigidBody2D<float>::createKinematic({0.0f, 0.0f});

	REQUIRE(body.type == RigidBody2DType::Kinematic);
	REQUIRE(body.inverseMass == Approx(0.0f));

	body.applyForce({100.0f, 100.0f});
	REQUIRE(body.acceleration.x == Approx(0.0f));
	REQUIRE(body.acceleration.y == Approx(0.0f));

	body.applyImpulse({100.0f, 100.0f});
	REQUIRE(body.velocity.x == Approx(0.0f));
	REQUIRE(body.velocity.y == Approx(0.0f));
}

TEST_CASE("RigidBody2D - integrate applies velocity to position", "[physics][Physics2D]")
{
	auto body = RigidBody2D<float>::createDynamic(1.0f, {0.0f, 0.0f});
	body.velocity = {10.0f, 5.0f};

	body.integrate(0.5f);

	REQUIRE(body.position.x == Approx(5.0f));
	REQUIRE(body.position.y == Approx(2.5f));
}

TEST_CASE("RigidBody2D - applyForce changes acceleration for dynamic body", "[physics][Physics2D]")
{
	auto body = RigidBody2D<float>::createDynamic(2.0f);
	body.applyForce({10.0f, 0.0f});

	REQUIRE(body.acceleration.x == Approx(5.0f));
}

TEST_CASE("RigidBody2D - applyImpulse changes velocity for dynamic body", "[physics][Physics2D]")
{
	auto body = RigidBody2D<float>::createDynamic(2.0f);
	body.applyImpulse({10.0f, 0.0f});

	REQUIRE(body.velocity.x == Approx(5.0f));
}

TEST_CASE("RigidBody2D - applyTorque accumulates torque", "[physics][Physics2D]")
{
	auto body = RigidBody2D<float>::createDynamic(1.0f);
	body.applyTorque(5.0f);
	body.applyTorque(3.0f);

	REQUIRE(body.torque == Approx(8.0f));
}

TEST_CASE("RigidBody2D - fixedRotation prevents angular velocity update", "[physics][Physics2D]")
{
	auto body = RigidBody2D<float>::createDynamic(1.0f);
	body.fixedRotation = true;
	body.applyTorque(10.0f);

	REQUIRE(body.torque == Approx(0.0f));

	body.angularVelocity = 5.0f;
	body.integrate(1.0f);

	REQUIRE(body.rotation == Approx(0.0f));
}

// ── Collider2D collision tests ──

TEST_CASE("Circle-circle collision detected", "[physics][Collider2D]")
{
	const CircleCollider a{{0.0f, 0.0f}, 5.0f};
	const CircleCollider b{{8.0f, 0.0f}, 5.0f};

	const auto contact = testCircleCircle(a, b);

	REQUIRE(contact.has_value());
	REQUIRE(contact->penetration == Approx(2.0f));
	REQUIRE(contact->normal.x == Approx(1.0f));
	REQUIRE(contact->normal.y == Approx(0.0f));
}

TEST_CASE("Circle-circle no collision when separated", "[physics][Collider2D]")
{
	const CircleCollider a{{0.0f, 0.0f}, 3.0f};
	const CircleCollider b{{10.0f, 0.0f}, 3.0f};

	const auto contact = testCircleCircle(a, b);

	REQUIRE_FALSE(contact.has_value());
}

TEST_CASE("Circle-AABB collision detected", "[physics][Collider2D]")
{
	const CircleCollider circle{{0.0f, 0.0f}, 5.0f};
	const AABBCollider aabb{{3.0f, -5.0f}, {13.0f, 5.0f}};

	const auto contact = testCircleAABB(circle, aabb);

	REQUIRE(contact.has_value());
	REQUIRE(contact->penetration > 0.0f);
}

TEST_CASE("AABB-AABB collision detected", "[physics][Collider2D]")
{
	const AABBCollider a{{0.0f, 0.0f}, {10.0f, 10.0f}};
	const AABBCollider b{{8.0f, 3.0f}, {18.0f, 13.0f}};

	const auto contact = testAABBAABB(a, b);

	REQUIRE(contact.has_value());
	REQUIRE(contact->penetration == Approx(2.0f));
}

TEST_CASE("AABB-AABB no collision when separated", "[physics][Collider2D]")
{
	const AABBCollider a{{0.0f, 0.0f}, {5.0f, 5.0f}};
	const AABBCollider b{{6.0f, 0.0f}, {10.0f, 5.0f}};

	const auto contact = testAABBAABB(a, b);

	REQUIRE_FALSE(contact.has_value());
}

TEST_CASE("Circle-OBB collision detected", "[physics][Collider2D]")
{
	const CircleCollider circle{{0.0f, 0.0f}, 3.0f};
	const OBBCollider obb{{2.0f, 0.0f}, {2.0f, 2.0f}, 0.0f};

	const auto contact = testCircleOBB(circle, obb);

	REQUIRE(contact.has_value());
	REQUIRE(contact->penetration > 0.0f);
}

TEST_CASE("pointInCircle returns true for inside point", "[physics][Collider2D]")
{
	const CircleCollider circle{{5.0f, 5.0f}, 3.0f};

	REQUIRE(pointInCircle({5.0f, 5.0f}, circle));
	REQUIRE(pointInCircle({6.0f, 5.0f}, circle));
	REQUIRE_FALSE(pointInCircle({9.0f, 5.0f}, circle));
}

TEST_CASE("pointInAABB returns true for inside point", "[physics][Collider2D]")
{
	const AABBCollider aabb{{0.0f, 0.0f}, {10.0f, 10.0f}};

	REQUIRE(pointInAABB({5.0f, 5.0f}, aabb));
	REQUIRE(pointInAABB({0.0f, 0.0f}, aabb));
	REQUIRE_FALSE(pointInAABB({-1.0f, 5.0f}, aabb));
}

// ── ManagedPhysicsWorld2D tests ──

TEST_CASE("ManagedPhysicsWorld2D - gravity integration moves dynamic body", "[physics][Physics2D]")
{
	ManagedPhysicsWorld2D world;
	world.setGravity({0.0f, 100.0f});

	auto body = RigidBody2D<float>::createDynamic(1.0f, {0.0f, 0.0f});
	CircleCollider collider{{0.0f, 0.0f}, 5.0f};
	auto id = world.addBody(body, collider);

	world.step(1.0f);

	const auto* result = world.getBody(id);
	REQUIRE(result != nullptr);
	REQUIRE(result->position.y > 0.0f);
}

TEST_CASE("ManagedPhysicsWorld2D - collision callback fires on overlap", "[physics][Physics2D]")
{
	ManagedPhysicsWorld2D world;

	auto bodyA = RigidBody2D<float>::createDynamic(1.0f, {0.0f, 0.0f});
	auto bodyB = RigidBody2D<float>::createDynamic(1.0f, {5.0f, 0.0f});

	CircleCollider colliderA{{0.0f, 0.0f}, 5.0f};
	CircleCollider colliderB{{0.0f, 0.0f}, 5.0f};

	auto idA = world.addBody(bodyA, colliderA);
	auto idB = world.addBody(bodyB, colliderB);

	bool callbackFired = false;
	BodyId2D cbA = INVALID_BODY_ID_2D;
	BodyId2D cbB = INVALID_BODY_ID_2D;

	world.onCollision([&](BodyId2D a, BodyId2D b, const Contact2D&)
	{
		callbackFired = true;
		cbA = a;
		cbB = b;
	});

	world.step(1.0f / 60.0f);

	REQUIRE(callbackFired);
	REQUIRE(cbA == idA);
	REQUIRE(cbB == idB);
}

TEST_CASE("ManagedPhysicsWorld2D - static body does not move under gravity", "[physics][Physics2D]")
{
	ManagedPhysicsWorld2D world;
	world.setGravity({0.0f, 100.0f});

	auto body = RigidBody2D<float>::createStatic({50.0f, 300.0f});
	AABBCollider collider{{-50.0f, -5.0f}, {50.0f, 5.0f}};
	auto id = world.addBody(body, collider);

	world.step(1.0f);

	const auto* result = world.getBody(id);
	REQUIRE(result != nullptr);
	REQUIRE(result->position.x == Approx(50.0f));
	REQUIRE(result->position.y == Approx(300.0f));
}

TEST_CASE("ManagedPhysicsWorld2D - kinematic body ignores forces but moves by velocity", "[physics][Physics2D]")
{
	ManagedPhysicsWorld2D world;
	world.setGravity({0.0f, 100.0f});

	auto body = RigidBody2D<float>::createKinematic({0.0f, 0.0f});
	body.velocity = {10.0f, 0.0f};
	CircleCollider collider{{0.0f, 0.0f}, 5.0f};
	auto id = world.addBody(body, collider);

	world.step(1.0f);

	const auto* result = world.getBody(id);
	REQUIRE(result != nullptr);
	// キネマティックは重力の影響を受けないが速度で移動する
	REQUIRE(result->position.x > 0.0f);
	// Y方向の重力は無視される（applyForceが効かない）
	REQUIRE(result->position.y == Approx(0.0f));
}

TEST_CASE("ManagedPhysicsWorld2D - removeBody decreases body count", "[physics][Physics2D]")
{
	ManagedPhysicsWorld2D world;

	auto bodyA = RigidBody2D<float>::createDynamic(1.0f);
	auto bodyB = RigidBody2D<float>::createDynamic(1.0f);
	CircleCollider collider{{0.0f, 0.0f}, 5.0f};

	auto idA = world.addBody(bodyA, collider);
	[[maybe_unused]] auto idB = world.addBody(bodyB, collider);
	REQUIRE(world.bodyCount() == 2);

	world.removeBody(idA);
	REQUIRE(world.bodyCount() == 1);

	// getBody returns nullptr for removed body
	REQUIRE(world.getBody(idA) == nullptr);
}

TEST_CASE("ManagedPhysicsWorld2D - trigger callback fires without physics response", "[physics][Physics2D]")
{
	ManagedPhysicsWorld2D world;

	auto bodyA = RigidBody2D<float>::createDynamic(1.0f, {0.0f, 0.0f});
	auto bodyB = RigidBody2D<float>::createDynamic(1.0f, {3.0f, 0.0f});

	CircleCollider colliderA{{0.0f, 0.0f}, 5.0f};
	CircleCollider colliderB{{0.0f, 0.0f}, 5.0f};

	[[maybe_unused]] auto idA = world.addBody(bodyA, colliderA);
	[[maybe_unused]] auto idB = world.addTriggerBody(bodyB, colliderB);

	bool triggerFired = false;
	bool collisionFired = false;

	world.onTrigger([&](BodyId2D, BodyId2D, const Contact2D&)
	{
		triggerFired = true;
	});
	world.onCollision([&](BodyId2D, BodyId2D, const Contact2D&)
	{
		collisionFired = true;
	});

	world.step(1.0f / 60.0f);

	REQUIRE(triggerFired);
	REQUIRE_FALSE(collisionFired);
}
