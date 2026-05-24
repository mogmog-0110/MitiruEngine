/// @file TestVec4.cpp
/// @brief Vec4 のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "sgc/math/Vec4.hpp"

using Catch::Approx;

// ── 構築 ──────────────────────────────────────────

TEST_CASE("Vec4 default construction is zero", "[math][vec4]")
{
	constexpr sgc::Vec4f v;
	STATIC_REQUIRE(v.x == 0.0f);
	STATIC_REQUIRE(v.y == 0.0f);
	STATIC_REQUIRE(v.z == 0.0f);
	STATIC_REQUIRE(v.w == 0.0f);
}

TEST_CASE("Vec4 component construction", "[math][vec4]")
{
	constexpr sgc::Vec4f v{ 1.0f, 2.0f, 3.0f, 4.0f };
	STATIC_REQUIRE(v.x == 1.0f);
	STATIC_REQUIRE(v.y == 2.0f);
	STATIC_REQUIRE(v.z == 3.0f);
	STATIC_REQUIRE(v.w == 4.0f);
}

TEST_CASE("Vec4 splat construction", "[math][vec4]")
{
	constexpr sgc::Vec4f v{ 5.0f };
	STATIC_REQUIRE(v.x == 5.0f);
	STATIC_REQUIRE(v.y == 5.0f);
	STATIC_REQUIRE(v.z == 5.0f);
	STATIC_REQUIRE(v.w == 5.0f);
}

TEST_CASE("Vec4 converting construction", "[math][vec4]")
{
	constexpr sgc::Vec4f vf{ 1.9f, 2.1f, 3.7f, 4.5f };
	constexpr sgc::Vec4i vi{ vf };
	STATIC_REQUIRE(vi.x == 1);
	STATIC_REQUIRE(vi.y == 2);
	STATIC_REQUIRE(vi.z == 3);
	STATIC_REQUIRE(vi.w == 4);
}

// ── 四則演算 ──────────────────────────────────────

TEST_CASE("Vec4 addition", "[math][vec4]")
{
	constexpr sgc::Vec4f a{ 1.0f, 2.0f, 3.0f, 4.0f };
	constexpr sgc::Vec4f b{ 5.0f, 6.0f, 7.0f, 8.0f };
	constexpr auto c = a + b;
	STATIC_REQUIRE(c.x == 6.0f);
	STATIC_REQUIRE(c.y == 8.0f);
	STATIC_REQUIRE(c.z == 10.0f);
	STATIC_REQUIRE(c.w == 12.0f);
}

TEST_CASE("Vec4 subtraction", "[math][vec4]")
{
	constexpr sgc::Vec4f a{ 5.0f, 6.0f, 7.0f, 8.0f };
	constexpr sgc::Vec4f b{ 1.0f, 2.0f, 3.0f, 4.0f };
	constexpr auto c = a - b;
	STATIC_REQUIRE(c.x == 4.0f);
	STATIC_REQUIRE(c.y == 4.0f);
	STATIC_REQUIRE(c.z == 4.0f);
	STATIC_REQUIRE(c.w == 4.0f);
}

TEST_CASE("Vec4 component-wise multiplication", "[math][vec4]")
{
	constexpr sgc::Vec4f a{ 2.0f, 3.0f, 4.0f, 5.0f };
	constexpr sgc::Vec4f b{ 3.0f, 4.0f, 5.0f, 6.0f };
	constexpr auto c = a * b;
	STATIC_REQUIRE(c.x == 6.0f);
	STATIC_REQUIRE(c.y == 12.0f);
	STATIC_REQUIRE(c.z == 20.0f);
	STATIC_REQUIRE(c.w == 30.0f);
}

TEST_CASE("Vec4 component-wise division", "[math][vec4]")
{
	constexpr sgc::Vec4f a{ 6.0f, 12.0f, 20.0f, 30.0f };
	constexpr sgc::Vec4f b{ 2.0f, 3.0f, 4.0f, 5.0f };
	constexpr auto c = a / b;
	STATIC_REQUIRE(c.x == 3.0f);
	STATIC_REQUIRE(c.y == 4.0f);
	STATIC_REQUIRE(c.z == 5.0f);
	STATIC_REQUIRE(c.w == 6.0f);
}

TEST_CASE("Vec4 scalar multiplication", "[math][vec4]")
{
	constexpr sgc::Vec4f v{ 1.0f, 2.0f, 3.0f, 4.0f };
	constexpr auto r = v * 3.0f;
	STATIC_REQUIRE(r.x == 3.0f);
	STATIC_REQUIRE(r.y == 6.0f);
	STATIC_REQUIRE(r.z == 9.0f);
	STATIC_REQUIRE(r.w == 12.0f);
}

TEST_CASE("Vec4 scalar division", "[math][vec4]")
{
	constexpr sgc::Vec4f v{ 4.0f, 8.0f, 12.0f, 16.0f };
	constexpr auto r = v / 4.0f;
	STATIC_REQUIRE(r.x == 1.0f);
	STATIC_REQUIRE(r.y == 2.0f);
	STATIC_REQUIRE(r.z == 3.0f);
	STATIC_REQUIRE(r.w == 4.0f);
}

TEST_CASE("Vec4 unary negate", "[math][vec4]")
{
	constexpr sgc::Vec4f v{ 1.0f, -2.0f, 3.0f, -4.0f };
	constexpr auto n = -v;
	STATIC_REQUIRE(n.x == -1.0f);
	STATIC_REQUIRE(n.y == 2.0f);
	STATIC_REQUIRE(n.z == -3.0f);
	STATIC_REQUIRE(n.w == 4.0f);
}

TEST_CASE("Scalar * Vec4 commutative", "[math][vec4]")
{
	constexpr sgc::Vec4f v{ 1.0f, 2.0f, 3.0f, 4.0f };
	constexpr auto r = 5.0f * v;
	STATIC_REQUIRE(r.x == 5.0f);
	STATIC_REQUIRE(r.y == 10.0f);
	STATIC_REQUIRE(r.z == 15.0f);
	STATIC_REQUIRE(r.w == 20.0f);
}

// ── 複合代入 ──────────────────────────────────────

TEST_CASE("Vec4 compound assignment operators", "[math][vec4]")
{
	sgc::Vec4f v{ 10.0f, 20.0f, 30.0f, 40.0f };

	v += sgc::Vec4f{ 1.0f, 2.0f, 3.0f, 4.0f };
	REQUIRE(v.x == Approx(11.0f));
	REQUIRE(v.y == Approx(22.0f));
	REQUIRE(v.z == Approx(33.0f));
	REQUIRE(v.w == Approx(44.0f));

	v -= sgc::Vec4f{ 1.0f, 2.0f, 3.0f, 4.0f };
	REQUIRE(v.x == Approx(10.0f));
	REQUIRE(v.y == Approx(20.0f));
	REQUIRE(v.z == Approx(30.0f));
	REQUIRE(v.w == Approx(40.0f));

	v *= 2.0f;
	REQUIRE(v.x == Approx(20.0f));
	REQUIRE(v.y == Approx(40.0f));
	REQUIRE(v.z == Approx(60.0f));
	REQUIRE(v.w == Approx(80.0f));

	v /= 4.0f;
	REQUIRE(v.x == Approx(5.0f));
	REQUIRE(v.y == Approx(10.0f));
	REQUIRE(v.z == Approx(15.0f));
	REQUIRE(v.w == Approx(20.0f));
}

// ── dot / length / normalized ─────────────────────

TEST_CASE("Vec4 dot product", "[math][vec4]")
{
	constexpr sgc::Vec4f a{ 1.0f, 2.0f, 3.0f, 4.0f };
	constexpr sgc::Vec4f b{ 5.0f, 6.0f, 7.0f, 8.0f };
	constexpr auto d = a.dot(b);
	STATIC_REQUIRE(d == 70.0f); // 5+12+21+32
}

TEST_CASE("Vec4 dot product of perpendicular vectors", "[math][vec4]")
{
	constexpr sgc::Vec4f a{ 1.0f, 0.0f, 0.0f, 0.0f };
	constexpr sgc::Vec4f b{ 0.0f, 1.0f, 0.0f, 0.0f };
	constexpr auto d = a.dot(b);
	STATIC_REQUIRE(d == 0.0f);
}

TEST_CASE("Vec4 lengthSquared", "[math][vec4]")
{
	constexpr sgc::Vec4f v{ 1.0f, 2.0f, 3.0f, 4.0f };
	constexpr auto lsq = v.lengthSquared();
	STATIC_REQUIRE(lsq == 30.0f); // 1+4+9+16
}

TEST_CASE("Vec4 length", "[math][vec4]")
{
	sgc::Vec4f v{ 0.0f, 0.0f, 3.0f, 4.0f };
	REQUIRE(v.length() == Approx(5.0f));
}

TEST_CASE("Vec4 normalized", "[math][vec4]")
{
	sgc::Vec4f v{ 0.0f, 0.0f, 0.0f, 5.0f };
	auto n = v.normalized();
	REQUIRE(n.x == Approx(0.0f));
	REQUIRE(n.y == Approx(0.0f));
	REQUIRE(n.z == Approx(0.0f));
	REQUIRE(n.w == Approx(1.0f));
	REQUIRE(n.length() == Approx(1.0f));
}

TEST_CASE("Vec4 zero vector normalized returns zero", "[math][vec4]")
{
	sgc::Vec4f v{};
	auto n = v.normalized();
	REQUIRE(n.x == Approx(0.0f));
	REQUIRE(n.y == Approx(0.0f));
	REQUIRE(n.z == Approx(0.0f));
	REQUIRE(n.w == Approx(0.0f));
}

// ── 静的定数 ──────────────────────────────────────

TEST_CASE("Vec4 static constants", "[math][vec4]")
{
	constexpr auto z = sgc::Vec4f::zero();
	STATIC_REQUIRE(z.x == 0.0f);
	STATIC_REQUIRE(z.y == 0.0f);
	STATIC_REQUIRE(z.z == 0.0f);
	STATIC_REQUIRE(z.w == 0.0f);

	constexpr auto o = sgc::Vec4f::one();
	STATIC_REQUIRE(o.x == 1.0f);
	STATIC_REQUIRE(o.y == 1.0f);
	STATIC_REQUIRE(o.z == 1.0f);
	STATIC_REQUIRE(o.w == 1.0f);
}

// ── 整数バリアント ────────────────────────────────

TEST_CASE("Vec4i integer operations", "[math][vec4]")
{
	constexpr sgc::Vec4i a{ 3, 6, 9, 12 };
	constexpr sgc::Vec4i b{ 1, 2, 3, 4 };
	constexpr auto c = a + b;
	STATIC_REQUIRE(c.x == 4);
	STATIC_REQUIRE(c.y == 8);
	STATIC_REQUIRE(c.z == 12);
	STATIC_REQUIRE(c.w == 16);

	constexpr auto lsq = a.lengthSquared();
	STATIC_REQUIRE(lsq == 270); // 9+36+81+144
}

// ── 等値比較 ──────────────────────────────────────

TEST_CASE("Vec4 equality comparison", "[math][vec4]")
{
	constexpr sgc::Vec4f a{ 1.0f, 2.0f, 3.0f, 4.0f };
	constexpr sgc::Vec4f b{ 1.0f, 2.0f, 3.0f, 4.0f };
	constexpr sgc::Vec4f c{ 1.0f, 2.0f, 3.0f, 5.0f };
	STATIC_REQUIRE(a == b);
	STATIC_REQUIRE_FALSE(a == c);
}
