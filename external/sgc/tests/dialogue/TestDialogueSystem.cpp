#include <catch2/catch_test_macros.hpp>
#include <sgc/dialogue/DialogueSystem.hpp>

using namespace sgc::dialogue;

TEST_CASE("DialogueGraph - addNode and getNode", "[dialogue]")
{
	DialogueGraph graph;
	graph.addNode(DialogueNode{"n1", "NPC", "Hello", {}});
	REQUIRE(graph.nodeCount() == 1);
	const auto* node = graph.getNode("n1");
	REQUIRE(node != nullptr);
	REQUIRE(node->speaker == "NPC");
	REQUIRE(node->text == "Hello");
}

TEST_CASE("DialogueGraph - getNode returns nullptr for missing id", "[dialogue]")
{
	DialogueGraph graph;
	REQUIRE(graph.getNode("missing") == nullptr);
}

TEST_CASE("DialogueGraph - hasNode", "[dialogue]")
{
	DialogueGraph graph;
	graph.addNode(DialogueNode{"n1", "A", "text", {}});
	REQUIRE(graph.hasNode("n1"));
	REQUIRE_FALSE(graph.hasNode("n2"));
}

TEST_CASE("DialogueRunner - start and currentNode", "[dialogue]")
{
	DialogueGraph graph;
	graph.setStartNodeId("start");
	graph.addNode(DialogueNode{"start", "NPC", "Welcome!", {}});

	DialogueRunner runner;
	runner.start(graph);
	REQUIRE_FALSE(runner.isFinished());
	const auto* node = runner.currentNode();
	REQUIRE(node != nullptr);
	REQUIRE(node->text == "Welcome!");
}

TEST_CASE("DialogueRunner - choose transitions to next node", "[dialogue]")
{
	DialogueGraph graph;
	graph.setStartNodeId("n1");
	graph.addNode(DialogueNode{"n1", "A", "text1", {
		DialogueChoice{"Go", "n2", std::nullopt}
	}});
	graph.addNode(DialogueNode{"n2", "A", "text2", {}});

	DialogueRunner runner;
	runner.start(graph);
	REQUIRE(runner.choose(0));
	REQUIRE(runner.currentNodeId() == "n2");
}

TEST_CASE("DialogueRunner - choose with invalid index fails", "[dialogue]")
{
	DialogueGraph graph;
	graph.setStartNodeId("n1");
	graph.addNode(DialogueNode{"n1", "A", "text", {
		DialogueChoice{"Only option", "n2", std::nullopt}
	}});

	DialogueRunner runner;
	runner.start(graph);
	REQUIRE_FALSE(runner.choose(5));
}

TEST_CASE("DialogueRunner - empty nextId finishes dialogue", "[dialogue]")
{
	DialogueGraph graph;
	graph.setStartNodeId("n1");
	graph.addNode(DialogueNode{"n1", "A", "Goodbye", {
		DialogueChoice{"End", "", std::nullopt}
	}});

	DialogueRunner runner;
	runner.start(graph);
	REQUIRE(runner.choose(0));
	REQUIRE(runner.isFinished());
}

TEST_CASE("DialogueRunner - missing target node finishes dialogue", "[dialogue]")
{
	DialogueGraph graph;
	graph.setStartNodeId("n1");
	graph.addNode(DialogueNode{"n1", "A", "text", {
		DialogueChoice{"Go", "nonexistent", std::nullopt}
	}});

	DialogueRunner runner;
	runner.start(graph);
	REQUIRE(runner.choose(0));
	REQUIRE(runner.isFinished());
}

TEST_CASE("DialogueRunner - conditional choice filtering", "[dialogue]")
{
	DialogueGraph graph;
	graph.setStartNodeId("n1");
	graph.addNode(DialogueNode{"n1", "A", "text", {
		DialogueChoice{"Always visible", "n2", std::nullopt},
		DialogueChoice{"Need key", "n3",
			ConditionFunc{[](const DialogueVariables& vars) {
				auto it = vars.find("hasKey");
				return it != vars.end() && std::get<bool>(it->second);
			}}
		}
	}});

	DialogueRunner runner;
	runner.start(graph);

	// 条件未設定→1つだけ表示
	auto choices = runner.availableChoices();
	REQUIRE(choices.size() == 1);

	// 条件設定→2つ表示
	runner.setVariable("hasKey", true);
	choices = runner.availableChoices();
	REQUIRE(choices.size() == 2);
}

TEST_CASE("DialogueRunner - setVariable and getVariable", "[dialogue]")
{
	DialogueRunner runner;
	runner.setVariable("score", 100);
	runner.setVariable("name", std::string("Hero"));

	auto score = runner.getVariable("score");
	REQUIRE(score.has_value());
	REQUIRE(std::get<int>(score.value()) == 100);

	auto name = runner.getVariable("name");
	REQUIRE(name.has_value());
	REQUIRE(std::get<std::string>(name.value()) == "Hero");

	REQUIRE_FALSE(runner.getVariable("missing").has_value());
}

TEST_CASE("DialogueRunner - start with custom start id", "[dialogue]")
{
	DialogueGraph graph;
	graph.setStartNodeId("n1");
	graph.addNode(DialogueNode{"n1", "A", "first", {}});
	graph.addNode(DialogueNode{"n2", "B", "second", {}});

	DialogueRunner runner;
	runner.start(graph, "n2");
	const auto* node = runner.currentNode();
	REQUIRE(node != nullptr);
	REQUIRE(node->text == "second");
}

TEST_CASE("DialogueRunner - isFinished before start", "[dialogue]")
{
	DialogueRunner runner;
	REQUIRE(runner.isFinished());
	REQUIRE(runner.currentNode() == nullptr);
}
