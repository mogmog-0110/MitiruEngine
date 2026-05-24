#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/physics/ForceField.hpp"

using namespace sgc;
using namespace sgc::physics;

static constexpr auto approx = [](float expected) {
	return Catch::Matchers::WithinAbs(expected, 0.1f);
};

// ── GravityField ────────────────────────────────────────

TEST_CASE("GravityField - applies force inside bounds", "[physics][forcefield]")
{
	GravityField field{AABB2f{{0, 0}, {100, 100}}, {0, -15.0f}};
	Vec2f force;

	REQUIRE(field.computeForce({50, 50}, 2.0f, force));
	REQUIRE_THAT(force.x, approx(0.0f));
	REQUIRE_THAT(force.y, approx(-30.0f));  // -15 * 2
}

TEST_CASE("GravityField - no force outside bounds", "[physics][forcefield]")
{
	GravityField field{AABB2f{{0, 0}, {100, 100}}, {0, -15.0f}};
	Vec2f force;

	REQUIRE_FALSE(field.computeForce({150, 50}, 2.0f, force));
}

TEST_CASE("GravityField - inactive field applies nothing", "[physics][forcefield]")
{
	GravityField field{AABB2f{{0, 0}, {100, 100}}, {0, -15.0f}};
	field.active = false;
	Vec2f force;

	REQUIRE_FALSE(field.computeForce({50, 50}, 2.0f, force));
}

TEST_CASE("GravityField - upward gravity for flip effect", "[physics][forcefield]")
{
	GravityField field{AABB2f{{0, 0}, {200, 200}}, {0, 9.8f}};
	Vec2f force;

	REQUIRE(field.computeForce({100, 100}, 1.0f, force));
	REQUIRE_THAT(force.y, approx(9.8f));
}

// ── DragField ───────────────────────────────────────────

TEST_CASE("DragField - applies velocity damping", "[physics][forcefield]")
{
	DragField field{AABB2f{{0, 0}, {200, 200}}, 0.5f};
	Vec2f vel{10.0f, -8.0f};

	auto result = field.applyDamping({100, 100}, vel);
	REQUIRE_THAT(result.x, approx(5.0f));
	REQUIRE_THAT(result.y, approx(-4.0f));
}

TEST_CASE("DragField - no damping outside bounds", "[physics][forcefield]")
{
	DragField field{AABB2f{{0, 0}, {100, 100}}, 0.5f};
	Vec2f vel{10.0f, 0.0f};

	auto result = field.applyDamping({200, 50}, vel);
	REQUIRE_THAT(result.x, approx(10.0f));
}

TEST_CASE("DragField - buoyancy force", "[physics][forcefield]")
{
	DragField field{AABB2f{{0, 0}, {200, 200}}, 0.9f, {0.0f, -3.0f}};
	Vec2f force;

	REQUIRE(field.computeForce({100, 100}, 2.0f, force));
	REQUIRE_THAT(force.y, approx(-6.0f));  // -3 * 2
}

// ── WindField ───────────────────────────────────────────

TEST_CASE("WindField - applies constant force", "[physics][forcefield]")
{
	WindField field{AABB2f{{0, 0}, {200, 200}}, {8.0f, 0.0f}};
	Vec2f force;

	REQUIRE(field.computeForce({100, 100}, force));
	REQUIRE_THAT(force.x, approx(8.0f));
	REQUIRE_THAT(force.y, approx(0.0f));
}

TEST_CASE("WindField - mass independent", "[physics][forcefield]")
{
	WindField field{AABB2f{{0, 0}, {200, 200}}, {5.0f, 3.0f}};
	Vec2f force;

	REQUIRE(field.computeForce({100, 100}, force));
	REQUIRE_THAT(force.x, approx(5.0f));
	REQUIRE_THAT(force.y, approx(3.0f));
}

// ── VortexField ─────────────────────────────────────────

TEST_CASE("VortexField - pulls toward center", "[physics][forcefield]")
{
	VortexField field{AABB2f{{0, 0}, {200, 200}}, 10.0f, 0.0f};
	Vec2f force;

	// Position at right edge, center at (100,100)
	REQUIRE(field.computeForce({200, 100}, force));
	REQUIRE(force.x < 0.0f);  // Pulls left toward center
	REQUIRE_THAT(force.y, approx(0.0f));
}

TEST_CASE("VortexField - spins around center", "[physics][forcefield]")
{
	VortexField field{AABB2f{{0, 0}, {200, 200}}, 0.0f, 10.0f};
	Vec2f force;

	// Position at right of center
	REQUIRE(field.computeForce({200, 100}, force));
	// Tangent of (-1,0) direction is (0, -1) rotated 90deg = (0, -1)... wait
	// toCenter = (100,100) - (200,100) = (-100, 0), normalized = (-1, 0)
	// tangent = (0, -1) * spin... actually tangent = (-dir.y, dir.x) = (0, -1)
	REQUIRE_THAT(force.x, approx(0.0f));
	REQUIRE(force.y < 0.0f);
}

TEST_CASE("VortexField - at center returns zero force", "[physics][forcefield]")
{
	VortexField field{AABB2f{{0, 0}, {200, 200}}, 5.0f, 5.0f};
	Vec2f force;

	REQUIRE(field.computeForce({100, 100}, force));
	REQUIRE_THAT(force.x, approx(0.0f));
	REQUIRE_THAT(force.y, approx(0.0f));
}

// ── ForceFieldBase ──────────────────────────────────────

TEST_CASE("ForceFieldBase - containsXZ checks XZ plane", "[physics][forcefield]")
{
	ForceFieldBase field;
	field.bounds = AABB2f{{0, 0}, {100, 100}};
	field.active = true;

	REQUIRE(field.containsXZ(Vec3f{50, 999, 50}));  // Y ignored
	REQUIRE_FALSE(field.containsXZ(Vec3f{150, 0, 50}));
}
