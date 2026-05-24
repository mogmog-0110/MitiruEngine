/// @file TestVec3.cpp
/// @brief Vec3.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/math/Vec3.hpp"

TEST_CASE("Vec3 default construction is zero", "[math][vec3]")
{
	constexpr sgc::Vec3f v;
	STATIC_REQUIRE(v.x == 0.0f);
	STATIC_REQUIRE(v.y == 0.0f);
	STATIC_REQUIRE(v.z == 0.0f);
}

TEST_CASE("Vec3 arithmetic", "[math][vec3]")
{
	constexpr sgc::Vec3f a{1.0f, 2.0f, 3.0f};
	constexpr sgc::Vec3f b{4.0f, 5.0f, 6.0f};
	constexpr auto sum = a + b;
	STATIC_REQUIRE(sum.x == 5.0f);
	STATIC_REQUIRE(sum.y == 7.0f);
	STATIC_REQUIRE(sum.z == 9.0f);
}

TEST_CASE("Vec3 cross product", "[math][vec3]")
{
	constexpr sgc::Vec3f x = sgc::Vec3f::unitX();
	constexpr sgc::Vec3f y = sgc::Vec3f::unitY();
	constexpr auto z = x.cross(y);
	STATIC_REQUIRE(z.x == 0.0f);
	STATIC_REQUIRE(z.y == 0.0f);
	STATIC_REQUIRE(z.z == 1.0f);
}

TEST_CASE("Vec3 dot product", "[math][vec3]")
{
	constexpr sgc::Vec3f a{1.0f, 2.0f, 3.0f};
	constexpr sgc::Vec3f b{4.0f, 5.0f, 6.0f};
	STATIC_REQUIRE(a.dot(b) == 32.0f);
}

TEST_CASE("Vec3 length", "[math][vec3]")
{
	sgc::Vec3f v{1.0f, 2.0f, 2.0f};
	REQUIRE(v.length() == Catch::Approx(3.0f));
}

TEST_CASE("Vec3 normalized", "[math][vec3]")
{
	sgc::Vec3f v{0.0f, 3.0f, 4.0f};
	auto n = v.normalized();
	REQUIRE(n.length() == Catch::Approx(1.0f));
}

TEST_CASE("Vec3 zero vector normalized returns zero", "[math][vec3]")
{
	sgc::Vec3f v{};
	auto n = v.normalized();
	REQUIRE(n.x == 0.0f);
	REQUIRE(n.y == 0.0f);
	REQUIRE(n.z == 0.0f);
}

TEST_CASE("Vec3 distanceSquaredTo", "[math][vec3]")
{
	constexpr sgc::Vec3f a{0.0f, 0.0f, 0.0f};
	constexpr sgc::Vec3f b{1.0f, 2.0f, 2.0f};
	STATIC_REQUIRE(a.distanceSquaredTo(b) == 9.0f);
}

TEST_CASE("Vec3 direction constants", "[math][vec3]")
{
	STATIC_REQUIRE(sgc::Vec3f::up() == sgc::Vec3f{0.0f, 1.0f, 0.0f});
	STATIC_REQUIRE(sgc::Vec3f::down() == sgc::Vec3f{0.0f, -1.0f, 0.0f});
	STATIC_REQUIRE(sgc::Vec3f::left() == sgc::Vec3f{-1.0f, 0.0f, 0.0f});
	STATIC_REQUIRE(sgc::Vec3f::right() == sgc::Vec3f{1.0f, 0.0f, 0.0f});
	STATIC_REQUIRE(sgc::Vec3f::forward() == sgc::Vec3f{0.0f, 0.0f, -1.0f});
	STATIC_REQUIRE(sgc::Vec3f::back() == sgc::Vec3f{0.0f, 0.0f, 1.0f});
}

TEST_CASE("Vec3 compound assignment operators", "[math][vec3]")
{
	sgc::Vec3f v{10.0f, 20.0f, 30.0f};

	v += sgc::Vec3f{1.0f, 2.0f, 3.0f};
	REQUIRE(v.x == Catch::Approx(11.0f));
	REQUIRE(v.y == Catch::Approx(22.0f));
	REQUIRE(v.z == Catch::Approx(33.0f));

	v -= sgc::Vec3f{1.0f, 2.0f, 3.0f};
	REQUIRE(v.x == Catch::Approx(10.0f));
	REQUIRE(v.y == Catch::Approx(20.0f));
	REQUIRE(v.z == Catch::Approx(30.0f));

	v *= 2.0f;
	REQUIRE(v.x == Catch::Approx(20.0f));
	REQUIRE(v.y == Catch::Approx(40.0f));
	REQUIRE(v.z == Catch::Approx(60.0f));

	v /= 4.0f;
	REQUIRE(v.x == Catch::Approx(5.0f));
	REQUIRE(v.y == Catch::Approx(10.0f));
	REQUIRE(v.z == Catch::Approx(15.0f));
}

TEST_CASE("Vec3 reflect off surface", "[math][vec3]")
{
	sgc::Vec3f incoming{1.0f, -1.0f, 0.0f};
	sgc::Vec3f normal{0.0f, 1.0f, 0.0f};
	auto reflected = incoming.reflect(normal);
	REQUIRE(reflected.x == Catch::Approx(1.0f));
	REQUIRE(reflected.y == Catch::Approx(1.0f));
	REQUIRE(reflected.z == Catch::Approx(0.0f));
}

TEST_CASE("Vec3 static constants zero and one", "[math][vec3]")
{
	STATIC_REQUIRE(sgc::Vec3f::zero() == sgc::Vec3f{0.0f, 0.0f, 0.0f});
	STATIC_REQUIRE(sgc::Vec3f::one() == sgc::Vec3f{1.0f, 1.0f, 1.0f});
	STATIC_REQUIRE(sgc::Vec3f::unitX() == sgc::Vec3f{1.0f, 0.0f, 0.0f});
	STATIC_REQUIRE(sgc::Vec3f::unitY() == sgc::Vec3f{0.0f, 1.0f, 0.0f});
	STATIC_REQUIRE(sgc::Vec3f::unitZ() == sgc::Vec3f{0.0f, 0.0f, 1.0f});
}

// ── clamped ─────────────────────────────────────────────

TEST_CASE("Vec3 clamped restricts components", "[math][vec3]")
{
	constexpr sgc::Vec3f v{-2.0f, 5.0f, 15.0f};
	constexpr auto c = v.clamped(sgc::Vec3f{0.0f, 0.0f, 0.0f}, sgc::Vec3f{10.0f, 10.0f, 10.0f});
	STATIC_REQUIRE(c.x == 0.0f);
	STATIC_REQUIRE(c.y == 5.0f);
	STATIC_REQUIRE(c.z == 10.0f);
}

// ── lerp ────────────────────────────────────────────────

TEST_CASE("Vec3 lerp halfway", "[math][vec3]")
{
	constexpr sgc::Vec3f a{0.0f, 0.0f, 0.0f};
	constexpr sgc::Vec3f b{10.0f, 20.0f, 30.0f};
	constexpr auto mid = a.lerp(b, 0.5f);
	STATIC_REQUIRE(mid.x == 5.0f);
	STATIC_REQUIRE(mid.y == 10.0f);
	STATIC_REQUIRE(mid.z == 15.0f);
}

// ── angleTo ─────────────────────────────────────────────

TEST_CASE("Vec3 angleTo parallel is zero", "[math][vec3]")
{
	sgc::Vec3f a{1.0f, 0.0f, 0.0f};
	sgc::Vec3f b{2.0f, 0.0f, 0.0f};
	REQUIRE(a.angleTo(b) == Catch::Approx(0.0f).margin(1e-5f));
}

TEST_CASE("Vec3 angleTo perpendicular is pi/2", "[math][vec3]")
{
	sgc::Vec3f a{1.0f, 0.0f, 0.0f};
	sgc::Vec3f b{0.0f, 1.0f, 0.0f};
	const float halfPi = 3.14159265f / 2.0f;
	REQUIRE(a.angleTo(b) == Catch::Approx(halfPi).margin(1e-5f));
}

// ── projectOnto ─────────────────────────────────────────

TEST_CASE("Vec3 projectOnto axis", "[math][vec3]")
{
	sgc::Vec3f v{3.0f, 4.0f, 5.0f};
	sgc::Vec3f axis{1.0f, 0.0f, 0.0f};
	auto proj = v.projectOnto(axis);
	REQUIRE(proj.x == Catch::Approx(3.0f));
	REQUIRE(proj.y == Catch::Approx(0.0f));
	REQUIRE(proj.z == Catch::Approx(0.0f));
}

// ── rotateAroundAxis ────────────────────────────────────

TEST_CASE("Vec3 rotateAroundAxis 90 degrees around Z", "[math][vec3]")
{
	sgc::Vec3f v{1.0f, 0.0f, 0.0f};
	sgc::Vec3f axis{0.0f, 0.0f, 1.0f};
	const float halfPi = 3.14159265f / 2.0f;
	auto rotated = v.rotateAroundAxis(axis, halfPi);
	REQUIRE(rotated.x == Catch::Approx(0.0f).margin(1e-5f));
	REQUIRE(rotated.y == Catch::Approx(1.0f).margin(1e-5f));
	REQUIRE(rotated.z == Catch::Approx(0.0f).margin(1e-5f));
}

// ── min/max ─────────────────────────────────────────────

TEST_CASE("Vec3 static min and max", "[math][vec3]")
{
	constexpr sgc::Vec3f a{1.0f, 5.0f, 3.0f};
	constexpr sgc::Vec3f b{3.0f, 2.0f, 7.0f};
	constexpr auto mn = sgc::Vec3f::min(a, b);
	constexpr auto mx = sgc::Vec3f::max(a, b);
	STATIC_REQUIRE(mn.x == 1.0f);
	STATIC_REQUIRE(mn.y == 2.0f);
	STATIC_REQUIRE(mn.z == 3.0f);
	STATIC_REQUIRE(mx.x == 3.0f);
	STATIC_REQUIRE(mx.y == 5.0f);
	STATIC_REQUIRE(mx.z == 7.0f);
}

// ── angleTo zero vector ─────────────────────────────────

TEST_CASE("Vec3 angleTo zero vector returns 0", "[math][vec3]")
{
	sgc::Vec3f a{1.0f, 0.0f, 0.0f};
	sgc::Vec3f zero{};
	REQUIRE(a.angleTo(zero) == Catch::Approx(0.0f));
}
