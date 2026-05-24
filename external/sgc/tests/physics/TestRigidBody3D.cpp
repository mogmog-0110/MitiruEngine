#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/physics/RigidBody3D.hpp"

using namespace sgc;
using namespace sgc::physics;
using Catch::Matchers::WithinAbs;

TEST_CASE("RigidBody3D inverseMass", "[physics][rigidbody3d]")
{
	RigidBody3D body;
	body.mass = 2.0f;
	CHECK_THAT(body.inverseMass(), WithinAbs(0.5, 1e-6));

	body.isStatic = true;
	CHECK(body.inverseMass() == 0.0f);
}

TEST_CASE("RigidBody3D applyForce changes acceleration", "[physics][rigidbody3d]")
{
	RigidBody3D body;
	body.mass = 2.0f;
	body.applyForce({10.0f, 0, 0});
	CHECK_THAT(body.acceleration.x, WithinAbs(5.0, 1e-6));
}

TEST_CASE("RigidBody3D applyImpulse changes velocity", "[physics][rigidbody3d]")
{
	RigidBody3D body;
	body.mass = 2.0f;
	body.applyImpulse({10.0f, 0, 0});
	CHECK_THAT(body.velocity.x, WithinAbs(5.0, 1e-6));
}

TEST_CASE("RigidBody3D integrate updates position", "[physics][rigidbody3d]")
{
	RigidBody3D body;
	body.mass = 1.0f;
	body.linearDamping = 0.0f;
	body.velocity = {1.0f, 0, 0};
	body.integrate(1.0f);

	CHECK_THAT(body.position.x, WithinAbs(1.0, 1e-4));
	// 加速度はリセットされる
	CHECK(body.acceleration == Vec3f::zero());
}

TEST_CASE("RigidBody3D static body does not move", "[physics][rigidbody3d]")
{
	RigidBody3D body;
	body.isStatic = true;
	body.applyForce({100.0f, 0, 0});
	body.integrate(1.0f);

	CHECK(body.position == Vec3f::zero());
	CHECK(body.velocity == Vec3f::zero());
}
