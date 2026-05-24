#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/math/ExpressionTree.hpp"

#include <algorithm>
#include <cmath>
#include <set>

using namespace sgc;

static constexpr auto approx = [](float expected) {
	return Catch::Matchers::WithinAbs(expected, 0.001f);
};

// ── Leaf Nodes ─────────────────────────────────────────

TEST_CASE("ExpressionTree - number literal evaluation", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(b.number(42.0f));

	REQUIRE_THAT(tree.evaluate({}), approx(42.0f));
}

TEST_CASE("ExpressionTree - variable evaluation with binding", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(b.variable("x"));

	VariableBindings vars{{"x", 7.5f}};
	REQUIRE_THAT(tree.evaluate(vars), approx(7.5f));
}

TEST_CASE("ExpressionTree - undefined variable returns NaN", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(b.variable("z"));

	REQUIRE(std::isnan(tree.evaluate({})));
}

// ── Binary Operations ──────────────────────────────────

TEST_CASE("ExpressionTree - addition", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(b.add(b.number(3.0f), b.number(4.0f)));

	REQUIRE_THAT(tree.evaluate({}), approx(7.0f));
}

TEST_CASE("ExpressionTree - subtraction", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(b.sub(b.number(10.0f), b.number(3.0f)));

	REQUIRE_THAT(tree.evaluate({}), approx(7.0f));
}

TEST_CASE("ExpressionTree - multiplication", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(b.mul(b.number(6.0f), b.number(7.0f)));

	REQUIRE_THAT(tree.evaluate({}), approx(42.0f));
}

TEST_CASE("ExpressionTree - division", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(b.div(b.number(20.0f), b.number(4.0f)));

	REQUIRE_THAT(tree.evaluate({}), approx(5.0f));
}

TEST_CASE("ExpressionTree - division by zero returns NaN", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(b.div(b.number(1.0f), b.number(0.0f)));

	REQUIRE(std::isnan(tree.evaluate({})));
}

TEST_CASE("ExpressionTree - power", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(b.pow(b.number(2.0f), b.number(10.0f)));

	REQUIRE_THAT(tree.evaluate({}), approx(1024.0f));
}

// ── Unary Functions ────────────────────────────────────

TEST_CASE("ExpressionTree - sin function", "[math][expression]")
{
	ExpressionBuilder b;
	const float pi = 3.14159265f;
	auto tree = b.build(b.sin(b.number(pi / 2.0f)));

	REQUIRE_THAT(tree.evaluate({}), approx(1.0f));
}

TEST_CASE("ExpressionTree - cos function", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(b.cos(b.number(0.0f)));

	REQUIRE_THAT(tree.evaluate({}), approx(1.0f));
}

TEST_CASE("ExpressionTree - tan function", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(b.tan(b.number(0.0f)));

	REQUIRE_THAT(tree.evaluate({}), approx(0.0f));
}

TEST_CASE("ExpressionTree - sqrt function", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(b.sqrt(b.number(25.0f)));

	REQUIRE_THAT(tree.evaluate({}), approx(5.0f));
}

TEST_CASE("ExpressionTree - abs function", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(b.abs(b.number(-7.0f)));

	REQUIRE_THAT(tree.evaluate({}), approx(7.0f));
}

TEST_CASE("ExpressionTree - log function", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(b.log(b.number(std::exp(1.0f))));

	REQUIRE_THAT(tree.evaluate({}), approx(1.0f));
}

TEST_CASE("ExpressionTree - exp function", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(b.exp(b.number(0.0f)));

	REQUIRE_THAT(tree.evaluate({}), approx(1.0f));
}

TEST_CASE("ExpressionTree - floor function", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(b.floor(b.number(3.7f)));

	REQUIRE_THAT(tree.evaluate({}), approx(3.0f));
}

TEST_CASE("ExpressionTree - ceil function", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(b.ceil(b.number(3.2f)));

	REQUIRE_THAT(tree.evaluate({}), approx(4.0f));
}

TEST_CASE("ExpressionTree - negate function", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(b.negate(b.number(5.0f)));

	REQUIRE_THAT(tree.evaluate({}), approx(-5.0f));
}

// ── Complex Expressions ────────────────────────────────

TEST_CASE("ExpressionTree - sin(x) + cos(y) * 2", "[math][expression]")
{
	ExpressionBuilder b;
	// sin(x) + cos(y) * 2
	auto tree = b.build(
		b.add(
			b.sin(b.variable("x")),
			b.mul(b.cos(b.variable("y")), b.number(2.0f))
		)
	);

	const float pi = 3.14159265f;
	VariableBindings vars{{"x", pi / 2.0f}, {"y", 0.0f}};
	// sin(pi/2) + cos(0) * 2 = 1 + 1*2 = 3
	REQUIRE_THAT(tree.evaluate(vars), approx(3.0f));
}

TEST_CASE("ExpressionTree - nested: sqrt(x^2 + y^2)", "[math][expression]")
{
	ExpressionBuilder b;
	// sqrt(x^2 + y^2)
	auto tree = b.build(
		b.sqrt(
			b.add(
				b.pow(b.variable("x"), b.number(2.0f)),
				b.pow(b.variable("y"), b.number(2.0f))
			)
		)
	);

	VariableBindings vars{{"x", 3.0f}, {"y", 4.0f}};
	REQUIRE_THAT(tree.evaluate(vars), approx(5.0f));
}

// ── toString ───────────────────────────────────────────

TEST_CASE("ExpressionTree - toString number", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(b.number(42.0f));
	REQUIRE(tree.toString() == "42");
}

TEST_CASE("ExpressionTree - toString variable", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(b.variable("x"));
	REQUIRE(tree.toString() == "x");
}

TEST_CASE("ExpressionTree - toString binary with precedence", "[math][expression]")
{
	ExpressionBuilder b;
	// 2 + 3 * 4
	auto tree = b.build(
		b.add(b.number(2.0f), b.mul(b.number(3.0f), b.number(4.0f)))
	);

	// mul has higher precedence than add, so no parens around 3 * 4
	REQUIRE(tree.toString() == "2 + 3 * 4");
}

TEST_CASE("ExpressionTree - toString parenthesizes lower precedence", "[math][expression]")
{
	ExpressionBuilder b;
	// (2 + 3) * 4
	auto tree = b.build(
		b.mul(b.add(b.number(2.0f), b.number(3.0f)), b.number(4.0f))
	);

	// add has lower precedence than mul, needs parens
	REQUIRE(tree.toString() == "(2 + 3) * 4");
}

TEST_CASE("ExpressionTree - toString unary function", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(b.sin(b.variable("x")));
	REQUIRE(tree.toString() == "sin(x)");
}

TEST_CASE("ExpressionTree - toString negate", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(b.negate(b.variable("x")));
	REQUIRE(tree.toString() == "(-x)");
}

TEST_CASE("ExpressionTree - toString complex expression", "[math][expression]")
{
	ExpressionBuilder b;
	// sin(x) + 2 * y
	auto tree = b.build(
		b.add(b.sin(b.variable("x")), b.mul(b.number(2.0f), b.variable("y")))
	);
	REQUIRE(tree.toString() == "sin(x) + 2 * y");
}

// ── Node IDs ───────────────────────────────────────────

TEST_CASE("ExpressionTree - all node IDs are unique", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(
		b.add(
			b.sin(b.variable("x")),
			b.mul(b.number(2.0f), b.variable("y"))
		)
	);

	std::vector<NodeId> ids;
	detail::collectNodeIds(*tree.root(), ids);

	// 6 nodes: add, sin, x, mul, 2, y
	REQUIRE(ids.size() == 6);

	std::set<NodeId> unique(ids.begin(), ids.end());
	REQUIRE(unique.size() == ids.size());
}

// ── nodeCount ──────────────────────────────────────────

TEST_CASE("ExpressionTree - nodeCount", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(
		b.add(b.number(1.0f), b.mul(b.number(2.0f), b.variable("x")))
	);

	// add(1, mul(2, x)) = 5 nodes
	REQUIRE(tree.nodeCount() == 5);
}

// ── collectVariables ───────────────────────────────────

TEST_CASE("ExpressionTree - collectVariables", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(
		b.add(
			b.mul(b.variable("x"), b.variable("y")),
			b.sin(b.variable("x"))  // x appears twice
		)
	);

	auto vars = tree.collectVariables();
	REQUIRE(vars.size() == 2);
	REQUIRE(std::find(vars.begin(), vars.end(), "x") != vars.end());
	REQUIRE(std::find(vars.begin(), vars.end(), "y") != vars.end());
}

// ── Constant Folding ───────────────────────────────────

TEST_CASE("ExpressionTree - constant folding pure constants", "[math][expression]")
{
	ExpressionBuilder b;
	// (3 + 4) * 2 → should fold to 14
	auto tree = b.build(
		b.mul(b.add(b.number(3.0f), b.number(4.0f)), b.number(2.0f))
	);

	tree.foldConstants();

	// 全て定数なので1ノードに畳み込まれる
	REQUIRE(tree.nodeCount() == 1);
	REQUIRE_THAT(tree.evaluate({}), approx(14.0f));
}

TEST_CASE("ExpressionTree - constant folding with variables", "[math][expression]")
{
	ExpressionBuilder b;
	// (3 + 4) * x → 7 * x
	auto tree = b.build(
		b.mul(b.add(b.number(3.0f), b.number(4.0f)), b.variable("x"))
	);

	tree.foldConstants();

	// 3ノード: mul, 7(folded), x
	REQUIRE(tree.nodeCount() == 3);
	REQUIRE(tree.toString() == "7 * x");

	VariableBindings vars{{"x", 3.0f}};
	REQUIRE_THAT(tree.evaluate(vars), approx(21.0f));
}

TEST_CASE("ExpressionTree - constant folding unary", "[math][expression]")
{
	ExpressionBuilder b;
	// sin(0) → 0
	auto tree = b.build(b.sin(b.number(0.0f)));

	tree.foldConstants();
	REQUIRE(tree.nodeCount() == 1);
	REQUIRE_THAT(tree.evaluate({}), approx(0.0f));
}

// ── Edge Cases ─────────────────────────────────────────

TEST_CASE("ExpressionTree - empty tree", "[math][expression]")
{
	ExpressionTree tree;
	REQUIRE(tree.empty());
	REQUIRE(tree.nodeCount() == 0);
	REQUIRE(tree.toString().empty());
	REQUIRE(tree.evaluate({}) == 0.0f);
}

TEST_CASE("ExpressionTree - single number tree", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree = b.build(b.number(0.0f));

	REQUIRE_FALSE(tree.empty());
	REQUIRE(tree.nodeCount() == 1);
}

TEST_CASE("ExpressionTree - move semantics", "[math][expression]")
{
	ExpressionBuilder b;
	auto tree1 = b.build(b.add(b.number(1.0f), b.number(2.0f)));

	ExpressionTree tree2 = std::move(tree1);
	REQUIRE_THAT(tree2.evaluate({}), approx(3.0f));
}
