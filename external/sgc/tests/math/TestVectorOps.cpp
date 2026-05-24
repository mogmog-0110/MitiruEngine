/// @file TestVectorOps.cpp
/// @brief VectorOps.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/math/VectorOps.hpp"

using Catch::Approx;

// ── Vec2 テスト ────────────────────────────────────────────────

TEST_CASE("VectorOps Vec2 dot product", "[math][vectorops]")
{
	constexpr sgc::Vec2f a{1.0f, 0.0f};
	constexpr sgc::Vec2f b{0.0f, 1.0f};
	STATIC_REQUIRE(sgc::math::dot(a, b) == 0.0f);

	constexpr sgc::Vec2f c{3.0f, 4.0f};
	constexpr sgc::Vec2f d{2.0f, 1.0f};
	STATIC_REQUIRE(sgc::math::dot(c, d) == 10.0f);
}

TEST_CASE("VectorOps Vec2 cross product", "[math][vectorops]")
{
	constexpr sgc::Vec2f a{1.0f, 0.0f};
	constexpr sgc::Vec2f b{0.0f, 1.0f};
	STATIC_REQUIRE(sgc::math::cross(a, b) == 1.0f);

	// 平行なベクトル
	constexpr sgc::Vec2f c{2.0f, 0.0f};
	STATIC_REQUIRE(sgc::math::cross(a, c) == 0.0f);
}

TEST_CASE("VectorOps Vec2 distance", "[math][vectorops]")
{
	const sgc::Vec2f a{0.0f, 0.0f};
	const sgc::Vec2f b{3.0f, 4.0f};
	REQUIRE(sgc::math::distance(a, b) == Approx(5.0f));

	const auto dsq = sgc::math::distanceSquared(a, b);
	REQUIRE(dsq == Approx(25.0f));
}

TEST_CASE("VectorOps Vec2 normalize", "[math][vectorops]")
{
	const sgc::Vec2f v{3.0f, 4.0f};
	const auto n = sgc::math::normalize(v);
	REQUIRE(n.x == Approx(0.6f));
	REQUIRE(n.y == Approx(0.8f));

	// ゼロベクトルの正規化
	const auto z = sgc::math::normalize(sgc::Vec2f{0.0f, 0.0f});
	REQUIRE(z.x == 0.0f);
	REQUIRE(z.y == 0.0f);
}

TEST_CASE("VectorOps Vec2 lerp", "[math][vectorops]")
{
	const sgc::Vec2f a{0.0f, 0.0f};
	const sgc::Vec2f b{10.0f, 20.0f};

	const auto mid = sgc::math::lerp(a, b, 0.5f);
	REQUIRE(mid.x == Approx(5.0f));
	REQUIRE(mid.y == Approx(10.0f));

	const auto start = sgc::math::lerp(a, b, 0.0f);
	REQUIRE(start.x == Approx(0.0f));
	REQUIRE(start.y == Approx(0.0f));
}

TEST_CASE("VectorOps Vec2 reflect", "[math][vectorops]")
{
	const sgc::Vec2f v{1.0f, -1.0f};
	const sgc::Vec2f normal{0.0f, 1.0f};
	const auto r = sgc::math::reflect(v, normal);
	REQUIRE(r.x == Approx(1.0f));
	REQUIRE(r.y == Approx(1.0f));
}

TEST_CASE("VectorOps Vec2 perpendicular", "[math][vectorops]")
{
	constexpr sgc::Vec2f v{1.0f, 0.0f};
	constexpr auto p = sgc::math::perpendicular(v);
	STATIC_REQUIRE(p.x == 0.0f);
	STATIC_REQUIRE(p.y == 1.0f);

	// 垂直ベクトルは元ベクトルと直交する
	STATIC_REQUIRE(sgc::math::dot(v, p) == 0.0f);
}

// ── Vec3 テスト ────────────────────────────────────────────────

TEST_CASE("VectorOps Vec3 dot product", "[math][vectorops]")
{
	constexpr sgc::Vec3f a{1.0f, 2.0f, 3.0f};
	constexpr sgc::Vec3f b{4.0f, 5.0f, 6.0f};
	STATIC_REQUIRE(sgc::math::dot(a, b) == 32.0f);
}

TEST_CASE("VectorOps Vec3 cross product", "[math][vectorops]")
{
	constexpr sgc::Vec3f x{1.0f, 0.0f, 0.0f};
	constexpr sgc::Vec3f y{0.0f, 1.0f, 0.0f};
	constexpr auto z = sgc::math::cross(x, y);
	STATIC_REQUIRE(z.x == 0.0f);
	STATIC_REQUIRE(z.y == 0.0f);
	STATIC_REQUIRE(z.z == 1.0f);
}

TEST_CASE("VectorOps Vec3 distance", "[math][vectorops]")
{
	const sgc::Vec3f a{0.0f, 0.0f, 0.0f};
	const sgc::Vec3f b{1.0f, 2.0f, 2.0f};
	REQUIRE(sgc::math::distance(a, b) == Approx(3.0f));
}

TEST_CASE("VectorOps Vec3 normalize", "[math][vectorops]")
{
	const sgc::Vec3f v{0.0f, 0.0f, 5.0f};
	const auto n = sgc::math::normalize(v);
	REQUIRE(n.x == Approx(0.0f));
	REQUIRE(n.y == Approx(0.0f));
	REQUIRE(n.z == Approx(1.0f));
}

TEST_CASE("VectorOps Vec3 lerp", "[math][vectorops]")
{
	const sgc::Vec3f a{0.0f, 0.0f, 0.0f};
	const sgc::Vec3f b{10.0f, 20.0f, 30.0f};
	const auto mid = sgc::math::lerp(a, b, 0.5f);
	REQUIRE(mid.x == Approx(5.0f));
	REQUIRE(mid.y == Approx(10.0f));
	REQUIRE(mid.z == Approx(15.0f));
}

TEST_CASE("VectorOps Vec3 reflect", "[math][vectorops]")
{
	const sgc::Vec3f v{1.0f, -1.0f, 0.0f};
	const sgc::Vec3f normal{0.0f, 1.0f, 0.0f};
	const auto r = sgc::math::reflect(v, normal);
	REQUIRE(r.x == Approx(1.0f));
	REQUIRE(r.y == Approx(1.0f));
	REQUIRE(r.z == Approx(0.0f));
}

TEST_CASE("VectorOps Vec3 slerp", "[math][vectorops]")
{
	const sgc::Vec3f a{1.0f, 0.0f, 0.0f};
	const sgc::Vec3f b{0.0f, 1.0f, 0.0f};

	// t=0 は始点に近い
	const auto s0 = sgc::math::slerp(a, b, 0.0f);
	REQUIRE(s0.x == Approx(1.0f).margin(1e-5f));
	REQUIRE(s0.y == Approx(0.0f).margin(1e-5f));

	// t=1 は終点に近い
	const auto s1 = sgc::math::slerp(a, b, 1.0f);
	REQUIRE(s1.x == Approx(0.0f).margin(1e-5f));
	REQUIRE(s1.y == Approx(1.0f).margin(1e-5f));

	// t=0.5 は中間（45度回転）
	const auto sMid = sgc::math::slerp(a, b, 0.5f);
	const float expected = 0.70710678f;  // cos(45) = sin(45)
	REQUIRE(sMid.x == Approx(expected).margin(1e-4f));
	REQUIRE(sMid.y == Approx(expected).margin(1e-4f));
}
