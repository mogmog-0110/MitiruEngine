#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/physics/Constraints.hpp"

using namespace sgc;
using namespace sgc::physics;
using Catch::Matchers::WithinAbs;

// ── DistanceConstraint ──

TEST_CASE("DistanceConstraint maintains distance", "[physics][constraints]")
{
	RigidBody3D a;
	a.mass = 1.0f;
	a.position = {0, 0, 0};

	RigidBody3D b;
	b.mass = 1.0f;
	b.position = {5, 0, 0};

	DistanceConstraint dc{&a, &b, 2.0f};

	// Solve multiple iterations
	for (int i = 0; i < 20; ++i)
	{
		dc.solve(1.0f / 60.0f);
	}

	const float dist = (b.position - a.position).length();
	CHECK_THAT(dist, WithinAbs(2.0, 0.01));
}

TEST_CASE("DistanceConstraint with static body", "[physics][constraints]")
{
	RigidBody3D a;
	a.isStatic = true;
	a.position = {0, 0, 0};

	RigidBody3D b;
	b.mass = 1.0f;
	b.position = {5, 0, 0};

	DistanceConstraint dc{&a, &b, 3.0f};

	for (int i = 0; i < 20; ++i)
	{
		dc.solve(1.0f / 60.0f);
	}

	// A should not move
	CHECK(a.position == Vec3f::zero());

	// B should be at distance 3 from A
	const float dist = b.position.length();
	CHECK_THAT(dist, WithinAbs(3.0, 0.01));
}

TEST_CASE("DistanceConstraint both static does nothing", "[physics][constraints]")
{
	RigidBody3D a;
	a.isStatic = true;
	a.position = {0, 0, 0};

	RigidBody3D b;
	b.isStatic = true;
	b.position = {5, 0, 0};

	DistanceConstraint dc{&a, &b, 2.0f};
	dc.solve(1.0f / 60.0f);

	CHECK(a.position == Vec3f::zero());
	CHECK_THAT(b.position.x, WithinAbs(5.0, 1e-6));
}

TEST_CASE("DistanceConstraint with compliance", "[physics][constraints]")
{
	RigidBody3D a;
	a.mass = 1.0f;
	a.position = {0, 0, 0};

	RigidBody3D b;
	b.mass = 1.0f;
	b.position = {5, 0, 0};

	// High compliance = softer constraint
	DistanceConstraint soft{&a, &b, 2.0f, 100.0f};
	soft.solve(1.0f / 60.0f);

	// With high compliance, correction is smaller per step
	const float dist = (b.position - a.position).length();
	CHECK(dist > 2.0f); // Not fully corrected yet
	CHECK(dist < 5.0f); // But some correction happened
}

TEST_CASE("DistanceConstraint setTargetDistance", "[physics][constraints]")
{
	RigidBody3D a;
	a.mass = 1.0f;
	a.position = {};

	RigidBody3D b;
	b.mass = 1.0f;
	b.position = {5, 0, 0};

	DistanceConstraint dc{&a, &b, 2.0f};
	CHECK_THAT(dc.targetDistance(), WithinAbs(2.0, 1e-6));

	dc.setTargetDistance(4.0f);
	CHECK_THAT(dc.targetDistance(), WithinAbs(4.0, 1e-6));
}

TEST_CASE("DistanceConstraint null body safety", "[physics][constraints]")
{
	DistanceConstraint dc{nullptr, nullptr, 2.0f};
	dc.solve(1.0f / 60.0f); // Should not crash
}

// ── HingeConstraint ──

TEST_CASE("HingeConstraint constrains angular velocity", "[physics][constraints]")
{
	RigidBody3D a;
	a.mass = 1.0f;
	a.position = {0, 0, 0};
	a.angularVelocity = {1.0f, 2.0f, 3.0f};

	RigidBody3D b;
	b.mass = 1.0f;
	b.position = {2, 0, 0};
	b.angularVelocity = {0.5f, 1.0f, 0.5f};

	// Y-axis hinge
	HingeConstraint hinge{&a, &b, {1, 0, 0}, {0, 1, 0}};
	hinge.solve(1.0f / 60.0f);

	// Angular velocity should only have Y component
	CHECK_THAT(a.angularVelocity.x, WithinAbs(0.0, 1e-6));
	CHECK_THAT(a.angularVelocity.z, WithinAbs(0.0, 1e-6));
	CHECK_THAT(a.angularVelocity.y, WithinAbs(2.0, 1e-6)); // Preserved

	CHECK_THAT(b.angularVelocity.x, WithinAbs(0.0, 1e-6));
	CHECK_THAT(b.angularVelocity.z, WithinAbs(0.0, 1e-6));
	CHECK_THAT(b.angularVelocity.y, WithinAbs(1.0, 1e-6));
}

TEST_CASE("HingeConstraint axis getter", "[physics][constraints]")
{
	RigidBody3D a, b;
	a.mass = 1.0f;
	b.mass = 1.0f;

	HingeConstraint hinge{&a, &b, {0, 0, 0}, {0, 0, 1}};
	CHECK_THAT(hinge.axis().z, WithinAbs(1.0, 1e-6));
}

TEST_CASE("HingeConstraint stiffness", "[physics][constraints]")
{
	RigidBody3D a, b;
	a.mass = 1.0f;
	b.mass = 1.0f;

	HingeConstraint hinge{&a, &b, {0, 0, 0}, {0, 1, 0}};
	CHECK_THAT(hinge.stiffness(), WithinAbs(0.5, 1e-6));

	hinge.setStiffness(0.8f);
	CHECK_THAT(hinge.stiffness(), WithinAbs(0.8, 1e-6));

	// Clamp test
	hinge.setStiffness(2.0f);
	CHECK_THAT(hinge.stiffness(), WithinAbs(1.0, 1e-6));
}

TEST_CASE("HingeConstraint null body safety", "[physics][constraints]")
{
	HingeConstraint hinge{nullptr, nullptr, {0, 0, 0}, {0, 1, 0}};
	hinge.solve(1.0f / 60.0f); // Should not crash
}

// ── SpringConstraint ──

TEST_CASE("SpringConstraint pulls bodies together", "[physics][constraints]")
{
	RigidBody3D a;
	a.mass = 1.0f;
	a.position = {0, 0, 0};

	RigidBody3D b;
	b.mass = 1.0f;
	b.position = {5, 0, 0};

	// Rest length 2, currently at distance 5 -> should pull together
	SpringConstraint spring{&a, &b, 2.0f, 50.0f, 0.0f};
	spring.solve(1.0f / 60.0f);

	// A should gain positive X velocity (toward B)
	CHECK(a.velocity.x > 0.0f);
	// B should gain negative X velocity (toward A)
	CHECK(b.velocity.x < 0.0f);
}

TEST_CASE("SpringConstraint pushes bodies apart", "[physics][constraints]")
{
	RigidBody3D a;
	a.mass = 1.0f;
	a.position = {0, 0, 0};

	RigidBody3D b;
	b.mass = 1.0f;
	b.position = {1, 0, 0};

	// Rest length 3, currently at distance 1 -> should push apart
	SpringConstraint spring{&a, &b, 3.0f, 50.0f, 0.0f};
	spring.solve(1.0f / 60.0f);

	CHECK(a.velocity.x < 0.0f);
	CHECK(b.velocity.x > 0.0f);
}

TEST_CASE("SpringConstraint damping reduces velocity", "[physics][constraints]")
{
	RigidBody3D a;
	a.mass = 1.0f;
	a.position = {0, 0, 0};
	a.velocity = {-1.0f, 0, 0}; // Moving away from B

	RigidBody3D b;
	b.mass = 1.0f;
	b.position = {2, 0, 0};
	b.velocity = {1.0f, 0, 0}; // Moving away from A

	// At rest length, but with relative velocity -> damping should slow them
	SpringConstraint spring{&a, &b, 2.0f, 0.0f, 10.0f}; // No stiffness, only damping
	spring.solve(1.0f / 60.0f);

	// Damping force opposes relative velocity
	CHECK(a.velocity.x > -1.0f); // Less negative
	CHECK(b.velocity.x < 1.0f);  // Less positive
}

TEST_CASE("SpringConstraint getters and setters", "[physics][constraints]")
{
	RigidBody3D a, b;
	a.mass = 1.0f;
	b.mass = 1.0f;

	SpringConstraint spring{&a, &b, 2.0f, 10.0f, 0.5f};

	CHECK_THAT(spring.restLength(), WithinAbs(2.0, 1e-6));
	CHECK_THAT(spring.stiffness(), WithinAbs(10.0, 1e-6));
	CHECK_THAT(spring.damping(), WithinAbs(0.5, 1e-6));

	spring.setRestLength(3.0f);
	spring.setStiffness(20.0f);
	spring.setDamping(1.0f);

	CHECK_THAT(spring.restLength(), WithinAbs(3.0, 1e-6));
	CHECK_THAT(spring.stiffness(), WithinAbs(20.0, 1e-6));
	CHECK_THAT(spring.damping(), WithinAbs(1.0, 1e-6));
}

TEST_CASE("SpringConstraint null body safety", "[physics][constraints]")
{
	SpringConstraint spring{nullptr, nullptr, 2.0f};
	spring.solve(1.0f / 60.0f); // Should not crash
}

// ── ConstraintSolver ──

TEST_CASE("ConstraintSolver add and remove", "[physics][constraints]")
{
	ConstraintSolver solver;
	CHECK(solver.constraintCount() == 0);

	RigidBody3D a, b;
	a.mass = 1.0f;
	b.mass = 1.0f;

	DistanceConstraint dc{&a, &b, 2.0f};
	solver.addConstraint(&dc);
	CHECK(solver.constraintCount() == 1);

	solver.removeConstraint(&dc);
	CHECK(solver.constraintCount() == 0);
}

TEST_CASE("ConstraintSolver add null ignored", "[physics][constraints]")
{
	ConstraintSolver solver;
	solver.addConstraint(nullptr);
	CHECK(solver.constraintCount() == 0);
}

TEST_CASE("ConstraintSolver solve iterates", "[physics][constraints]")
{
	RigidBody3D a;
	a.mass = 1.0f;
	a.position = {0, 0, 0};

	RigidBody3D b;
	b.mass = 1.0f;
	b.position = {10, 0, 0};

	DistanceConstraint dc{&a, &b, 2.0f};

	ConstraintSolver solver;
	solver.addConstraint(&dc);
	solver.setIterations(10);
	CHECK(solver.iterations() == 10);

	solver.solve(1.0f / 60.0f);

	const float dist = (b.position - a.position).length();
	CHECK_THAT(dist, WithinAbs(2.0, 0.1));
}

TEST_CASE("ConstraintSolver clear", "[physics][constraints]")
{
	ConstraintSolver solver;

	RigidBody3D a, b;
	a.mass = 1.0f;
	b.mass = 1.0f;

	DistanceConstraint dc{&a, &b, 2.0f};
	SpringConstraint sc{&a, &b, 3.0f};

	solver.addConstraint(&dc);
	solver.addConstraint(&sc);
	CHECK(solver.constraintCount() == 2);

	solver.clear();
	CHECK(solver.constraintCount() == 0);
}

TEST_CASE("ConstraintSolver multiple constraints", "[physics][constraints]")
{
	RigidBody3D a, b, c;
	a.mass = 1.0f; a.position = {0, 0, 0};
	b.mass = 1.0f; b.position = {5, 0, 0};
	c.mass = 1.0f; c.position = {10, 0, 0};

	DistanceConstraint ab{&a, &b, 2.0f};
	DistanceConstraint bc{&b, &c, 2.0f};

	ConstraintSolver solver;
	solver.addConstraint(&ab);
	solver.addConstraint(&bc);
	solver.setIterations(20);

	solver.solve(1.0f / 60.0f);

	const float distAB = (b.position - a.position).length();
	const float distBC = (c.position - b.position).length();

	CHECK_THAT(distAB, WithinAbs(2.0, 0.1));
	CHECK_THAT(distBC, WithinAbs(2.0, 0.1));
}

TEST_CASE("ConstraintSolver minimum iterations clamped to 1", "[physics][constraints]")
{
	ConstraintSolver solver;
	solver.setIterations(0);
	CHECK(solver.iterations() == 1);

	solver.setIterations(-5);
	CHECK(solver.iterations() == 1);
}
