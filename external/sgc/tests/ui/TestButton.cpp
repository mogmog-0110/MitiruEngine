/// @file TestButton.cpp
/// @brief Button evaluation utility tests

#include <catch2/catch_test_macros.hpp>

#include "sgc/ui/Button.hpp"

using namespace sgc;
using namespace sgc::ui;

static const Rectf BUTTON_RECT{{100.0f, 100.0f}, {100.0f, 40.0f}};

// ── State tests ─────────────────────────────────────────

TEST_CASE("evaluateButton returns Normal when mouse outside", "[ui][button]")
{
	auto result = evaluateButton(BUTTON_RECT, {0.0f, 0.0f}, false, false);
	REQUIRE(result.state == WidgetState::Normal);
	REQUIRE_FALSE(result.clicked);
}

TEST_CASE("evaluateButton returns Hovered when mouse inside", "[ui][button]")
{
	auto result = evaluateButton(BUTTON_RECT, {150.0f, 120.0f}, false, false);
	REQUIRE(result.state == WidgetState::Hovered);
	REQUIRE_FALSE(result.clicked);
}

TEST_CASE("evaluateButton returns Pressed when mouse inside and down", "[ui][button]")
{
	auto result = evaluateButton(BUTTON_RECT, {150.0f, 120.0f}, true, false);
	REQUIRE(result.state == WidgetState::Pressed);
	REQUIRE_FALSE(result.clicked);
}

TEST_CASE("evaluateButton returns clicked when mouse inside and pressed", "[ui][button]")
{
	auto result = evaluateButton(BUTTON_RECT, {150.0f, 120.0f}, true, true);
	REQUIRE(result.state == WidgetState::Pressed);
	REQUIRE(result.clicked);
}

TEST_CASE("evaluateButton not clicked when mouse outside and pressed", "[ui][button]")
{
	auto result = evaluateButton(BUTTON_RECT, {0.0f, 0.0f}, true, true);
	REQUIRE(result.state == WidgetState::Normal);
	REQUIRE_FALSE(result.clicked);
}

TEST_CASE("evaluateButton returns Disabled when not enabled", "[ui][button]")
{
	auto result = evaluateButton(BUTTON_RECT, {150.0f, 120.0f}, false, false, false);
	REQUIRE(result.state == WidgetState::Disabled);
	REQUIRE_FALSE(result.clicked);
}

TEST_CASE("evaluateButton Disabled blocks click", "[ui][button]")
{
	auto result = evaluateButton(BUTTON_RECT, {150.0f, 120.0f}, true, true, false);
	REQUIRE(result.state == WidgetState::Disabled);
	REQUIRE_FALSE(result.clicked);
}

TEST_CASE("evaluateButton Disabled with mouse outside", "[ui][button]")
{
	auto result = evaluateButton(BUTTON_RECT, {0.0f, 0.0f}, false, false, false);
	REQUIRE(result.state == WidgetState::Disabled);
	REQUIRE_FALSE(result.clicked);
}

TEST_CASE("evaluateButton mouseDown outside does not affect state", "[ui][button]")
{
	auto result = evaluateButton(BUTTON_RECT, {0.0f, 0.0f}, true, false);
	REQUIRE(result.state == WidgetState::Normal);
	REQUIRE_FALSE(result.clicked);
}

TEST_CASE("evaluateButton is constexpr evaluable", "[ui][button]")
{
	constexpr Rectf rect{{0.0f, 0.0f}, {100.0f, 50.0f}};
	constexpr auto result = evaluateButton(rect, {50.0f, 25.0f}, false, false);
	REQUIRE(result.state == WidgetState::Hovered);
	REQUIRE_FALSE(result.clicked);
}
