#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <sgc/procedural/LSystem.hpp>

#include <cmath>
#include <set>

using namespace sgc::procedural;

TEST_CASE("LSystem - zero iterations returns axiom", "[procedural]")
{
	LSystemConfig config;
	config.axiom = "F+F";
	config.rules = {{'F', "FF"}};
	config.iterations = 0;

	const auto result = generateLSystem(config);
	REQUIRE(result == "F+F");
}

TEST_CASE("LSystem - single rule replacement", "[procedural]")
{
	LSystemConfig config;
	config.axiom = "A";
	config.rules = {{'A', "AB"}};
	config.iterations = 1;

	const auto result = generateLSystem(config);
	REQUIRE(result == "AB");
}

TEST_CASE("LSystem - multiple iterations grow string", "[procedural]")
{
	// Algae: A -> AB, B -> A
	LSystemConfig config;
	config.axiom = "A";
	config.rules = {{'A', "AB"}, {'B', "A"}};

	config.iterations = 1;
	REQUIRE(generateLSystem(config) == "AB");

	config.iterations = 2;
	REQUIRE(generateLSystem(config) == "ABA");

	config.iterations = 3;
	REQUIRE(generateLSystem(config) == "ABAAB");

	config.iterations = 4;
	REQUIRE(generateLSystem(config) == "ABAABABA");
}

TEST_CASE("LSystem - stochastic rule with seed", "[procedural]")
{
	LSystemConfig config;
	config.axiom = "FFFFF";
	config.rules = {
		{'F', "A", 0.5f},
		{'F', "B", 0.5f}
	};
	config.iterations = 1;
	config.seed = 42;

	const auto result1 = generateLSystem(config);
	REQUIRE(result1.size() == 5);

	// Same seed produces same result
	const auto result2 = generateLSystem(config);
	REQUIRE(result1 == result2);

	// Different seed may produce different result
	config.seed = 999;
	const auto result3 = generateLSystem(config);
	REQUIRE(result3.size() == 5);

	// All characters should be A or B
	for (const char ch : result1)
	{
		REQUIRE((ch == 'A' || ch == 'B'));
	}
}

TEST_CASE("LSystem - turtle produces segments for simple F string", "[procedural]")
{
	const std::string lStr = "FFF";
	TurtleConfig config;
	config.stepLength = 10.0f;
	config.startPosition = sgc::Vec2f{0.0f, 0.0f};
	config.startAngle = 0.0f; // right

	const auto result = interpretTurtle(lStr, config);
	REQUIRE(result.segments.size() == 3);

	// First segment: (0,0) -> (10,0)
	REQUIRE(result.segments[0].first.x == Catch::Approx(0.0f));
	REQUIRE(result.segments[0].first.y == Catch::Approx(0.0f));
	REQUIRE(result.segments[0].second.x == Catch::Approx(10.0f));
	REQUIRE(result.segments[0].second.y == Catch::Approx(0.0f));

	// Third segment ends at (30, 0)
	REQUIRE(result.segments[2].second.x == Catch::Approx(30.0f));
	REQUIRE(result.segments[2].second.y == Catch::Approx(0.0f));
}

TEST_CASE("LSystem - turtle branching with push/pop", "[procedural]")
{
	// F[+F]F : draw forward, branch left, draw, return, draw forward
	const std::string lStr = "F[+F]F";
	TurtleConfig config;
	config.stepLength = 10.0f;
	config.angleIncrement = 90.0f;
	config.startAngle = 0.0f; // right

	const auto result = interpretTurtle(lStr, config);
	REQUIRE(result.segments.size() == 3);

	// First F: (0,0) -> (10,0)
	REQUIRE(result.segments[0].second.x == Catch::Approx(10.0f));
	REQUIRE(result.segments[0].second.y == Catch::Approx(0.0f));

	// After [+F]: branch from (10,0), turn left 90 degrees (angle becomes -90),
	// draw upward to (10,-10)
	REQUIRE(result.segments[1].first.x == Catch::Approx(10.0f));
	REQUIRE(result.segments[1].first.y == Catch::Approx(0.0f));
	REQUIRE(result.segments[1].second.x == Catch::Approx(10.0f));
	REQUIRE(result.segments[1].second.y == Catch::Approx(-10.0f).margin(0.001f));

	// After ]: restored to (10,0) angle 0, then F: (10,0) -> (20,0)
	REQUIRE(result.segments[2].first.x == Catch::Approx(10.0f));
	REQUIRE(result.segments[2].first.y == Catch::Approx(0.0f));
	REQUIRE(result.segments[2].second.x == Catch::Approx(20.0f));
	REQUIRE(result.segments[2].second.y == Catch::Approx(0.0f));
}

TEST_CASE("LSystem - turtle rotation produces angled segments", "[procedural]")
{
	// F-F : forward, turn right 90, forward
	const std::string lStr = "F-F";
	TurtleConfig config;
	config.stepLength = 10.0f;
	config.angleIncrement = 90.0f;
	config.startAngle = 0.0f; // right

	const auto result = interpretTurtle(lStr, config);
	REQUIRE(result.segments.size() == 2);

	// First F: (0,0) -> (10,0) going right
	REQUIRE(result.segments[0].second.x == Catch::Approx(10.0f));
	REQUIRE(result.segments[0].second.y == Catch::Approx(0.0f));

	// After - (right turn, angle becomes 90): F goes downward
	REQUIRE(result.segments[1].second.x == Catch::Approx(10.0f).margin(0.001f));
	REQUIRE(result.segments[1].second.y == Catch::Approx(10.0f));
}

TEST_CASE("LSystem - Koch curve produces expected segment count", "[procedural]")
{
	// Koch curve: F -> F+F--F+F
	LSystemConfig config;
	config.axiom = "F";
	config.rules = {{'F', "F+F--F+F"}};

	config.iterations = 1;
	auto lStr = generateLSystem(config);
	auto result = interpretTurtle(lStr, TurtleConfig{});
	REQUIRE(result.segments.size() == 4); // 4 F's in "F+F--F+F"

	config.iterations = 2;
	lStr = generateLSystem(config);
	result = interpretTurtle(lStr, TurtleConfig{});
	REQUIRE(result.segments.size() == 16); // 4^2 = 16

	config.iterations = 3;
	lStr = generateLSystem(config);
	result = interpretTurtle(lStr, TurtleConfig{});
	REQUIRE(result.segments.size() == 64); // 4^3 = 64
}

TEST_CASE("LSystem - empty axiom returns empty string", "[procedural]")
{
	LSystemConfig config;
	config.axiom = "";
	config.rules = {{'F', "FF"}};
	config.iterations = 5;

	const auto result = generateLSystem(config);
	REQUIRE(result.empty());
}
