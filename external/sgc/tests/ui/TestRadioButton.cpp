/// @file TestRadioButton.cpp
/// @brief ラジオボタン評価ユーティリティのテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/ui/RadioButton.hpp"

using namespace sgc;
using namespace sgc::ui;

static const Rectf RADIO_RECT{{100.0f, 200.0f}, {200.0f, 30.0f}};

// ── Disabled state ──────────────────────────────────────

TEST_CASE("evaluateRadio returns Disabled when not enabled", "[ui][radio]")
{
	auto result = evaluateRadio(
		RADIO_RECT, {150.0f, 210.0f}, true, true, 1, 0, false);
	REQUIRE(result.state == WidgetState::Disabled);
	REQUIRE(result.selected == 0);
	REQUIRE_FALSE(result.changed);
}

// ── Click on unselected ─────────────────────────────────

TEST_CASE("evaluateRadio click on unselected changes selection", "[ui][radio]")
{
	auto result = evaluateRadio(
		RADIO_RECT, {150.0f, 210.0f}, true, true, 2, 0);
	REQUIRE(result.state == WidgetState::Pressed);
	REQUIRE(result.selected == 2);
	REQUIRE(result.changed);
}

// ── Click on already-selected ───────────────────────────

TEST_CASE("evaluateRadio click on already-selected does not change", "[ui][radio]")
{
	auto result = evaluateRadio(
		RADIO_RECT, {150.0f, 210.0f}, true, true, 0, 0);
	REQUIRE(result.state == WidgetState::Pressed);
	REQUIRE(result.selected == 0);
	REQUIRE_FALSE(result.changed);
}

// ── Mouse outside ───────────────────────────────────────

TEST_CASE("evaluateRadio returns Normal when mouse outside", "[ui][radio]")
{
	auto result = evaluateRadio(
		RADIO_RECT, {0.0f, 0.0f}, false, false, 1, 0);
	REQUIRE(result.state == WidgetState::Normal);
	REQUIRE(result.selected == 0);
	REQUIRE_FALSE(result.changed);
}

// ── Hover and press states ──────────────────────────────

TEST_CASE("evaluateRadio returns Hovered when mouse inside without press", "[ui][radio]")
{
	auto result = evaluateRadio(
		RADIO_RECT, {150.0f, 210.0f}, false, false, 1, 0);
	REQUIRE(result.state == WidgetState::Hovered);
}

TEST_CASE("evaluateRadio returns Pressed when mouse inside and down", "[ui][radio]")
{
	auto result = evaluateRadio(
		RADIO_RECT, {150.0f, 210.0f}, true, false, 1, 0);
	REQUIRE(result.state == WidgetState::Pressed);
}

// ── radioCircleCenter ───────────────────────────────────

TEST_CASE("radioCircleCenter returns center at left side of bounds", "[ui][radio]")
{
	// center = (x + h/2, y + h/2) = (100 + 15, 200 + 15) = (115, 215)
	auto center = radioCircleCenter(RADIO_RECT);
	REQUIRE_THAT(center.x, Catch::Matchers::WithinAbs(115.0, 0.001));
	REQUIRE_THAT(center.y, Catch::Matchers::WithinAbs(215.0, 0.001));
}

// ── constexpr ───────────────────────────────────────────

TEST_CASE("evaluateRadio is constexpr evaluable", "[ui][radio]")
{
	constexpr Rectf rect{{0.0f, 0.0f}, {100.0f, 30.0f}};
	constexpr auto result = evaluateRadio(
		rect, {50.0f, 15.0f}, true, true, 2, 0);
	REQUIRE(result.selected == 2);
	REQUIRE(result.changed);
}
