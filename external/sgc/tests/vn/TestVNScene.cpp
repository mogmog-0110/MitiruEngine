#include <catch2/catch_test_macros.hpp>

#include "sgc/vn/VNScene.hpp"

using namespace sgc::vn;

TEST_CASE("VNScene - AddCommand increases count", "[vn][vnscene]")
{
	VNScene scene;
	REQUIRE(scene.commandCount() == 0);

	scene.addCommand({VNCommand::Type::Say, "sakura", "Hello!", ""});
	REQUIRE(scene.commandCount() == 1);

	scene.addCommand({VNCommand::Type::Say, "sakura", "World!", ""});
	REQUIRE(scene.commandCount() == 2);
}

TEST_CASE("VNScene - Start begins first command", "[vn][vnscene]")
{
	VNScene scene;
	scene.addCommand({VNCommand::Type::Say, "sakura", "Hello!", ""});
	scene.start();

	REQUIRE(scene.phase() == VNPhase::Displaying);
	REQUIRE(scene.currentSpeaker() == "sakura");
	REQUIRE_FALSE(scene.isFinished());
}

TEST_CASE("VNScene - Say command displays text", "[vn][vnscene]")
{
	VNScene scene;
	TextDisplayConfig config;
	config.charsPerSecond = 100.0f;
	scene.setTextConfig(config);

	scene.addCommand({VNCommand::Type::Say, "sakura", "Hi", ""});
	scene.start();

	REQUIRE(scene.phase() == VNPhase::Displaying);

	// Update enough to complete text
	scene.update(1.0f, false, false);
	REQUIRE(scene.phase() == VNPhase::WaitingInput);

	// Advance past the text
	scene.update(0.0f, true, false);
	REQUIRE(scene.isFinished());
}

TEST_CASE("VNScene - ShowCharacter command", "[vn][vnscene]")
{
	VNScene scene;

	CharacterDef def;
	def.id = "sakura";
	def.displayName = "Sakura";
	scene.characters().registerCharacter(std::move(def));

	VNCommand showCmd;
	showCmd.type = VNCommand::Type::ShowCharacter;
	showCmd.characterId = "sakura";
	showCmd.position = CharacterPosition::Left;
	showCmd.expression = "happy";
	scene.addCommand(std::move(showCmd));

	scene.addCommand({VNCommand::Type::Say, "sakura", "Hello!", ""});
	scene.start();

	// ShowCharacter is instant, so we should be on Say now
	REQUIRE(scene.phase() == VNPhase::Displaying);

	auto charState = scene.characters().getState("sakura");
	REQUIRE(charState.has_value());
	REQUIRE(charState->visible);
	REQUIRE(charState->position == CharacterPosition::Left);
}

TEST_CASE("VNScene - Choice command enters ShowingChoices phase", "[vn][vnscene]")
{
	VNScene scene;

	VNCommand choiceCmd;
	choiceCmd.type = VNCommand::Type::Choice;
	choiceCmd.choices = {{"Yes", "yes"}, {"No", "no"}};
	scene.addCommand(std::move(choiceCmd));

	scene.start();
	REQUIRE(scene.phase() == VNPhase::ShowingChoices);

	auto choices = scene.currentChoices();
	REQUIRE(choices.size() == 2);
	REQUIRE(std::string(choices[0].text) == "Yes");
	REQUIRE(std::string(choices[1].text) == "No");
}

TEST_CASE("VNScene - SelectChoice advances past choices", "[vn][vnscene]")
{
	VNScene scene;

	VNCommand choiceCmd;
	choiceCmd.type = VNCommand::Type::Choice;
	choiceCmd.choices = {{"Yes", "yes"}, {"No", "no"}};
	scene.addCommand(std::move(choiceCmd));

	scene.start();
	REQUIRE(scene.phase() == VNPhase::ShowingChoices);

	scene.selectChoice(0);
	// No more commands, so scene should be finished
	REQUIRE(scene.isFinished());
}

TEST_CASE("VNScene - Finished after all commands", "[vn][vnscene]")
{
	VNScene scene;
	TextDisplayConfig config;
	config.charsPerSecond = 1000.0f;
	scene.setTextConfig(config);

	scene.addCommand({VNCommand::Type::Say, "sakura", "A", ""});
	scene.addCommand({VNCommand::Type::Say, "sakura", "B", ""});

	scene.start();

	// Complete first Say
	scene.update(1.0f, false, false);  // text complete
	scene.update(0.0f, true, false);   // advance

	REQUIRE_FALSE(scene.isFinished());

	// Complete second Say
	scene.update(1.0f, false, false);  // text complete
	scene.update(0.0f, true, false);   // advance

	REQUIRE(scene.isFinished());
}

TEST_CASE("VNScene - Backlog records dialogue", "[vn][vnscene]")
{
	VNScene scene;
	TextDisplayConfig config;
	config.charsPerSecond = 1000.0f;
	scene.setTextConfig(config);

	scene.addCommand({VNCommand::Type::Say, "sakura", "Hello!", ""});
	scene.addCommand({VNCommand::Type::Say, "taro", "Hi!", ""});

	scene.start();

	// Complete first Say
	scene.update(1.0f, false, false);
	scene.update(0.0f, true, false);

	// Complete second Say
	scene.update(1.0f, false, false);
	scene.update(0.0f, true, false);

	const auto& log = scene.backlog();
	REQUIRE(log.size() == 2);

	const auto& entries = log.entries();
	REQUIRE(entries[0].speaker == "sakura");
	REQUIRE(entries[0].text == "Hello!");
	REQUIRE(entries[1].speaker == "taro");
	REQUIRE(entries[1].text == "Hi!");
}

TEST_CASE("VNScene - Skip text in Displaying phase", "[vn][vnscene]")
{
	VNScene scene;
	TextDisplayConfig config;
	config.charsPerSecond = 1.0f;  // very slow
	scene.setTextConfig(config);

	scene.addCommand({VNCommand::Type::Say, "sakura", "Long text here", ""});
	scene.start();

	REQUIRE(scene.phase() == VNPhase::Displaying);

	// Skip should show all text
	scene.update(0.0f, false, true);
	REQUIRE(scene.phase() == VNPhase::WaitingInput);
	REQUIRE(scene.textState().isComplete);
}

TEST_CASE("VNScene - Empty scene is finished immediately", "[vn][vnscene]")
{
	VNScene scene;
	scene.start();
	REQUIRE(scene.isFinished());
}
