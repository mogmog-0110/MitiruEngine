/// @file TestTreeView.cpp
/// @brief TreeView evaluation utility tests

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/ui/TreeView.hpp"

using namespace sgc;
using namespace sgc::ui;
using Catch::Matchers::WithinAbs;

static const Rectf TREE_RECT{{50.0f, 50.0f}, {300.0f, 200.0f}};

// Tree structure:
// [0] Root (depth 0, parent -1)
//   [1] Child A (depth 1, parent 0)
//   [2] Child B (depth 1, parent 0)
//     [3] Grandchild (depth 2, parent 2)
static const int32_t PARENTS[] = {-1, 0, 0, 2};
static const int32_t DEPTHS[] = {0, 1, 1, 2};

static TreeViewConfig makeConfig(const TreeNodeState* states, float scroll = 0.0f)
{
	return {TREE_RECT, 4, PARENTS, DEPTHS, states, 24.0f, 20.0f, scroll};
}

TEST_CASE("evaluateTreeView returns Normal when mouse outside", "[ui][treeview]")
{
	TreeNodeState states[] = {{true, false}, {false, false}, {true, false}, {false, false}};
	auto config = makeConfig(states);
	auto result = evaluateTreeView(config, {0.0f, 0.0f}, false, false, 0.0f);
	REQUIRE(result.state == WidgetState::Normal);
	REQUIRE(result.hoveredNode == -1);
	REQUIRE(result.clickedNode == -1);
}

TEST_CASE("evaluateTreeView detects hovered node", "[ui][treeview]")
{
	TreeNodeState states[] = {{true, false}, {false, false}, {true, false}, {false, false}};
	auto config = makeConfig(states);
	// Root at y=50, Child A at y=74, Child B at y=98, Grandchild at y=122
	auto result = evaluateTreeView(config, {100.0f, 60.0f}, false, false, 0.0f);
	REQUIRE(result.hoveredNode == 0);
}

TEST_CASE("evaluateTreeView detects clicked node", "[ui][treeview]")
{
	TreeNodeState states[] = {{true, false}, {false, false}, {true, false}, {false, false}};
	auto config = makeConfig(states);
	// Child A at y=74..98
	auto result = evaluateTreeView(config, {100.0f, 80.0f}, true, true, 0.0f);
	REQUIRE(result.clickedNode == 1);
}

TEST_CASE("evaluateTreeView hides children of collapsed node", "[ui][treeview]")
{
	TreeNodeState states[] = {{true, false}, {false, false}, {false, false}, {false, false}};
	auto config = makeConfig(states);
	// Node 2 is collapsed, so node 3 should not be visible
	REQUIRE_FALSE(isNodeVisible(3, PARENTS, states));
}

TEST_CASE("evaluateTreeView shows children of expanded node", "[ui][treeview]")
{
	TreeNodeState states[] = {{true, false}, {false, false}, {true, false}, {false, false}};
	auto config = makeConfig(states);
	REQUIRE(isNodeVisible(3, PARENTS, states));
}

TEST_CASE("evaluateTreeView scrolls content", "[ui][treeview]")
{
	TreeNodeState states[] = {{true, false}, {false, false}, {true, false}, {false, false}};
	auto config = makeConfig(states);
	auto result = evaluateTreeView(config, {100.0f, 100.0f}, false, false, -20.0f);
	REQUIRE_THAT(result.scrollOffset, WithinAbs(20.0f, 0.01f));
}

TEST_CASE("evaluateTreeView clamps scroll to zero", "[ui][treeview]")
{
	TreeNodeState states[] = {{true, false}, {false, false}, {true, false}, {false, false}};
	auto config = makeConfig(states);
	auto result = evaluateTreeView(config, {100.0f, 100.0f}, false, false, 100.0f);
	REQUIRE_THAT(result.scrollOffset, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("evaluateTreeView calculates node bounds", "[ui][treeview]")
{
	TreeNodeState states[] = {{true, false}, {false, false}, {true, false}, {false, false}};
	auto config = makeConfig(states);
	auto result = evaluateTreeView(config, {0.0f, 0.0f}, false, false, 0.0f);
	auto bounds = treeNodeBounds(config, result, 1);
	// Node 1 (depth 1): x = 50 + 20 = 70, y = 50 + 24 = 74
	REQUIRE_THAT(bounds.x(), WithinAbs(70.0f, 0.01f));
	REQUIRE_THAT(bounds.y(), WithinAbs(74.0f, 0.01f));
}
