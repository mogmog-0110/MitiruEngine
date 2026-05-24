/// @file TestToggleSwitch.cpp
/// @brief トグルスイッチ評価ユーティリティのテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/ui/ToggleSwitch.hpp"

using namespace sgc;
using namespace sgc::ui;

static const Rectf TOGGLE_RECT{{50.0f, 50.0f}, {60.0f, 30.0f}};

// ── Disabled state ──────────────────────────────────────

TEST_CASE("evaluateToggle returns Disabled when not enabled", "[ui][toggle]")
{
	auto result = evaluateToggle(
		TOGGLE_RECT, {60.0f, 60.0f}, true, true, false, false);
	REQUIRE(result.state == WidgetState::Disabled);
	REQUIRE_FALSE(result.value);
	REQUIRE_FALSE(result.toggled);
}

TEST_CASE("evaluateToggle Disabled preserves isOn state", "[ui][toggle]")
{
	auto result = evaluateToggle(
		TOGGLE_RECT, {60.0f, 60.0f}, true, true, true, false);
	REQUIRE(result.state == WidgetState::Disabled);
	REQUIRE(result.value);
	REQUIRE_FALSE(result.toggled);
}

// ── Mouse outside ───────────────────────────────────────

TEST_CASE("evaluateToggle returns Normal when mouse outside", "[ui][toggle]")
{
	auto result = evaluateToggle(
		TOGGLE_RECT, {0.0f, 0.0f}, false, false, false);
	REQUIRE(result.state == WidgetState::Normal);
	REQUIRE_FALSE(result.toggled);
}

// ── Hover and press states ──────────────────────────────

TEST_CASE("evaluateToggle returns Hovered when mouse inside no press", "[ui][toggle]")
{
	auto result = evaluateToggle(
		TOGGLE_RECT, {60.0f, 60.0f}, false, false, false);
	REQUIRE(result.state == WidgetState::Hovered);
}

TEST_CASE("evaluateToggle returns Pressed when mouse inside and mouseDown", "[ui][toggle]")
{
	auto result = evaluateToggle(
		TOGGLE_RECT, {60.0f, 60.0f}, true, false, false);
	REQUIRE(result.state == WidgetState::Pressed);
}

// ── Click toggles ───────────────────────────────────────

TEST_CASE("evaluateToggle click toggles ON to OFF", "[ui][toggle]")
{
	auto result = evaluateToggle(
		TOGGLE_RECT, {60.0f, 60.0f}, true, true, true);
	REQUIRE(result.state == WidgetState::Pressed);
	REQUIRE_FALSE(result.value);
	REQUIRE(result.toggled);
}

TEST_CASE("evaluateToggle click toggles OFF to ON", "[ui][toggle]")
{
	auto result = evaluateToggle(
		TOGGLE_RECT, {60.0f, 60.0f}, true, true, false);
	REQUIRE(result.state == WidgetState::Pressed);
	REQUIRE(result.value);
	REQUIRE(result.toggled);
}

// ── No toggle when outside ──────────────────────────────

TEST_CASE("evaluateToggle mouse outside with mousePressed does not toggle", "[ui][toggle]")
{
	auto result = evaluateToggle(
		TOGGLE_RECT, {0.0f, 0.0f}, true, true, false);
	REQUIRE(result.state == WidgetState::Normal);
	REQUIRE_FALSE(result.value);
	REQUIRE_FALSE(result.toggled);
}

// ── toggleKnobRect ─────────────────────────────────────

TEST_CASE("toggleKnobRect OFF returns knob at left", "[ui][toggle]")
{
	auto knob = toggleKnobRect(TOGGLE_RECT, false);
	REQUIRE_THAT(knob.x(), Catch::Matchers::WithinAbs(50.0, 0.001));
	REQUIRE_THAT(knob.y(), Catch::Matchers::WithinAbs(50.0, 0.001));
	REQUIRE_THAT(knob.width(), Catch::Matchers::WithinAbs(30.0, 0.001));
	REQUIRE_THAT(knob.height(), Catch::Matchers::WithinAbs(30.0, 0.001));
}

TEST_CASE("toggleKnobRect ON returns knob at right", "[ui][toggle]")
{
	auto knob = toggleKnobRect(TOGGLE_RECT, true);
	// x = bounds.x + bounds.width - height = 50 + 60 - 30 = 80
	REQUIRE_THAT(knob.x(), Catch::Matchers::WithinAbs(80.0, 0.001));
	REQUIRE_THAT(knob.y(), Catch::Matchers::WithinAbs(50.0, 0.001));
	REQUIRE_THAT(knob.width(), Catch::Matchers::WithinAbs(30.0, 0.001));
	REQUIRE_THAT(knob.height(), Catch::Matchers::WithinAbs(30.0, 0.001));
}

// ── constexpr ───────────────────────────────────────────

TEST_CASE("evaluateToggle is constexpr evaluable", "[ui][toggle]")
{
	constexpr Rectf rect{{0.0f, 0.0f}, {60.0f, 30.0f}};
	constexpr auto result = evaluateToggle(
		rect, {30.0f, 15.0f}, true, true, false);
	REQUIRE(result.value);
	REQUIRE(result.toggled);
}
