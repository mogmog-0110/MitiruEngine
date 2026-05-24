/// @file TestTextInput.cpp
/// @brief TextInput evaluation utility tests

#include <catch2/catch_test_macros.hpp>

#include "sgc/ui/TextInput.hpp"

using namespace sgc;
using namespace sgc::ui;

static const Rectf INPUT_RECT{{100.0f, 200.0f}, {300.0f, 30.0f}};

TEST_CASE("evaluateTextInput returns Normal when unfocused and mouse outside", "[ui][textinput]")
{
	TextInputConfig config{INPUT_RECT, "hello", 5, -1, -1, false, 100};
	auto result = evaluateTextInput(config, {0.0f, 0.0f},
		false, false, false, false, false, false, false, false, false, false, false);
	REQUIRE(result.state == WidgetState::Normal);
	REQUIRE_FALSE(result.isFocused);
}

TEST_CASE("evaluateTextInput gains focus on click inside", "[ui][textinput]")
{
	TextInputConfig config{INPUT_RECT, "hello", 0, -1, -1, false, 100};
	auto result = evaluateTextInput(config, {200.0f, 215.0f},
		true, true, false, false, false, false, false, false, false, false, false);
	REQUIRE(result.isFocused);
}

TEST_CASE("evaluateTextInput loses focus on click outside", "[ui][textinput]")
{
	TextInputConfig config{INPUT_RECT, "hello", 3, -1, -1, true, 100};
	auto result = evaluateTextInput(config, {0.0f, 0.0f},
		true, true, false, false, false, false, false, false, false, false, false);
	REQUIRE_FALSE(result.isFocused);
}

TEST_CASE("evaluateTextInput moves cursor left", "[ui][textinput]")
{
	TextInputConfig config{INPUT_RECT, "hello", 3, -1, -1, true, 100};
	auto result = evaluateTextInput(config, {200.0f, 215.0f},
		false, false, false, false, false, true, false, false, false, false, false);
	REQUIRE(result.cursorPos == 2);
	REQUIRE(result.action == TextInputAction::MoveCursorLeft);
}

TEST_CASE("evaluateTextInput moves cursor right", "[ui][textinput]")
{
	TextInputConfig config{INPUT_RECT, "hello", 2, -1, -1, true, 100};
	auto result = evaluateTextInput(config, {200.0f, 215.0f},
		false, false, false, false, false, false, true, false, false, false, false);
	REQUIRE(result.cursorPos == 3);
	REQUIRE(result.action == TextInputAction::MoveCursorRight);
}

TEST_CASE("evaluateTextInput moves cursor to start on Home", "[ui][textinput]")
{
	TextInputConfig config{INPUT_RECT, "hello", 3, -1, -1, true, 100};
	auto result = evaluateTextInput(config, {200.0f, 215.0f},
		false, false, false, false, false, false, false, true, false, false, false);
	REQUIRE(result.cursorPos == 0);
	REQUIRE(result.action == TextInputAction::MoveToStart);
}

TEST_CASE("evaluateTextInput moves cursor to end on End", "[ui][textinput]")
{
	TextInputConfig config{INPUT_RECT, "hello", 0, -1, -1, true, 100};
	auto result = evaluateTextInput(config, {200.0f, 215.0f},
		false, false, false, false, false, false, false, false, true, false, false);
	REQUIRE(result.cursorPos == 5);
	REQUIRE(result.action == TextInputAction::MoveToEnd);
}

TEST_CASE("evaluateTextInput cursor does not go below zero", "[ui][textinput]")
{
	TextInputConfig config{INPUT_RECT, "hello", 0, -1, -1, true, 100};
	auto result = evaluateTextInput(config, {200.0f, 215.0f},
		false, false, false, false, false, true, false, false, false, false, false);
	REQUIRE(result.cursorPos == 0);
}
