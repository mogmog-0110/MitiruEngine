#include <catch2/catch_test_macros.hpp>

#include "sgc/input/InputModeManager.hpp"

using sgc::InputMode;
using sgc::InputModeManager;

TEST_CASE("InputModeManager - default mode is Gameplay", "[input][InputModeManager]")
{
	InputModeManager mgr;
	REQUIRE(mgr.currentMode() == InputMode::Gameplay);
	REQUIRE(mgr.isGameplay());
	REQUIRE(mgr.depth() == 1);
}

TEST_CASE("InputModeManager - setMode replaces stack", "[input][InputModeManager]")
{
	InputModeManager mgr;

	mgr.pushMode(InputMode::Menu);
	mgr.pushMode(InputMode::TextInput);
	REQUIRE(mgr.depth() == 3);

	mgr.setMode(InputMode::Cutscene);
	REQUIRE(mgr.currentMode() == InputMode::Cutscene);
	REQUIRE(mgr.depth() == 1);
}

TEST_CASE("InputModeManager - push and pop", "[input][InputModeManager]")
{
	InputModeManager mgr;

	mgr.pushMode(InputMode::TextInput);
	REQUIRE(mgr.isTextInput());
	REQUIRE_FALSE(mgr.isGameplay());
	REQUIRE(mgr.depth() == 2);

	mgr.popMode();
	REQUIRE(mgr.isGameplay());
	REQUIRE(mgr.depth() == 1);
}

TEST_CASE("InputModeManager - pop does not remove last element", "[input][InputModeManager]")
{
	InputModeManager mgr;

	mgr.popMode();
	REQUIRE(mgr.isGameplay());
	REQUIRE(mgr.depth() == 1);

	mgr.popMode();
	REQUIRE(mgr.depth() == 1);
}

TEST_CASE("InputModeManager - menu mode", "[input][InputModeManager]")
{
	InputModeManager mgr;

	mgr.pushMode(InputMode::Menu);
	REQUIRE(mgr.isMenu());
	REQUIRE_FALSE(mgr.isGameplay());
	REQUIRE(mgr.isInputEnabled());
	REQUIRE_FALSE(mgr.isGameplayInputEnabled());
}

TEST_CASE("InputModeManager - disabled mode", "[input][InputModeManager]")
{
	InputModeManager mgr;

	mgr.pushMode(InputMode::Disabled);
	REQUIRE_FALSE(mgr.isInputEnabled());
	REQUIRE_FALSE(mgr.isGameplayInputEnabled());
}

TEST_CASE("InputModeManager - nested push/pop", "[input][InputModeManager]")
{
	InputModeManager mgr;

	mgr.pushMode(InputMode::Menu);
	mgr.pushMode(InputMode::TextInput);
	REQUIRE(mgr.isTextInput());
	REQUIRE(mgr.depth() == 3);

	mgr.popMode();
	REQUIRE(mgr.isMenu());

	mgr.popMode();
	REQUIRE(mgr.isGameplay());
}

TEST_CASE("InputModeManager - reset restores default", "[input][InputModeManager]")
{
	InputModeManager mgr;

	mgr.pushMode(InputMode::TextInput);
	mgr.pushMode(InputMode::Disabled);
	mgr.reset();

	REQUIRE(mgr.isGameplay());
	REQUIRE(mgr.depth() == 1);
}

TEST_CASE("InputModeManager - isGameplayInputEnabled", "[input][InputModeManager]")
{
	InputModeManager mgr;

	REQUIRE(mgr.isGameplayInputEnabled());

	mgr.pushMode(InputMode::TextInput);
	REQUIRE_FALSE(mgr.isGameplayInputEnabled());
	REQUIRE(mgr.isInputEnabled());
}

TEST_CASE("InputModeManager - MAX_DEPTH guard", "[input][InputModeManager]")
{
	InputModeManager mgr;

	// depth starts at 1 (Gameplay), push until MAX_DEPTH
	for (std::size_t i = 1; i < InputModeManager::MAX_DEPTH; ++i)
	{
		REQUIRE(mgr.pushMode(InputMode::Menu));
	}
	REQUIRE(mgr.depth() == InputModeManager::MAX_DEPTH);

	// further push should fail
	REQUIRE_FALSE(mgr.pushMode(InputMode::TextInput));
	REQUIRE(mgr.depth() == InputModeManager::MAX_DEPTH);

	// pop one and push should succeed again
	mgr.popMode();
	REQUIRE(mgr.pushMode(InputMode::TextInput));
}

TEST_CASE("InputModeManager - pushMode returns true on success", "[input][InputModeManager]")
{
	InputModeManager mgr;
	REQUIRE(mgr.pushMode(InputMode::Menu));
	REQUIRE(mgr.depth() == 2);
}
