/// @file TestSpline.cpp
/// @brief Spline.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/math/Spline.hpp"
#include "sgc/math/Vec2.hpp"

using Vec = sgc::Vec2<float>;

TEST_CASE("catmullRom returns p1 at t=0", "[math][spline]")
{
	const Vec p0{-10.0f, 0.0f};
	const Vec p1{0.0f, 0.0f};
	const Vec p2{10.0f, 10.0f};
	const Vec p3{20.0f, 10.0f};

	const auto result = sgc::catmullRom(p0, p1, p2, p3, 0.0f);
	REQUIRE(result.x == Catch::Approx(0.0f));
	REQUIRE(result.y == Catch::Approx(0.0f));
}

TEST_CASE("catmullRom returns p2 at t=1", "[math][spline]")
{
	const Vec p0{-10.0f, 0.0f};
	const Vec p1{0.0f, 0.0f};
	const Vec p2{10.0f, 10.0f};
	const Vec p3{20.0f, 10.0f};

	const auto result = sgc::catmullRom(p0, p1, p2, p3, 1.0f);
	REQUIRE(result.x == Catch::Approx(10.0f));
	REQUIRE(result.y == Catch::Approx(10.0f));
}

TEST_CASE("catmullRom midpoint on collinear points", "[math][spline]")
{
	// Collinear points: spline should pass through midpoint
	const Vec p0{0.0f, 0.0f};
	const Vec p1{10.0f, 0.0f};
	const Vec p2{20.0f, 0.0f};
	const Vec p3{30.0f, 0.0f};

	const auto result = sgc::catmullRom(p0, p1, p2, p3, 0.5f);
	REQUIRE(result.x == Catch::Approx(15.0f));
	REQUIRE(result.y == Catch::Approx(0.0f));
}

TEST_CASE("SplinePath evaluate at t=0 and t=1", "[math][spline]")
{
	sgc::SplinePath<Vec> path;
	path.addPoint({0.0f, 0.0f});
	path.addPoint({10.0f, 0.0f});
	path.addPoint({20.0f, 0.0f});
	path.addPoint({30.0f, 0.0f});

	REQUIRE(path.pointCount() == 4);

	const auto start = path.evaluate(0.0f);
	REQUIRE(start.x == Catch::Approx(10.0f));
	REQUIRE(start.y == Catch::Approx(0.0f));

	const auto end = path.evaluate(1.0f);
	REQUIRE(end.x == Catch::Approx(20.0f));
	REQUIRE(end.y == Catch::Approx(0.0f));
}

TEST_CASE("SplinePath multi-segment", "[math][spline]")
{
	sgc::SplinePath<Vec> path;
	path.addPoint({0.0f, 0.0f});
	path.addPoint({10.0f, 0.0f});
	path.addPoint({20.0f, 0.0f});
	path.addPoint({30.0f, 0.0f});
	path.addPoint({40.0f, 0.0f});

	// 2 segments: [0]-[1]-[2]-[3] and [1]-[2]-[3]-[4]
	const auto mid = path.evaluate(0.5f);
	REQUIRE(mid.x == Catch::Approx(20.0f));
	REQUIRE(mid.y == Catch::Approx(0.0f));
}

TEST_CASE("SplinePath throws with fewer than 4 points", "[math][spline]")
{
	sgc::SplinePath<Vec> path;
	path.addPoint({0.0f, 0.0f});
	path.addPoint({10.0f, 0.0f});
	path.addPoint({20.0f, 0.0f});

	REQUIRE_THROWS_AS(path.evaluate(0.5f), std::runtime_error);
}

TEST_CASE("SplinePath clear resets state", "[math][spline]")
{
	sgc::SplinePath<Vec> path;
	path.addPoint({0.0f, 0.0f});
	path.addPoint({10.0f, 0.0f});
	path.addPoint({20.0f, 0.0f});
	path.addPoint({30.0f, 0.0f});

	path.clear();
	REQUIRE(path.pointCount() == 0);
}

TEST_CASE("SplinePath approximateLength on straight line", "[math][spline]")
{
	sgc::SplinePath<Vec> path;
	path.addPoint({0.0f, 0.0f});
	path.addPoint({10.0f, 0.0f});
	path.addPoint({20.0f, 0.0f});
	path.addPoint({30.0f, 0.0f});

	// Path goes from p1=(10,0) to p2=(20,0), should be ~10
	const float len = path.approximateLength(200);
	REQUIRE(len == Catch::Approx(10.0f).margin(0.5f));
}

TEST_CASE("SplinePath approximateLength returns 0 with too few points", "[math][spline]")
{
	sgc::SplinePath<Vec> path;
	path.addPoint({0.0f, 0.0f});

	REQUIRE(path.approximateLength() == 0.0f);
}
