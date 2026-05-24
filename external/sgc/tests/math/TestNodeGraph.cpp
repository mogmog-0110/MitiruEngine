#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/math/NodeGraph.hpp"

using namespace sgc;

static constexpr auto approx = [](float expected) {
	return Catch::Matchers::WithinAbs(expected, 0.001f);
};

// ── Node Creation ──────────────────────────────────────

TEST_CASE("NodeGraph - add number node", "[math][nodegraph]")
{
	NodeGraph graph;
	auto id = graph.addNumberNode(42.0f, {100, 50});

	REQUIRE(graph.nodeCount() == 1);
	const auto* node = graph.findNode(id);
	REQUIRE(node != nullptr);
	REQUIRE(node->m_type == GraphNodeType::NumberLiteral);
	REQUIRE_THAT(node->m_numberValue, approx(42.0f));
	REQUIRE(node->m_ports.size() == 1);
	REQUIRE(node->m_ports[0].direction == PortDirection::Output);
}

TEST_CASE("NodeGraph - add variable node", "[math][nodegraph]")
{
	NodeGraph graph;
	auto id = graph.addVariableNode("x", {50, 100});

	const auto* node = graph.findNode(id);
	REQUIRE(node->m_type == GraphNodeType::Variable);
	REQUIRE(node->m_variableName == "x");
	REQUIRE(node->m_label == "x");
}

TEST_CASE("NodeGraph - add binary op node", "[math][nodegraph]")
{
	NodeGraph graph;
	auto id = graph.addBinaryOpNode(BinaryOp::Mul, {200, 100});

	const auto* node = graph.findNode(id);
	REQUIRE(node->m_type == GraphNodeType::BinaryOp);
	REQUIRE(node->m_binaryOp == BinaryOp::Mul);
	REQUIRE(node->m_ports.size() == 3);  // lhs, rhs, out
	REQUIRE(node->m_ports[0].direction == PortDirection::Input);
	REQUIRE(node->m_ports[1].direction == PortDirection::Input);
	REQUIRE(node->m_ports[2].direction == PortDirection::Output);
}

TEST_CASE("NodeGraph - add unary func node", "[math][nodegraph]")
{
	NodeGraph graph;
	auto id = graph.addUnaryFuncNode(UnaryFunc::Sin, {200, 100});

	const auto* node = graph.findNode(id);
	REQUIRE(node->m_type == GraphNodeType::UnaryFunc);
	REQUIRE(node->m_ports.size() == 2);  // in, out
}

TEST_CASE("NodeGraph - add output node", "[math][nodegraph]")
{
	NodeGraph graph;
	auto id = graph.addOutputNode({350, 150});

	const auto* node = graph.findNode(id);
	REQUIRE(node->m_type == GraphNodeType::Output);
	REQUIRE(node->m_ports.size() == 1);  // result input
	REQUIRE(node->m_ports[0].direction == PortDirection::Input);
}

TEST_CASE("NodeGraph - unique node IDs", "[math][nodegraph]")
{
	NodeGraph graph;
	auto id1 = graph.addNumberNode(1.0f, {0, 0});
	auto id2 = graph.addVariableNode("x", {0, 0});
	auto id3 = graph.addBinaryOpNode(BinaryOp::Add, {0, 0});

	REQUIRE(id1 != id2);
	REQUIRE(id2 != id3);
	REQUIRE(id1 != id3);
}

// ── Wire Connection ────────────────────────────────────

TEST_CASE("NodeGraph - connect output to input", "[math][nodegraph]")
{
	NodeGraph graph;
	auto nNum = graph.addNumberNode(5.0f, {0, 0});
	auto nOut = graph.addOutputNode({200, 0});

	REQUIRE(graph.connect({nNum, 0}, {nOut, 0}));
	REQUIRE(graph.wireCount() == 1);
}

TEST_CASE("NodeGraph - reject self connection", "[math][nodegraph]")
{
	NodeGraph graph;
	auto nBin = graph.addBinaryOpNode(BinaryOp::Add, {0, 0});

	// output port index 2 to input port index 0
	REQUIRE_FALSE(graph.connect({nBin, 2}, {nBin, 0}));
}

TEST_CASE("NodeGraph - reject wrong direction", "[math][nodegraph]")
{
	NodeGraph graph;
	auto nNum = graph.addNumberNode(1.0f, {0, 0});
	auto nOut = graph.addOutputNode({200, 0});

	// Try connecting input to input (nOut port 0 is Input)
	REQUIRE_FALSE(graph.connect({nOut, 0}, {nNum, 0}));
}

TEST_CASE("NodeGraph - input port replaces existing wire", "[math][nodegraph]")
{
	NodeGraph graph;
	auto n1 = graph.addNumberNode(1.0f, {0, 0});
	auto n2 = graph.addNumberNode(2.0f, {0, 50});
	auto nOut = graph.addOutputNode({200, 0});

	graph.connect({n1, 0}, {nOut, 0});
	REQUIRE(graph.wireCount() == 1);

	// 新しい接続が古いのを上書き
	graph.connect({n2, 0}, {nOut, 0});
	REQUIRE(graph.wireCount() == 1);

	const auto* wire = graph.findWireToInput({nOut, 0});
	REQUIRE(wire != nullptr);
	REQUIRE(wire->from.nodeId == n2);
}

TEST_CASE("NodeGraph - disconnect input port", "[math][nodegraph]")
{
	NodeGraph graph;
	auto nNum = graph.addNumberNode(5.0f, {0, 0});
	auto nOut = graph.addOutputNode({200, 0});

	graph.connect({nNum, 0}, {nOut, 0});
	REQUIRE(graph.wireCount() == 1);

	graph.disconnect({nOut, 0});
	REQUIRE(graph.wireCount() == 0);
}

// ── Node Removal ───────────────────────────────────────

TEST_CASE("NodeGraph - remove node deletes related wires", "[math][nodegraph]")
{
	NodeGraph graph;
	auto n1 = graph.addNumberNode(1.0f, {0, 0});
	auto n2 = graph.addBinaryOpNode(BinaryOp::Add, {100, 0});
	auto n3 = graph.addOutputNode({200, 0});

	graph.connect({n1, 0}, {n2, 0});
	graph.connect({n2, 2}, {n3, 0});
	REQUIRE(graph.wireCount() == 2);

	graph.removeNode(n2);
	REQUIRE(graph.nodeCount() == 2);
	REQUIRE(graph.wireCount() == 0);  // n2に関連するワイヤーは全て削除
}

// ── Validation ─────────────────────────────────────────

TEST_CASE("NodeGraph - isComplete for simple graph", "[math][nodegraph]")
{
	NodeGraph graph;
	auto nX = graph.addVariableNode("x", {0, 0});
	auto nOut = graph.addOutputNode({200, 0});

	REQUIRE_FALSE(graph.isComplete());  // 未接続

	graph.connect({nX, 0}, {nOut, 0});
	REQUIRE(graph.isComplete());
}

TEST_CASE("NodeGraph - isComplete with binary op", "[math][nodegraph]")
{
	NodeGraph graph;
	auto nX = graph.addVariableNode("x", {0, 0});
	auto n2 = graph.addNumberNode(2.0f, {0, 50});
	auto nMul = graph.addBinaryOpNode(BinaryOp::Mul, {150, 25});
	auto nOut = graph.addOutputNode({300, 25});

	// 片方だけ接続
	graph.connect({nX, 0}, {nMul, 0});
	graph.connect({nMul, 2}, {nOut, 0});
	REQUIRE_FALSE(graph.isComplete());

	// 両方接続
	graph.connect({n2, 0}, {nMul, 1});
	REQUIRE(graph.isComplete());
}

TEST_CASE("NodeGraph - hasLoop detects cycle", "[math][nodegraph]")
{
	NodeGraph graph;
	auto n1 = graph.addBinaryOpNode(BinaryOp::Add, {0, 0});
	auto n2 = graph.addBinaryOpNode(BinaryOp::Mul, {150, 0});

	// n1 output → n2 input
	graph.connect({n1, 2}, {n2, 0});
	REQUIRE_FALSE(graph.hasLoop());

	// n2 output → n1 input (creates cycle)
	graph.connect({n2, 2}, {n1, 0});
	REQUIRE(graph.hasLoop());
}

TEST_CASE("NodeGraph - no loop in valid graph", "[math][nodegraph]")
{
	NodeGraph graph;
	auto nX = graph.addVariableNode("x", {0, 0});
	auto nSin = graph.addUnaryFuncNode(UnaryFunc::Sin, {100, 0});
	auto nOut = graph.addOutputNode({200, 0});

	graph.connect({nX, 0}, {nSin, 0});
	graph.connect({nSin, 1}, {nOut, 0});

	REQUIRE_FALSE(graph.hasLoop());
}

// ── ExpressionTree Conversion ──────────────────────────

TEST_CASE("NodeGraph - toExpressionTree simple: x", "[math][nodegraph]")
{
	NodeGraph graph;
	auto nX = graph.addVariableNode("x", {0, 0});
	auto nOut = graph.addOutputNode({200, 0});
	graph.connect({nX, 0}, {nOut, 0});

	auto tree = graph.toExpressionTree();
	REQUIRE_FALSE(tree.empty());
	REQUIRE(tree.toString() == "x");

	VariableBindings vars{{"x", 5.0f}};
	REQUIRE_THAT(tree.evaluate(vars), approx(5.0f));
}

TEST_CASE("NodeGraph - toExpressionTree: 2 * x", "[math][nodegraph]")
{
	NodeGraph graph;
	auto nX = graph.addVariableNode("x", {0, 0});
	auto n2 = graph.addNumberNode(2.0f, {0, 50});
	auto nMul = graph.addBinaryOpNode(BinaryOp::Mul, {150, 25});
	auto nOut = graph.addOutputNode({300, 25});

	graph.connect({n2, 0}, {nMul, 0});
	graph.connect({nX, 0}, {nMul, 1});
	graph.connect({nMul, 2}, {nOut, 0});

	auto tree = graph.toExpressionTree();
	REQUIRE_FALSE(tree.empty());

	VariableBindings vars{{"x", 7.0f}};
	REQUIRE_THAT(tree.evaluate(vars), approx(14.0f));
}

TEST_CASE("NodeGraph - toExpressionTree: sin(x)", "[math][nodegraph]")
{
	NodeGraph graph;
	auto nX = graph.addVariableNode("x", {0, 0});
	auto nSin = graph.addUnaryFuncNode(UnaryFunc::Sin, {100, 0});
	auto nOut = graph.addOutputNode({250, 0});

	graph.connect({nX, 0}, {nSin, 0});
	graph.connect({nSin, 1}, {nOut, 0});

	auto tree = graph.toExpressionTree();
	REQUIRE(tree.toString() == "sin(x)");

	const float pi = 3.14159265f;
	VariableBindings vars{{"x", pi / 2.0f}};
	REQUIRE_THAT(tree.evaluate(vars), approx(1.0f));
}

TEST_CASE("NodeGraph - toExpressionTree: sin(x) + 2 * y", "[math][nodegraph]")
{
	NodeGraph graph;
	auto nX = graph.addVariableNode("x", {0, 0});
	auto nY = graph.addVariableNode("y", {0, 80});
	auto n2 = graph.addNumberNode(2.0f, {0, 160});
	auto nSin = graph.addUnaryFuncNode(UnaryFunc::Sin, {120, 0});
	auto nMul = graph.addBinaryOpNode(BinaryOp::Mul, {120, 120});
	auto nAdd = graph.addBinaryOpNode(BinaryOp::Add, {280, 60});
	auto nOut = graph.addOutputNode({420, 60});

	graph.connect({nX, 0}, {nSin, 0});           // x → sin
	graph.connect({n2, 0}, {nMul, 0});            // 2 → mul.lhs
	graph.connect({nY, 0}, {nMul, 1});            // y → mul.rhs
	graph.connect({nSin, 1}, {nAdd, 0});          // sin → add.lhs
	graph.connect({nMul, 2}, {nAdd, 1});          // mul → add.rhs
	graph.connect({nAdd, 2}, {nOut, 0});           // add → output

	auto tree = graph.toExpressionTree();
	REQUIRE_FALSE(tree.empty());

	const float pi = 3.14159265f;
	VariableBindings vars{{"x", pi / 2.0f}, {"y", 5.0f}};
	// sin(pi/2) + 2*5 = 1 + 10 = 11
	REQUIRE_THAT(tree.evaluate(vars), approx(11.0f));
}

TEST_CASE("NodeGraph - toExpressionTree incomplete returns empty", "[math][nodegraph]")
{
	NodeGraph graph;
	auto nOut = graph.addOutputNode({200, 0});

	auto tree = graph.toExpressionTree();
	REQUIRE(tree.empty());
}

TEST_CASE("NodeGraph - toExpressionTree no output returns empty", "[math][nodegraph]")
{
	NodeGraph graph;
	graph.addNumberNode(5.0f, {0, 0});

	auto tree = graph.toExpressionTree();
	REQUIRE(tree.empty());
}

// ── Node Value Update ──────────────────────────────────

TEST_CASE("NodeGraph - setNumberValue", "[math][nodegraph]")
{
	NodeGraph graph;
	auto id = graph.addNumberNode(1.0f, {0, 0});

	REQUIRE(graph.setNumberValue(id, 99.0f));
	REQUIRE_THAT(graph.findNode(id)->m_numberValue, approx(99.0f));
}

TEST_CASE("NodeGraph - setBinaryOp", "[math][nodegraph]")
{
	NodeGraph graph;
	auto id = graph.addBinaryOpNode(BinaryOp::Add, {0, 0});

	REQUIRE(graph.setBinaryOp(id, BinaryOp::Sub));
	REQUIRE(graph.findNode(id)->m_binaryOp == BinaryOp::Sub);
}

TEST_CASE("NodeGraph - setUnaryFunc", "[math][nodegraph]")
{
	NodeGraph graph;
	auto id = graph.addUnaryFuncNode(UnaryFunc::Sin, {0, 0});

	REQUIRE(graph.setUnaryFunc(id, UnaryFunc::Cos));
	REQUIRE(graph.findNode(id)->m_unaryFunc == UnaryFunc::Cos);
}

// ── Clear ──────────────────────────────────────────────

TEST_CASE("NodeGraph - clear removes all", "[math][nodegraph]")
{
	NodeGraph graph;
	auto n1 = graph.addNumberNode(1.0f, {0, 0});
	auto n2 = graph.addOutputNode({200, 0});
	graph.connect({n1, 0}, {n2, 0});

	graph.clear();
	REQUIRE(graph.nodeCount() == 0);
	REQUIRE(graph.wireCount() == 0);
}

// ── findOutputNodeId ───────────────────────────────────

TEST_CASE("NodeGraph - findOutputNodeId", "[math][nodegraph]")
{
	NodeGraph graph;
	graph.addNumberNode(1.0f, {0, 0});
	REQUIRE_FALSE(graph.findOutputNodeId().has_value());

	auto outId = graph.addOutputNode({200, 0});
	REQUIRE(graph.findOutputNodeId().value() == outId);
}
