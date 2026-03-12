#include <catch2/catch_test_macros.hpp>

#include "mitiru/scene/SceneGraph.hpp"

using namespace mitiru::scene;

TEST_CASE("SceneGraph - create and destroy nodes", "[mitiru][scene]")
{
	SceneGraph graph;

	auto n1 = graph.createNode("Root");
	auto n2 = graph.createNode("Child");

	REQUIRE(graph.isValid(n1));
	REQUIRE(graph.isValid(n2));
	REQUIRE(graph.nodeCount() == 2);

	auto* node = graph.getNode(n1);
	REQUIRE(node != nullptr);
	REQUIRE(node->name == "Root");

	graph.destroyNode(n1);
	REQUIRE_FALSE(graph.isValid(n1));
	REQUIRE(graph.nodeCount() == 1);
}

TEST_CASE("SceneGraph - generation counter invalidates old ids", "[mitiru][scene]")
{
	SceneGraph graph;

	auto n1 = graph.createNode("A");
	auto oldId = n1;
	graph.destroyNode(n1);
	REQUIRE_FALSE(graph.isValid(oldId));

	// 同じインデックスが再利用されるが世代が異なる
	auto n2 = graph.createNode("B");
	REQUIRE(graph.isValid(n2));
	REQUIRE_FALSE(graph.isValid(oldId));
	REQUIRE(n2.index() == oldId.index());
	REQUIRE(n2.generation() != oldId.generation());
}

TEST_CASE("SceneGraph - reparent nodes", "[mitiru][scene]")
{
	SceneGraph graph;

	auto root = graph.createNode("Root");
	auto child = graph.createNode("Child");

	graph.reparent(child, root);

	const auto* rootNode = graph.getNode(root);
	REQUIRE(rootNode->children.size() == 1);
	REQUIRE(rootNode->children[0] == child);

	const auto* childNode = graph.getNode(child);
	REQUIRE(childNode->parent == root);
}

TEST_CASE("SceneGraph - reparent prevents circular reference", "[mitiru][scene]")
{
	SceneGraph graph;

	auto a = graph.createNode("A");
	auto b = graph.createNode("B");
	auto c = graph.createNode("C");

	graph.reparent(b, a);
	graph.reparent(c, b);

	// cの子にaを設定しようとする → 循環参照 → 無視される
	graph.reparent(a, c);

	const auto* aNode = graph.getNode(a);
	REQUIRE_FALSE(aNode->parent.isValid());
}

TEST_CASE("SceneGraph - world transform walks parent chain", "[mitiru][scene]")
{
	SceneGraph graph;

	auto root = graph.createNode("Root");
	auto child = graph.createNode("Child");
	graph.reparent(child, root);

	// ルートに平行移動を設定
	graph.getNode(root)->localTransform = sgc::Mat4f::translation({10.0f, 0.0f, 0.0f});
	// 子にも平行移動を設定
	graph.getNode(child)->localTransform = sgc::Mat4f::translation({0.0f, 5.0f, 0.0f});

	auto worldMat = graph.getWorldTransform(child);
	// ワールド位置は (10, 5, 0) になるはず
	auto pos = worldMat.transformPoint({0.0f, 0.0f, 0.0f});
	REQUIRE(pos.x == 10.0f);
	REQUIRE(pos.y == 5.0f);
	REQUIRE(pos.z == 0.0f);
}

TEST_CASE("SceneGraph - findByName", "[mitiru][scene]")
{
	SceneGraph graph;

	graph.createNode("Alpha");
	graph.createNode("Beta");
	auto gamma = graph.createNode("Gamma");

	auto found = graph.findByName("Gamma");
	REQUIRE(found.has_value());
	REQUIRE(*found == gamma);

	auto notFound = graph.findByName("Delta");
	REQUIRE_FALSE(notFound.has_value());
}

TEST_CASE("SceneGraph - depth first traversal", "[mitiru][scene]")
{
	SceneGraph graph;

	auto root = graph.createNode("Root");
	auto a = graph.createNode("A");
	auto b = graph.createNode("B");
	auto a1 = graph.createNode("A1");

	graph.reparent(a, root);
	graph.reparent(b, root);
	graph.reparent(a1, a);

	std::vector<std::string> visited;
	graph.traverse(root, TraversalOrder::DepthFirst,
		[&visited](const SceneNode& node) { visited.push_back(node.name); }
	);

	REQUIRE(visited.size() == 4);
	REQUIRE(visited[0] == "Root");
	REQUIRE(visited[1] == "A");
	REQUIRE(visited[2] == "A1");
	REQUIRE(visited[3] == "B");
}

TEST_CASE("SceneGraph - breadth first traversal", "[mitiru][scene]")
{
	SceneGraph graph;

	auto root = graph.createNode("Root");
	auto a = graph.createNode("A");
	auto b = graph.createNode("B");
	auto a1 = graph.createNode("A1");

	graph.reparent(a, root);
	graph.reparent(b, root);
	graph.reparent(a1, a);

	std::vector<std::string> visited;
	graph.traverse(root, TraversalOrder::BreadthFirst,
		[&visited](const SceneNode& node) { visited.push_back(node.name); }
	);

	REQUIRE(visited.size() == 4);
	REQUIRE(visited[0] == "Root");
	REQUIRE(visited[1] == "A");
	REQUIRE(visited[2] == "B");
	REQUIRE(visited[3] == "A1");
}

TEST_CASE("SceneGraph - destroy node cascades to children", "[mitiru][scene]")
{
	SceneGraph graph;

	auto root = graph.createNode("Root");
	auto child = graph.createNode("Child");
	auto grandchild = graph.createNode("Grandchild");

	graph.reparent(child, root);
	graph.reparent(grandchild, child);

	graph.destroyNode(root);

	REQUIRE_FALSE(graph.isValid(root));
	REQUIRE_FALSE(graph.isValid(child));
	REQUIRE_FALSE(graph.isValid(grandchild));
	REQUIRE(graph.nodeCount() == 0);
}
