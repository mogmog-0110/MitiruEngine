#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/physics/CollisionResponse.hpp"

using namespace sgc;
using namespace sgc::physics;
using Catch::Matchers::WithinAbs;

TEST_CASE("resolveCollision basic impulse", "[physics][collision-response]")
{
	RigidBody3D a;
	a.mass = 1.0f;
	a.restitution = 1.0f;
	a.velocity = {1.0f, 0, 0};

	RigidBody3D b;
	b.mass = 1.0f;
	b.restitution = 1.0f;
	b.velocity = {-1.0f, 0, 0};

	ContactPoint contact;
	contact.normal = {1.0f, 0, 0};
	contact.penetrationDepth = 0.1f;

	auto impulse = resolveCollision(a, b, contact);

	// Perfectly elastic: velocities should swap
	CHECK_THAT(a.velocity.x, WithinAbs(-1.0, 1e-4));
	CHECK_THAT(b.velocity.x, WithinAbs(1.0, 1e-4));
	CHECK(impulse.x < 0.0f);
}

TEST_CASE("resolveCollision with zero restitution", "[physics][collision-response]")
{
	RigidBody3D a;
	a.mass = 1.0f;
	a.restitution = 0.0f;
	a.velocity = {2.0f, 0, 0};

	RigidBody3D b;
	b.mass = 1.0f;
	b.restitution = 0.0f;
	b.velocity = {0, 0, 0};

	ContactPoint contact;
	contact.normal = {1.0f, 0, 0};
	contact.penetrationDepth = 0.1f;

	resolveCollision(a, b, contact);

	// Perfectly inelastic: both should have same velocity
	CHECK_THAT(a.velocity.x, WithinAbs(1.0, 1e-4));
	CHECK_THAT(b.velocity.x, WithinAbs(1.0, 1e-4));
}

TEST_CASE("resolveCollision static body does not move", "[physics][collision-response]")
{
	RigidBody3D a;
	a.mass = 1.0f;
	a.restitution = 0.5f;
	a.velocity = {1.0f, 0, 0};

	RigidBody3D b;
	b.isStatic = true;
	b.velocity = {};

	ContactPoint contact;
	contact.normal = {1.0f, 0, 0};
	contact.penetrationDepth = 0.1f;

	resolveCollision(a, b, contact);

	// Static body velocity stays zero
	CHECK(b.velocity.x == 0.0f);
	// Dynamic body should bounce back
	CHECK(a.velocity.x < 0.0f);
}

TEST_CASE("resolveCollision both static does nothing", "[physics][collision-response]")
{
	RigidBody3D a;
	a.isStatic = true;

	RigidBody3D b;
	b.isStatic = true;

	ContactPoint contact;
	contact.normal = {1.0f, 0, 0};
	contact.penetrationDepth = 0.1f;

	auto impulse = resolveCollision(a, b, contact);
	CHECK(impulse == Vec3f::zero());
}

TEST_CASE("resolveCollision separating bodies no impulse", "[physics][collision-response]")
{
	RigidBody3D a;
	a.mass = 1.0f;
	a.velocity = {-1.0f, 0, 0};

	RigidBody3D b;
	b.mass = 1.0f;
	b.velocity = {1.0f, 0, 0};

	ContactPoint contact;
	contact.normal = {1.0f, 0, 0};
	contact.penetrationDepth = 0.1f;

	auto impulse = resolveCollision(a, b, contact);

	// Already separating: no impulse
	CHECK(impulse == Vec3f::zero());
}

TEST_CASE("resolveCollision with friction", "[physics][collision-response]")
{
	RigidBody3D a;
	a.mass = 1.0f;
	a.restitution = 0.0f;
	a.velocity = {2.0f, 0, 1.0f};

	RigidBody3D b;
	b.mass = 1.0f;
	b.restitution = 0.0f;
	b.velocity = {};

	ContactPoint contact;
	contact.normal = {1.0f, 0, 0};
	contact.penetrationDepth = 0.1f;

	FrictionParams friction{0.5f, 0.4f};
	resolveCollision(a, b, contact, friction);

	// Z-velocity should be reduced by friction
	CHECK(std::abs(a.velocity.z) < 1.0f);
}

TEST_CASE("separateBodies corrects positions", "[physics][collision-response]")
{
	RigidBody3D a;
	a.mass = 1.0f;
	a.position = {0, 0, 0};

	RigidBody3D b;
	b.mass = 1.0f;
	b.position = {0.8f, 0, 0};

	ContactPoint contact;
	contact.normal = {1.0f, 0, 0};
	contact.penetrationDepth = 0.2f;

	BaumgarteParams params{1.0f, 0.0f}; // Full correction, no slop
	separateBodies(a, b, contact, params);

	// Bodies should be pushed apart
	CHECK(a.position.x < 0.0f);
	CHECK(b.position.x > 0.8f);
}

TEST_CASE("separateBodies respects slop", "[physics][collision-response]")
{
	RigidBody3D a;
	a.mass = 1.0f;
	a.position = {0, 0, 0};

	RigidBody3D b;
	b.mass = 1.0f;
	b.position = {0.99f, 0, 0};

	ContactPoint contact;
	contact.normal = {1.0f, 0, 0};
	contact.penetrationDepth = 0.005f; // Less than default slop

	separateBodies(a, b, contact);

	// No correction because penetration < slop
	CHECK(a.position.x == 0.0f);
	CHECK(b.position.x == 0.99f);
}

TEST_CASE("separateBodies static body not moved", "[physics][collision-response]")
{
	RigidBody3D a;
	a.mass = 1.0f;
	a.position = {0, 0, 0};

	RigidBody3D b;
	b.isStatic = true;
	b.position = {0.8f, 0, 0};

	ContactPoint contact;
	contact.normal = {1.0f, 0, 0};
	contact.penetrationDepth = 0.2f;

	BaumgarteParams params{1.0f, 0.0f};
	separateBodies(a, b, contact, params);

	// Static body should not move
	CHECK(b.position.x == 0.8f);
	// Dynamic body takes full correction
	CHECK(a.position.x < 0.0f);
}

TEST_CASE("resolveAndSeparate combined", "[physics][collision-response]")
{
	RigidBody3D a;
	a.mass = 1.0f;
	a.restitution = 0.5f;
	a.position = {0, 0, 0};
	a.velocity = {1.0f, 0, 0};

	RigidBody3D b;
	b.mass = 1.0f;
	b.restitution = 0.5f;
	b.position = {0.8f, 0, 0};
	b.velocity = {};

	ContactPoint contact;
	contact.normal = {1.0f, 0, 0};
	contact.penetrationDepth = 0.2f;

	auto impulse = resolveAndSeparate(a, b, contact);

	CHECK(impulse.x != 0.0f);
	// Bodies separated
	const float gap = b.position.x - a.position.x;
	CHECK(gap > 0.8f);
}

TEST_CASE("CollisionPair stores contacts", "[physics][collision-response]")
{
	CollisionPair pair;
	pair.bodyIdA = 0;
	pair.bodyIdB = 1;
	pair.contacts.push_back(ContactPoint{{0, 0, 0}, {1, 0, 0}, 0.1f});
	pair.contacts.push_back(ContactPoint{{0, 1, 0}, {0, 1, 0}, 0.05f});

	CHECK(pair.contacts.size() == 2);
	CHECK(pair.bodyIdA == 0);
	CHECK(pair.bodyIdB == 1);
}
