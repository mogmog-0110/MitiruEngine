/// @file TestBezier.cpp
/// @brief Bezier.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/math/Bezier.hpp"
#include "sgc/math/Vec2.hpp"

using Vec = sgc::Vec2<float>;

TEST_CASE("quadraticBezier returns start at t=0", "[math][bezier]")
{
	const Vec p0{0.0f, 0.0f};
	const Vec p1{50.0f, 100.0f};
	const Vec p2{100.0f, 0.0f};

	const auto result = sgc::quadraticBezier(p0, p1, p2, 0.0f);
	REQUIRE(result.x == Catch::Approx(0.0f));
	REQUIRE(result.y == Catch::Approx(0.0f));
}

TEST_CASE("quadraticBezier returns end at t=1", "[math][bezier]")
{
	const Vec p0{0.0f, 0.0f};
	const Vec p1{50.0f, 100.0f};
	const Vec p2{100.0f, 0.0f};

	const auto result = sgc::quadraticBezier(p0, p1, p2, 1.0f);
	REQUIRE(result.x == Catch::Approx(100.0f));
	REQUIRE(result.y == Catch::Approx(0.0f));
}

TEST_CASE("quadraticBezier midpoint", "[math][bezier]")
{
	const Vec p0{0.0f, 0.0f};
	const Vec p1{50.0f, 100.0f};
	const Vec p2{100.0f, 0.0f};

	const auto result = sgc::quadraticBezier(p0, p1, p2, 0.5f);
	// B(0.5) = 0.25*p0 + 0.5*p1 + 0.25*p2 = (50, 50)
	REQUIRE(result.x == Catch::Approx(50.0f));
	REQUIRE(result.y == Catch::Approx(50.0f));
}

TEST_CASE("cubicBezier returns start at t=0", "[math][bezier]")
{
	const Vec p0{0.0f, 0.0f};
	const Vec p1{30.0f, 60.0f};
	const Vec p2{70.0f, 60.0f};
	const Vec p3{100.0f, 0.0f};

	const auto result = sgc::cubicBezier(p0, p1, p2, p3, 0.0f);
	REQUIRE(result.x == Catch::Approx(0.0f));
	REQUIRE(result.y == Catch::Approx(0.0f));
}

TEST_CASE("cubicBezier returns end at t=1", "[math][bezier]")
{
	const Vec p0{0.0f, 0.0f};
	const Vec p1{30.0f, 60.0f};
	const Vec p2{70.0f, 60.0f};
	const Vec p3{100.0f, 0.0f};

	const auto result = sgc::cubicBezier(p0, p1, p2, p3, 1.0f);
	REQUIRE(result.x == Catch::Approx(100.0f));
	REQUIRE(result.y == Catch::Approx(0.0f));
}

TEST_CASE("cubicBezierTangent at endpoints", "[math][bezier]")
{
	const Vec p0{0.0f, 0.0f};
	const Vec p1{0.0f, 100.0f};
	const Vec p2{100.0f, 100.0f};
	const Vec p3{100.0f, 0.0f};

	// t=0: tangent = 3*(p1 - p0) = (0, 300)
	const auto t0 = sgc::cubicBezierTangent(p0, p1, p2, p3, 0.0f);
	REQUIRE(t0.x == Catch::Approx(0.0f));
	REQUIRE(t0.y == Catch::Approx(300.0f));

	// t=1: tangent = 3*(p3 - p2) = (0, -300)
	const auto t1 = sgc::cubicBezierTangent(p0, p1, p2, p3, 1.0f);
	REQUIRE(t1.x == Catch::Approx(0.0f));
	REQUIRE(t1.y == Catch::Approx(-300.0f));
}

TEST_CASE("BezierPath single segment evaluate", "[math][bezier]")
{
	sgc::BezierPath<Vec> path;
	path.setStart({0.0f, 0.0f});
	path.addCubic({30.0f, 60.0f}, {70.0f, 60.0f}, {100.0f, 0.0f});

	REQUIRE(path.segmentCount() == 1);

	const auto start = path.evaluate(0.0f);
	REQUIRE(start.x == Catch::Approx(0.0f));
	REQUIRE(start.y == Catch::Approx(0.0f));

	const auto end = path.evaluate(1.0f);
	REQUIRE(end.x == Catch::Approx(100.0f));
	REQUIRE(end.y == Catch::Approx(0.0f));
}

TEST_CASE("BezierPath multi-segment evaluate", "[math][bezier]")
{
	sgc::BezierPath<Vec> path;
	path.setStart({0.0f, 0.0f});
	path.addCubic({10.0f, 20.0f}, {40.0f, 20.0f}, {50.0f, 0.0f});
	path.addCubic({60.0f, -20.0f}, {90.0f, -20.0f}, {100.0f, 0.0f});

	REQUIRE(path.segmentCount() == 2);

	// t=0.5 should be at the boundary of segments (50, 0)
	const auto mid = path.evaluate(0.5f);
	REQUIRE(mid.x == Catch::Approx(50.0f));
	REQUIRE(mid.y == Catch::Approx(0.0f));
}

TEST_CASE("BezierPath approximateLength positive", "[math][bezier]")
{
	sgc::BezierPath<Vec> path;
	path.setStart({0.0f, 0.0f});
	path.addCubic({0.0f, 0.0f}, {100.0f, 0.0f}, {100.0f, 0.0f});

	// Straight line from (0,0) to (100,0) should be approximately 100
	const float len = path.approximateLength(200);
	REQUIRE(len == Catch::Approx(100.0f).margin(1.0f));
}

TEST_CASE("BezierPath throws on empty evaluate", "[math][bezier]")
{
	sgc::BezierPath<Vec> path;
	REQUIRE_THROWS_AS(path.evaluate(0.5f), std::runtime_error);
}
