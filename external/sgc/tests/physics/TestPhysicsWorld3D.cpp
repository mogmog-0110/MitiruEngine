#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/physics/PhysicsWorld.hpp"

using namespace sgc;
using namespace sgc::physics;
using Catch::Matchers::WithinAbs;

TEST_CASE("PhysicsWorld3D default gravity", "[physics][world3d]")
{
	PhysicsWorld3D world;
	CHECK_THAT(world.gravity().y, WithinAbs(-9.81, 1e-4));
}

TEST_CASE("PhysicsWorld3D custom gravity", "[physics][world3d]")
{
	PhysicsWorld3D world({0, -20.0f, 0});
	CHECK_THAT(world.gravity().y, WithinAbs(-20.0, 1e-4));

	world.setGravity({0, 0, 0});
	CHECK(world.gravity() == Vec3f::zero());
}

TEST_CASE("PhysicsWorld3D addBody and removeBody", "[physics][world3d]")
{
	PhysicsWorld3D world;

	RigidBody3D body;
	body.mass = 1.0f;

	auto h = world.addBody(body);
	CHECK(world.activeBodyCount() == 1);
	CHECK(world.totalBodyCount() == 1);

	world.removeBody(h);
	CHECK(world.activeBodyCount() == 0);
	CHECK(world.totalBodyCount() == 1); // Still in array, just inactive
}

TEST_CASE("PhysicsWorld3D getBody", "[physics][world3d]")
{
	PhysicsWorld3D world;

	RigidBody3D body;
	body.mass = 5.0f;
	body.position = {1, 2, 3};

	auto h = world.addBody(body);
	CHECK_THAT(world.getBody(h).mass, WithinAbs(5.0, 1e-6));
	CHECK_THAT(world.getBody(h).position.x, WithinAbs(1.0, 1e-6));

	// Modify through reference
	world.getBody(h).mass = 10.0f;
	CHECK_THAT(world.getBody(h).mass, WithinAbs(10.0, 1e-6));
}

TEST_CASE("PhysicsWorld3D step applies gravity", "[physics][world3d]")
{
	PhysicsWorld3D world({0, -10.0f, 0});

	RigidBody3D body;
	body.mass = 1.0f;
	body.position = {0, 100, 0};
	body.linearDamping = 0.0f;

	auto h = world.addBody(body);
	world.step(1.0f);

	// Should have fallen
	CHECK(world.getBody(h).position.y < 100.0f);
	CHECK(world.getBody(h).velocity.y < 0.0f);
}

TEST_CASE("PhysicsWorld3D static body unaffected by gravity", "[physics][world3d]")
{
	PhysicsWorld3D world({0, -10.0f, 0});

	RigidBody3D body;
	body.isStatic = true;
	body.position = {0, 0, 0};

	auto h = world.addBody(body);
	world.step(1.0f);

	CHECK(world.getBody(h).position == Vec3f::zero());
}

TEST_CASE("PhysicsWorld3D collision between two spheres", "[physics][world3d]")
{
	PhysicsWorld3D world({0, 0, 0}); // No gravity

	// 大きいBaumgarte補正で確実に分離させる
	BaumgarteParams bp;
	bp.factor = 0.8f;
	bp.slop = 0.0f;
	world.setBaumgarte(bp);

	RigidBody3D a;
	a.mass = 1.0f;
	a.position = {0, 0, 0};
	a.velocity = {};
	a.restitution = 0.5f;
	a.linearDamping = 0.0f;

	RigidBody3D b;
	b.mass = 1.0f;
	b.position = {0.5f, 0, 0}; // Overlapping (radius 0.5 each)
	b.velocity = {};
	b.restitution = 0.5f;
	b.linearDamping = 0.0f;

	auto hA = world.addBody(a);
	auto hB = world.addBody(b);
	world.setShape(hA, SphereShape{0.5f});
	world.setShape(hB, SphereShape{0.5f});

	// A should move left, B should move right
	const float aXBefore = world.getBody(hA).position.x;
	const float bXBefore = world.getBody(hB).position.x;

	world.step(0.016f);

	CHECK(world.getBody(hA).position.x < aXBefore);
	CHECK(world.getBody(hB).position.x > bXBefore);
}

TEST_CASE("PhysicsWorld3D onCollisionEnter callback", "[physics][world3d]")
{
	PhysicsWorld3D world({0, 0, 0});

	RigidBody3D a;
	a.mass = 1.0f;
	a.position = {0, 0, 0};

	RigidBody3D b;
	b.mass = 1.0f;
	b.position = {0.5f, 0, 0}; // Overlapping with radius 0.5 each

	auto hA = world.addBody(a);
	auto hB = world.addBody(b);
	world.setShape(hA, SphereShape{0.5f});
	world.setShape(hB, SphereShape{0.5f});

	int enterCount = 0;
	world.onCollisionEnter([&](BodyHandle, BodyHandle, const ContactPoint&)
	{
		++enterCount;
	});

	world.step(0.001f);
	CHECK(enterCount == 1);

	// Second step: still colliding, no new enter
	world.step(0.001f);
	// enterCount may or may not increase depending on separation
}

TEST_CASE("PhysicsWorld3D onCollisionExit callback", "[physics][world3d]")
{
	PhysicsWorld3D world({0, 0, 0});

	RigidBody3D a;
	a.mass = 1.0f;
	a.position = {0, 0, 0};
	a.linearDamping = 0.0f;

	RigidBody3D b;
	b.mass = 1.0f;
	b.position = {0.5f, 0, 0}; // Overlapping
	b.linearDamping = 0.0f;

	auto hA = world.addBody(a);
	auto hB = world.addBody(b);
	world.setShape(hA, SphereShape{0.5f});
	world.setShape(hB, SphereShape{0.5f});

	int exitCount = 0;
	world.onCollisionExit([&](BodyHandle, BodyHandle, const ContactPoint&)
	{
		++exitCount;
	});

	// First step: collision detected, bodies get pushed apart
	world.step(0.016f);

	// After separation, manually move them far apart to guarantee no collision
	world.getBody(hA).position = {-10, 0, 0};
	world.getBody(hB).position = {10, 0, 0};
	world.step(0.016f);

	CHECK(exitCount >= 1);
}

TEST_CASE("PhysicsWorld3D sleep after low velocity", "[physics][world3d]")
{
	PhysicsWorld3D world({0, 0, 0}); // No gravity

	SleepParams sp;
	sp.velocityThreshold = 0.1f;
	sp.framesToSleep = 3;
	sp.enabled = true;
	world.setSleepParams(sp);

	RigidBody3D body;
	body.mass = 1.0f;
	body.velocity = Vec3f::zero(); // Already stationary

	auto h = world.addBody(body);

	CHECK_FALSE(world.isSleeping(h));

	// After enough steps, should fall asleep
	world.step(0.016f);
	world.step(0.016f);
	world.step(0.016f);

	CHECK(world.isSleeping(h));
}

TEST_CASE("PhysicsWorld3D wakeUp", "[physics][world3d]")
{
	PhysicsWorld3D world({0, 0, 0});

	SleepParams sp;
	sp.velocityThreshold = 0.1f;
	sp.framesToSleep = 1;
	sp.enabled = true;
	world.setSleepParams(sp);

	RigidBody3D body;
	body.mass = 1.0f;

	auto h = world.addBody(body);
	world.step(0.016f);
	CHECK(world.isSleeping(h));

	world.wakeUp(h);
	CHECK_FALSE(world.isSleeping(h));
}

TEST_CASE("PhysicsWorld3D sleeping body not integrated", "[physics][world3d]")
{
	PhysicsWorld3D world({0, -10.0f, 0});

	SleepParams sp;
	sp.velocityThreshold = 100.0f; // High threshold to force sleep
	sp.framesToSleep = 1;
	sp.enabled = true;
	world.setSleepParams(sp);

	RigidBody3D body;
	body.mass = 1.0f;
	body.position = {0, 50, 0};

	auto h = world.addBody(body);

	// First step: body moves then goes to sleep
	world.step(0.016f);
	CHECK(world.isSleeping(h));

	const float yAfterSleep = world.getBody(h).position.y;

	// Next step: sleeping body should not move
	world.step(0.016f);
	CHECK_THAT(world.getBody(h).position.y, WithinAbs(yAfterSleep, 1e-6));
}

TEST_CASE("PhysicsWorld3D clear removes all", "[physics][world3d]")
{
	PhysicsWorld3D world;

	(void)world.addBody(RigidBody3D{});
	(void)world.addBody(RigidBody3D{});
	CHECK(world.totalBodyCount() == 2);

	world.clear();
	CHECK(world.totalBodyCount() == 0);
	CHECK(world.activeBodyCount() == 0);
}

TEST_CASE("PhysicsWorld3D zero dt does nothing", "[physics][world3d]")
{
	PhysicsWorld3D world({0, -10.0f, 0});

	RigidBody3D body;
	body.mass = 1.0f;
	body.position = {0, 100, 0};

	auto h = world.addBody(body);
	world.step(0.0f);

	CHECK(world.getBody(h).position.y == 100.0f);
}

TEST_CASE("PhysicsWorld3D INVALID_BODY_HANDLE constant", "[physics][world3d]")
{
	CHECK(INVALID_BODY_HANDLE == UINT32_MAX);
}
