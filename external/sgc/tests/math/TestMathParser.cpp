#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <numbers>

#include "sgc/math/MathParser.hpp"

using Catch::Approx;

TEST_CASE("MathParser simple addition", "[math][MathParser]")
{
	sgc::math::MathParser parser;
	auto result = parser.evaluate("2+3");
	REQUIRE(result.has_value());
	REQUIRE(*result == Approx(5.0f));
}

TEST_CASE("MathParser operator precedence", "[math][MathParser]")
{
	sgc::math::MathParser parser;
	auto result = parser.evaluate("2+3*4");
	REQUIRE(result.has_value());
	REQUIRE(*result == Approx(14.0f));
}

TEST_CASE("MathParser parentheses override precedence", "[math][MathParser]")
{
	sgc::math::MathParser parser;
	auto result = parser.evaluate("(2+3)*4");
	REQUIRE(result.has_value());
	REQUIRE(*result == Approx(20.0f));
}

TEST_CASE("MathParser unary minus", "[math][MathParser]")
{
	sgc::math::MathParser parser;

	auto r1 = parser.evaluate("-5");
	REQUIRE(r1.has_value());
	REQUIRE(*r1 == Approx(-5.0f));

	auto r2 = parser.evaluate("3*-2");
	REQUIRE(r2.has_value());
	REQUIRE(*r2 == Approx(-6.0f));
}

TEST_CASE("MathParser power operator", "[math][MathParser]")
{
	sgc::math::MathParser parser;
	auto result = parser.evaluate("2^3");
	REQUIRE(result.has_value());
	REQUIRE(*result == Approx(8.0f));
}

TEST_CASE("MathParser sin cos tan functions", "[math][MathParser]")
{
	sgc::math::MathParser parser;

	auto r1 = parser.evaluate("sin(0)");
	REQUIRE(r1.has_value());
	REQUIRE(*r1 == Approx(0.0f).margin(1e-6f));

	auto r2 = parser.evaluate("cos(0)");
	REQUIRE(r2.has_value());
	REQUIRE(*r2 == Approx(1.0f).margin(1e-6f));

	auto r3 = parser.evaluate("tan(0)");
	REQUIRE(r3.has_value());
	REQUIRE(*r3 == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("MathParser abs sqrt log functions", "[math][MathParser]")
{
	sgc::math::MathParser parser;

	auto r1 = parser.evaluate("abs(-7)");
	REQUIRE(r1.has_value());
	REQUIRE(*r1 == Approx(7.0f));

	auto r2 = parser.evaluate("sqrt(16)");
	REQUIRE(r2.has_value());
	REQUIRE(*r2 == Approx(4.0f));

	auto r3 = parser.evaluate("log(100)");
	REQUIRE(r3.has_value());
	REQUIRE(*r3 == Approx(2.0f).margin(0.01f));
}

TEST_CASE("MathParser variable substitution", "[math][MathParser]")
{
	sgc::math::MathParser parser;
	parser.setVariable("x", 3.0f);
	auto result = parser.evaluate("2*x+1");
	REQUIRE(result.has_value());
	REQUIRE(*result == Approx(7.0f));
}

TEST_CASE("MathParser multiple variables", "[math][MathParser]")
{
	sgc::math::MathParser parser;
	parser.setVariable("x", 2.0f);
	parser.setVariable("y", 3.0f);
	auto result = parser.evaluate("x+y");
	REQUIRE(result.has_value());
	REQUIRE(*result == Approx(5.0f));
}

TEST_CASE("MathParser constants pi and e", "[math][MathParser]")
{
	sgc::math::MathParser parser;

	auto r1 = parser.evaluate("pi");
	REQUIRE(r1.has_value());
	REQUIRE(*r1 == Approx(std::numbers::pi_v<float>));

	auto r2 = parser.evaluate("e");
	REQUIRE(r2.has_value());
	REQUIRE(*r2 == Approx(std::numbers::e_v<float>));
}

TEST_CASE("MathParser invalid expression returns nullopt", "[math][MathParser]")
{
	sgc::math::MathParser parser;

	auto r1 = parser.evaluate("2**3");
	REQUIRE_FALSE(r1.has_value());

	auto r2 = parser.evaluate("(2+3");
	REQUIRE_FALSE(r2.has_value());

	auto r3 = parser.evaluate("");
	REQUIRE_FALSE(r3.has_value());

	REQUIRE_FALSE(parser.getLastError().empty());
}

TEST_CASE("MathParser nested functions", "[math][MathParser]")
{
	sgc::math::MathParser parser;
	auto result = parser.evaluate("sin(cos(0))");
	REQUIRE(result.has_value());
	// cos(0) = 1, sin(1) ~ 0.8415
	REQUIRE(*result == Approx(std::sin(1.0f)).margin(1e-5f));
}

TEST_CASE("MathParser division by zero", "[math][MathParser]")
{
	sgc::math::MathParser parser;
	auto result = parser.evaluate("1/0");
	REQUIRE_FALSE(result.has_value());
	REQUIRE(parser.getLastError() == "Division by zero");
}

TEST_CASE("MathParser getVariable returns nullopt for unknown", "[math][MathParser]")
{
	sgc::math::MathParser parser;
	REQUIRE_FALSE(parser.getVariable("unknown").has_value());

	parser.setVariable("x", 42.0f);
	auto val = parser.getVariable("x");
	REQUIRE(val.has_value());
	REQUIRE(*val == Approx(42.0f));
}

TEST_CASE("MathParser isValid checks syntax", "[math][MathParser]")
{
	sgc::math::MathParser parser;
	REQUIRE(parser.isValid("2+3"));
	REQUIRE_FALSE(parser.isValid("2+"));
}

TEST_CASE("MathParser min and max functions", "[math][MathParser]")
{
	sgc::math::MathParser parser;

	auto r1 = parser.evaluate("min(3, 7)");
	REQUIRE(r1.has_value());
	REQUIRE(*r1 == Approx(3.0f));

	auto r2 = parser.evaluate("max(3, 7)");
	REQUIRE(r2.has_value());
	REQUIRE(*r2 == Approx(7.0f));
}

TEST_CASE("MathParser exp and ln functions", "[math][MathParser]")
{
	sgc::math::MathParser parser;

	auto r1 = parser.evaluate("exp(0)");
	REQUIRE(r1.has_value());
	REQUIRE(*r1 == Approx(1.0f));

	auto r2 = parser.evaluate("ln(1)");
	REQUIRE(r2.has_value());
	REQUIRE(*r2 == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("MathParser floor and ceil functions", "[math][MathParser]")
{
	sgc::math::MathParser parser;

	auto r1 = parser.evaluate("floor(3.7)");
	REQUIRE(r1.has_value());
	REQUIRE(*r1 == Approx(3.0f));

	auto r2 = parser.evaluate("ceil(3.2)");
	REQUIRE(r2.has_value());
	REQUIRE(*r2 == Approx(4.0f));
}
