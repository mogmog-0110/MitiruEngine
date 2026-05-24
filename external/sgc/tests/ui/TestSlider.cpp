/// @file TestSlider.cpp
/// @brief Slider evaluation utility tests

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/ui/Slider.hpp"

using namespace sgc;
using namespace sgc::ui;
using Catch::Matchers::WithinAbs;

static const Rectf SLIDER_RECT{{100.0f, 200.0f}, {200.0f, 20.0f}};

// ── Disabled state ──────────────────────────────────────

TEST_CASE("evaluateSlider returns Disabled when not enabled", "[ui][slider]")
{
	auto result = evaluateSlider(
		SLIDER_RECT, {150.0f, 210.0f}, true, true,
		50.0f, 0.0f, 100.0f, false, false);
	REQUIRE(result.state == WidgetState::Disabled);
	REQUIRE(result.value == 50.0f);
	REQUIRE_FALSE(result.changed);
	REQUIRE_FALSE(result.dragging);
}

// ── Normal / Hover state ────────────────────────────────

TEST_CASE("evaluateSlider returns Normal when mouse outside", "[ui][slider]")
{
	auto result = evaluateSlider(
		SLIDER_RECT, {0.0f, 0.0f}, false, false,
		50.0f, 0.0f, 100.0f);
	REQUIRE(result.state == WidgetState::Normal);
	REQUIRE(result.value == 50.0f);
	REQUIRE_FALSE(result.changed);
	REQUIRE_FALSE(result.dragging);
}

TEST_CASE("evaluateSlider returns Hovered when mouse inside without press", "[ui][slider]")
{
	auto result = evaluateSlider(
		SLIDER_RECT, {200.0f, 210.0f}, false, false,
		50.0f, 0.0f, 100.0f);
	REQUIRE(result.state == WidgetState::Hovered);
	REQUIRE_FALSE(result.dragging);
}

// ── Drag start ──────────────────────────────────────────

TEST_CASE("evaluateSlider starts drag on press inside", "[ui][slider]")
{
	// Mouse at x=200 => relX=100, t=100/200=0.5 => value=50
	auto result = evaluateSlider(
		SLIDER_RECT, {200.0f, 210.0f}, true, true,
		0.0f, 0.0f, 100.0f);
	REQUIRE(result.state == WidgetState::Pressed);
	REQUIRE(result.dragging);
	REQUIRE(result.changed);
	REQUIRE_THAT(result.value, WithinAbs(50.0f, 0.01f));
}

TEST_CASE("evaluateSlider does not start drag on press outside", "[ui][slider]")
{
	auto result = evaluateSlider(
		SLIDER_RECT, {0.0f, 0.0f}, true, true,
		50.0f, 0.0f, 100.0f);
	REQUIRE_FALSE(result.dragging);
	REQUIRE(result.value == 50.0f);
	REQUIRE_FALSE(result.changed);
}

// ── Drag continue ───────────────────────────────────────

TEST_CASE("evaluateSlider continues drag when wasDragging and mouseDown", "[ui][slider]")
{
	// Mouse at x=250 => relX=150, t=150/200=0.75 => value=75
	auto result = evaluateSlider(
		SLIDER_RECT, {250.0f, 300.0f}, true, false,
		50.0f, 0.0f, 100.0f, true);
	REQUIRE(result.dragging);
	REQUIRE(result.changed);
	REQUIRE_THAT(result.value, WithinAbs(75.0f, 0.01f));
}

TEST_CASE("evaluateSlider stops drag when mouse released", "[ui][slider]")
{
	auto result = evaluateSlider(
		SLIDER_RECT, {250.0f, 300.0f}, false, false,
		75.0f, 0.0f, 100.0f, true);
	REQUIRE_FALSE(result.dragging);
	REQUIRE(result.value == 75.0f);
	REQUIRE_FALSE(result.changed);
}

// ── Value clamping ──────────────────────────────────────

TEST_CASE("evaluateSlider clamps value at left edge", "[ui][slider]")
{
	// Mouse at x=50 (left of slider) => relX=-50, t clamped to 0 => value=0
	auto result = evaluateSlider(
		SLIDER_RECT, {50.0f, 210.0f}, true, false,
		50.0f, 0.0f, 100.0f, true);
	REQUIRE(result.dragging);
	REQUIRE_THAT(result.value, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("evaluateSlider clamps value at right edge", "[ui][slider]")
{
	// Mouse at x=400 (right of slider) => relX=300, t clamped to 1 => value=100
	auto result = evaluateSlider(
		SLIDER_RECT, {400.0f, 210.0f}, true, false,
		50.0f, 0.0f, 100.0f, true);
	REQUIRE(result.dragging);
	REQUIRE_THAT(result.value, WithinAbs(100.0f, 0.01f));
}

// ── Value calculation ───────────────────────────────────

TEST_CASE("evaluateSlider calculates value at left boundary", "[ui][slider]")
{
	// Mouse at x=100 => relX=0, t=0 => value=0
	auto result = evaluateSlider(
		SLIDER_RECT, {100.0f, 210.0f}, true, true,
		50.0f, 0.0f, 100.0f);
	REQUIRE_THAT(result.value, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("evaluateSlider calculates value at right boundary", "[ui][slider]")
{
	// Mouse at x=300 => relX=200, t=1 => value=100
	auto result = evaluateSlider(
		SLIDER_RECT, {300.0f, 210.0f}, true, true,
		50.0f, 0.0f, 100.0f);
	REQUIRE_THAT(result.value, WithinAbs(100.0f, 0.01f));
}

TEST_CASE("evaluateSlider works with custom range", "[ui][slider]")
{
	// Mouse at x=200 => relX=100, t=0.5 => value = 10 + 0.5*(20-10) = 15
	auto result = evaluateSlider(
		SLIDER_RECT, {200.0f, 210.0f}, true, true,
		10.0f, 10.0f, 20.0f);
	REQUIRE_THAT(result.value, WithinAbs(15.0f, 0.01f));
}

TEST_CASE("evaluateSlider unchanged reports no change", "[ui][slider]")
{
	// Mouse at x=200 => value=50, currentValue already 50
	auto result = evaluateSlider(
		SLIDER_RECT, {200.0f, 210.0f}, true, true,
		50.0f, 0.0f, 100.0f);
	REQUIRE_FALSE(result.changed);
	REQUIRE_THAT(result.value, WithinAbs(50.0f, 0.01f));
}

TEST_CASE("evaluateSlider is constexpr evaluable", "[ui][slider]")
{
	constexpr Rectf rect{{0.0f, 0.0f}, {100.0f, 20.0f}};
	constexpr auto result = evaluateSlider(
		rect, {50.0f, 10.0f}, true, true,
		0.0f, 0.0f, 100.0f);
	REQUIRE(result.dragging);
	REQUIRE(result.changed);
}
