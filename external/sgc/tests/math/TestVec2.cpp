/// @file TestVec2.cpp
/// @brief Vec2.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/math/Vec2.hpp"

TEST_CASE("Vec2 default construction is zero", "[math][vec2]")
{
	constexpr sgc::Vec2f v;
	STATIC_REQUIRE(v.x == 0.0f);
	STATIC_REQUIRE(v.y == 0.0f);
}

TEST_CASE("Vec2 component construction", "[math][vec2]")
{
	constexpr sgc::Vec2f v{3.0f, 4.0f};
	STATIC_REQUIRE(v.x == 3.0f);
	STATIC_REQUIRE(v.y == 4.0f);
}

TEST_CASE("Vec2 arithmetic operators", "[math][vec2]")
{
	constexpr sgc::Vec2f a{1.0f, 2.0f};
	constexpr sgc::Vec2f b{3.0f, 4.0f};

	constexpr auto sum = a + b;
	STATIC_REQUIRE(sum.x == 4.0f);
	STATIC_REQUIRE(sum.y == 6.0f);

	constexpr auto diff = b - a;
	STATIC_REQUIRE(diff.x == 2.0f);
	STATIC_REQUIRE(diff.y == 2.0f);

	constexpr auto scaled = a * 3.0f;
	STATIC_REQUIRE(scaled.x == 3.0f);
	STATIC_REQUIRE(scaled.y == 6.0f);

	constexpr auto neg = -a;
	STATIC_REQUIRE(neg.x == -1.0f);
	STATIC_REQUIRE(neg.y == -2.0f);
}

TEST_CASE("Vec2 dot product", "[math][vec2]")
{
	constexpr sgc::Vec2f a{1.0f, 0.0f};
	constexpr sgc::Vec2f b{0.0f, 1.0f};
	STATIC_REQUIRE(a.dot(b) == 0.0f);

	constexpr sgc::Vec2f c{2.0f, 3.0f};
	constexpr sgc::Vec2f d{4.0f, 5.0f};
	STATIC_REQUIRE(c.dot(d) == 23.0f);
}

TEST_CASE("Vec2 cross product", "[math][vec2]")
{
	constexpr sgc::Vec2f a{1.0f, 0.0f};
	constexpr sgc::Vec2f b{0.0f, 1.0f};
	STATIC_REQUIRE(a.cross(b) == 1.0f);
}

TEST_CASE("Vec2 length", "[math][vec2]")
{
	sgc::Vec2f v{3.0f, 4.0f};
	REQUIRE(v.length() == Catch::Approx(5.0f));
	REQUIRE(v.lengthSquared() == 25.0f);
}

TEST_CASE("Vec2 normalized", "[math][vec2]")
{
	sgc::Vec2f v{3.0f, 4.0f};
	auto n = v.normalized();
	REQUIRE(n.length() == Catch::Approx(1.0f));
}

TEST_CASE("Vec2 zero vector normalized returns zero", "[math][vec2]")
{
	sgc::Vec2f v{0.0f, 0.0f};
	auto n = v.normalized();
	REQUIRE(n.x == 0.0f);
	REQUIRE(n.y == 0.0f);
}

TEST_CASE("Vec2 distance", "[math][vec2]")
{
	sgc::Vec2f a{0.0f, 0.0f};
	sgc::Vec2f b{3.0f, 4.0f};
	REQUIRE(a.distanceTo(b) == Catch::Approx(5.0f));
}

TEST_CASE("Vec2 perpendicular", "[math][vec2]")
{
	constexpr sgc::Vec2f v{1.0f, 0.0f};
	constexpr auto perp = v.perpendicular();
	STATIC_REQUIRE(perp.x == 0.0f);
	STATIC_REQUIRE(perp.y == 1.0f);
}

TEST_CASE("Vec2 static constants", "[math][vec2]")
{
	STATIC_REQUIRE(sgc::Vec2f::zero() == sgc::Vec2f{0.0f, 0.0f});
	STATIC_REQUIRE(sgc::Vec2f::one() == sgc::Vec2f{1.0f, 1.0f});
	STATIC_REQUIRE(sgc::Vec2f::right() == sgc::Vec2f{1.0f, 0.0f});
}

TEST_CASE("Vec2i integer operations", "[math][vec2]")
{
	constexpr sgc::Vec2i a{10, 20};
	constexpr sgc::Vec2i b{3, 7};
	constexpr auto sum = a + b;
	STATIC_REQUIRE(sum.x == 13);
	STATIC_REQUIRE(sum.y == 27);
}

TEST_CASE("Scalar * Vec2 commutative", "[math][vec2]")
{
	constexpr sgc::Vec2f v{2.0f, 3.0f};
	constexpr auto a = v * 5.0f;
	constexpr auto b = 5.0f * v;
	STATIC_REQUIRE(a == b);
}

// ── rotate ──────────────────────────────────────────────

TEST_CASE("Vec2 rotate by 0 returns same vector", "[math][vec2]")
{
	sgc::Vec2f v{1.0f, 0.0f};
	auto r = v.rotate(0.0f);
	REQUIRE(r.x == Catch::Approx(1.0f));
	REQUIRE(r.y == Catch::Approx(0.0f));
}

TEST_CASE("Vec2 rotate by pi/2", "[math][vec2]")
{
	sgc::Vec2f v{1.0f, 0.0f};
	const float halfPi = 3.14159265f / 2.0f;
	auto r = v.rotate(halfPi);
	REQUIRE(r.x == Catch::Approx(0.0f).margin(1e-5f));
	REQUIRE(r.y == Catch::Approx(1.0f).margin(1e-5f));
}

TEST_CASE("Vec2 rotate by pi", "[math][vec2]")
{
	sgc::Vec2f v{1.0f, 0.0f};
	const float pi = 3.14159265f;
	auto r = v.rotate(pi);
	REQUIRE(r.x == Catch::Approx(-1.0f).margin(1e-5f));
	REQUIRE(r.y == Catch::Approx(0.0f).margin(1e-5f));
}

// ── reflect ─────────────────────────────────────────────

TEST_CASE("Vec2 reflect off vertical wall", "[math][vec2]")
{
	sgc::Vec2f incoming{1.0f, -1.0f};
	sgc::Vec2f normal{-1.0f, 0.0f};
	auto reflected = incoming.reflect(normal);
	REQUIRE(reflected.x == Catch::Approx(-1.0f));
	REQUIRE(reflected.y == Catch::Approx(-1.0f));
}

// ── distanceSquaredTo ───────────────────────────────────

TEST_CASE("Vec2 distanceSquaredTo", "[math][vec2]")
{
	constexpr sgc::Vec2f a{0.0f, 0.0f};
	constexpr sgc::Vec2f b{3.0f, 4.0f};
	STATIC_REQUIRE(a.distanceSquaredTo(b) == 25.0f);
}

// ── compound assignment ─────────────────────────────────

TEST_CASE("Vec2 compound assignment operators", "[math][vec2]")
{
	sgc::Vec2f v{10.0f, 20.0f};

	v += sgc::Vec2f{1.0f, 2.0f};
	REQUIRE(v.x == Catch::Approx(11.0f));
	REQUIRE(v.y == Catch::Approx(22.0f));

	v -= sgc::Vec2f{1.0f, 2.0f};
	REQUIRE(v.x == Catch::Approx(10.0f));
	REQUIRE(v.y == Catch::Approx(20.0f));

	v *= 3.0f;
	REQUIRE(v.x == Catch::Approx(30.0f));
	REQUIRE(v.y == Catch::Approx(60.0f));

	v /= 6.0f;
	REQUIRE(v.x == Catch::Approx(5.0f));
	REQUIRE(v.y == Catch::Approx(10.0f));
}

// ── clamped ─────────────────────────────────────────────

TEST_CASE("Vec2 clamped restricts components", "[math][vec2]")
{
	constexpr sgc::Vec2f v{-5.0f, 15.0f};
	constexpr auto c = v.clamped(sgc::Vec2f{0.0f, 0.0f}, sgc::Vec2f{10.0f, 10.0f});
	STATIC_REQUIRE(c.x == 0.0f);
	STATIC_REQUIRE(c.y == 10.0f);
}

// ── lerp ────────────────────────────────────────────────

TEST_CASE("Vec2 lerp halfway", "[math][vec2]")
{
	constexpr sgc::Vec2f a{0.0f, 0.0f};
	constexpr sgc::Vec2f b{10.0f, 20.0f};
	constexpr auto mid = a.lerp(b, 0.5f);
	STATIC_REQUIRE(mid.x == 5.0f);
	STATIC_REQUIRE(mid.y == 10.0f);
}

// ── angleTo ─────────────────────────────────────────────

TEST_CASE("Vec2 angleTo returns correct angle", "[math][vec2]")
{
	sgc::Vec2f origin{0.0f, 0.0f};
	sgc::Vec2f target{1.0f, 0.0f};
	REQUIRE(origin.angleTo(target) == Catch::Approx(0.0f));

	sgc::Vec2f up{0.0f, 1.0f};
	const float halfPi = 3.14159265f / 2.0f;
	REQUIRE(origin.angleTo(up) == Catch::Approx(halfPi).margin(1e-5f));
}

// ── projected ───────────────────────────────────────────

TEST_CASE("Vec2 projected onto axis", "[math][vec2]")
{
	sgc::Vec2f v{3.0f, 4.0f};
	sgc::Vec2f axis{1.0f, 0.0f};
	auto proj = v.projected(axis);
	REQUIRE(proj.x == Catch::Approx(3.0f));
	REQUIRE(proj.y == Catch::Approx(0.0f));
}

// ── min/max ─────────────────────────────────────────────

TEST_CASE("Vec2 static min and max", "[math][vec2]")
{
	constexpr sgc::Vec2f a{1.0f, 5.0f};
	constexpr sgc::Vec2f b{3.0f, 2.0f};
	constexpr auto mn = sgc::Vec2f::min(a, b);
	constexpr auto mx = sgc::Vec2f::max(a, b);
	STATIC_REQUIRE(mn.x == 1.0f);
	STATIC_REQUIRE(mn.y == 2.0f);
	STATIC_REQUIRE(mx.x == 3.0f);
	STATIC_REQUIRE(mx.y == 5.0f);
}
