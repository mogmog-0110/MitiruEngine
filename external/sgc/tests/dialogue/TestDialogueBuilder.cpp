#include <catch2/catch_test_macros.hpp>
#include <sgc/dialogue/DialogueBuilder.hpp>

using namespace sgc::dialogue;

TEST_CASE("DialogueBuilder - build simple graph", "[dialogue]")
{
	auto graph = DialogueBuilder()
		.node("start", "NPC", "Hello!")
			.choice("Hi", "end")
		.node("end", "NPC", "Goodbye!")
		.setStart("start")
		.build();

	REQUIRE(graph.nodeCount() == 2);
	REQUIRE(graph.startNodeId() == "start");
	REQUIRE(graph.hasNode("start"));
	REQUIRE(graph.hasNode("end"));
}

TEST_CASE("DialogueBuilder - node with multiple choices", "[dialogue]")
{
	auto graph = DialogueBuilder()
		.node("n1", "A", "Pick one")
			.choice("Option 1", "n2")
			.choice("Option 2", "n3")
			.choice("Option 3", "n4")
		.setStart("n1")
		.build();

	const auto* node = graph.getNode("n1");
	REQUIRE(node != nullptr);
	REQUIRE(node->choices.size() == 3);
	REQUIRE(node->choices[0].text == "Option 1");
	REQUIRE(node->choices[1].nextNodeId == "n3");
}

TEST_CASE("DialogueBuilder - choiceIf with condition", "[dialogue]")
{
	auto graph = DialogueBuilder()
		.node("n1", "A", "text")
			.choice("Always", "n2")
			.choiceIf("Conditional", "n3", [](const DialogueVariables& vars) {
				auto it = vars.find("flag");
				return it != vars.end() && std::get<bool>(it->second);
			})
		.setStart("n1")
		.build();

	const auto* node = graph.getNode("n1");
	REQUIRE(node != nullptr);
	REQUIRE(node->choices.size() == 2);
	REQUIRE_FALSE(node->choices[0].condition.has_value());
	REQUIRE(node->choices[1].condition.has_value());
}

TEST_CASE("DialogueBuilder - chain builds valid runner flow", "[dialogue]")
{
	auto graph = DialogueBuilder()
		.node("start", "NPC", "Welcome!")
			.choice("Continue", "mid")
		.node("mid", "NPC", "Middle")
			.choice("End", "fin")
		.node("fin", "NPC", "Done!")
		.setStart("start")
		.build();

	DialogueRunner runner;
	runner.start(graph);
	REQUIRE(runner.currentNode()->text == "Welcome!");
	runner.choose(0);
	REQUIRE(runner.currentNode()->text == "Middle");
	runner.choose(0);
	REQUIRE(runner.currentNode()->text == "Done!");
}

TEST_CASE("DialogueBuilder - empty builder produces empty graph", "[dialogue]")
{
	auto graph = DialogueBuilder().build();
	REQUIRE(graph.nodeCount() == 0);
}

TEST_CASE("DialogueBuilder - single node graph", "[dialogue]")
{
	auto graph = DialogueBuilder()
		.node("only", "Speaker", "Solo node")
		.setStart("only")
		.build();

	REQUIRE(graph.nodeCount() == 1);
	const auto* node = graph.getNode("only");
	REQUIRE(node != nullptr);
	REQUIRE(node->speaker == "Speaker");
	REQUIRE(node->choices.empty());
}
