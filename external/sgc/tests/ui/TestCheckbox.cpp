/// @file TestCheckbox.cpp
/// @brief Checkbox evaluation utility tests

#include <catch2/catch_test_macros.hpp>

#include "sgc/ui/Checkbox.hpp"

using namespace sgc;
using namespace sgc::ui;

static const Rectf CHECK_RECT{{50.0f, 50.0f}, {24.0f, 24.0f}};

// ── Disabled state ──────────────────────────────────────

TEST_CASE("evaluateCheckbox returns Disabled when not enabled", "[ui][checkbox]")
{
	auto result = evaluateCheckbox(
		CHECK_RECT, {60.0f, 60.0f}, true, true, false, false);
	REQUIRE(result.state == WidgetState::Disabled);
	REQUIRE_FALSE(result.checked);
	REQUIRE_FALSE(result.toggled);
}

TEST_CASE("evaluateCheckbox Disabled preserves checked state", "[ui][checkbox]")
{
	auto result = evaluateCheckbox(
		CHECK_RECT, {60.0f, 60.0f}, true, true, true, false);
	REQUIRE(result.state == WidgetState::Disabled);
	REQUIRE(result.checked);
	REQUIRE_FALSE(result.toggled);
}

// ── Click toggles ───────────────────────────────────────

TEST_CASE("evaluateCheckbox click toggles unchecked to checked", "[ui][checkbox]")
{
	auto result = evaluateCheckbox(
		CHECK_RECT, {60.0f, 60.0f}, true, true, false);
	REQUIRE(result.state == WidgetState::Pressed);
	REQUIRE(result.checked);
	REQUIRE(result.toggled);
}

TEST_CASE("evaluateCheckbox click toggles checked to unchecked", "[ui][checkbox]")
{
	auto result = evaluateCheckbox(
		CHECK_RECT, {60.0f, 60.0f}, true, true, true);
	REQUIRE(result.state == WidgetState::Pressed);
	REQUIRE_FALSE(result.checked);
	REQUIRE(result.toggled);
}

// ── No click preserves state ────────────────────────────

TEST_CASE("evaluateCheckbox no click preserves unchecked", "[ui][checkbox]")
{
	auto result = evaluateCheckbox(
		CHECK_RECT, {60.0f, 60.0f}, false, false, false);
	REQUIRE(result.state == WidgetState::Hovered);
	REQUIRE_FALSE(result.checked);
	REQUIRE_FALSE(result.toggled);
}

TEST_CASE("evaluateCheckbox no click preserves checked", "[ui][checkbox]")
{
	auto result = evaluateCheckbox(
		CHECK_RECT, {60.0f, 60.0f}, false, false, true);
	REQUIRE(result.state == WidgetState::Hovered);
	REQUIRE(result.checked);
	REQUIRE_FALSE(result.toggled);
}

TEST_CASE("evaluateCheckbox mouse outside does not toggle", "[ui][checkbox]")
{
	auto result = evaluateCheckbox(
		CHECK_RECT, {0.0f, 0.0f}, true, true, false);
	REQUIRE(result.state == WidgetState::Normal);
	REQUIRE_FALSE(result.checked);
	REQUIRE_FALSE(result.toggled);
}

// ── Visual states ───────────────────────────────────────

TEST_CASE("evaluateCheckbox returns Normal when mouse outside", "[ui][checkbox]")
{
	auto result = evaluateCheckbox(
		CHECK_RECT, {0.0f, 0.0f}, false, false, false);
	REQUIRE(result.state == WidgetState::Normal);
}

TEST_CASE("evaluateCheckbox returns Hovered when mouse inside", "[ui][checkbox]")
{
	auto result = evaluateCheckbox(
		CHECK_RECT, {60.0f, 60.0f}, false, false, false);
	REQUIRE(result.state == WidgetState::Hovered);
}

TEST_CASE("evaluateCheckbox returns Pressed when mouse inside and down", "[ui][checkbox]")
{
	auto result = evaluateCheckbox(
		CHECK_RECT, {60.0f, 60.0f}, true, false, false);
	REQUIRE(result.state == WidgetState::Pressed);
}

TEST_CASE("evaluateCheckbox is constexpr evaluable", "[ui][checkbox]")
{
	constexpr Rectf rect{{0.0f, 0.0f}, {20.0f, 20.0f}};
	constexpr auto result = evaluateCheckbox(
		rect, {10.0f, 10.0f}, true, true, false);
	REQUIRE(result.checked);
	REQUIRE(result.toggled);
}
