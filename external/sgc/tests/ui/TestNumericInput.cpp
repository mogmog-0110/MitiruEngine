/// @file TestNumericInput.cpp
/// @brief NumericInput evaluation utility tests

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/ui/NumericInput.hpp"

using namespace sgc;
using namespace sgc::ui;
using Catch::Matchers::WithinAbs;

static const Rectf INPUT_RECT{100.0f, 200.0f, 200.0f, 40.0f};

// ── Disabled state ──────────────────────────────────────

TEST_CASE("evaluateNumericInput returns Disabled when not enabled", "[ui][numericinput]")
{
	auto result = evaluateNumericInput(
		INPUT_RECT, {110.0f, 220.0f}, true, true,
		50.0f, 0.0f, 100.0f, 1.0f, false);
	REQUIRE(result.state == WidgetState::Disabled);
	REQUIRE(result.value == 50.0f);
	REQUIRE_FALSE(result.changed);
}

// ── Normal state ────────────────────────────────────────

TEST_CASE("evaluateNumericInput returns Normal when mouse outside", "[ui][numericinput]")
{
	auto result = evaluateNumericInput(
		INPUT_RECT, {0.0f, 0.0f}, false, false,
		50.0f, 0.0f, 100.0f);
	REQUIRE(result.state == WidgetState::Normal);
	REQUIRE(result.value == 50.0f);
	REQUIRE_FALSE(result.changed);
	REQUIRE_FALSE(result.minusFocused);
	REQUIRE_FALSE(result.plusFocused);
}

// ── Hover state ─────────────────────────────────────────

TEST_CASE("evaluateNumericInput returns Hovered when mouse inside", "[ui][numericinput]")
{
	auto result = evaluateNumericInput(
		INPUT_RECT, {200.0f, 220.0f}, false, false,
		50.0f, 0.0f, 100.0f);
	REQUIRE(result.state == WidgetState::Hovered);
}

// ── Minus button ────────────────────────────────────────

TEST_CASE("evaluateNumericInput decrements on minus click", "[ui][numericinput]")
{
	// Minus button is left 25%: x=[100, 150)
	auto result = evaluateNumericInput(
		INPUT_RECT, {120.0f, 220.0f}, true, true,
		50.0f, 0.0f, 100.0f, 5.0f);
	REQUIRE(result.changed);
	REQUIRE_THAT(result.value, WithinAbs(45.0f, 0.01f));
	REQUIRE(result.minusFocused);
	REQUIRE_FALSE(result.plusFocused);
}

TEST_CASE("evaluateNumericInput clamps to minValue on minus", "[ui][numericinput]")
{
	auto result = evaluateNumericInput(
		INPUT_RECT, {120.0f, 220.0f}, true, true,
		2.0f, 0.0f, 100.0f, 5.0f);
	REQUIRE(result.changed);
	REQUIRE_THAT(result.value, WithinAbs(0.0f, 0.01f));
}

// ── Plus button ─────────────────────────────────────────

TEST_CASE("evaluateNumericInput increments on plus click", "[ui][numericinput]")
{
	// Plus button is right 25%: x=[250, 300]
	auto result = evaluateNumericInput(
		INPUT_RECT, {260.0f, 220.0f}, true, true,
		50.0f, 0.0f, 100.0f, 5.0f);
	REQUIRE(result.changed);
	REQUIRE_THAT(result.value, WithinAbs(55.0f, 0.01f));
	REQUIRE_FALSE(result.minusFocused);
	REQUIRE(result.plusFocused);
}

TEST_CASE("evaluateNumericInput clamps to maxValue on plus", "[ui][numericinput]")
{
	auto result = evaluateNumericInput(
		INPUT_RECT, {260.0f, 220.0f}, true, true,
		98.0f, 0.0f, 100.0f, 5.0f);
	REQUIRE(result.changed);
	REQUIRE_THAT(result.value, WithinAbs(100.0f, 0.01f));
}

// ── Center area (no change) ─────────────────────────────

TEST_CASE("evaluateNumericInput no change on center click", "[ui][numericinput]")
{
	// Center area: x=[150, 250)
	auto result = evaluateNumericInput(
		INPUT_RECT, {200.0f, 220.0f}, true, true,
		50.0f, 0.0f, 100.0f);
	REQUIRE_FALSE(result.changed);
	REQUIRE(result.value == 50.0f);
}

// ── Helper rects ────────────────────────────────────────

TEST_CASE("numericInputMinusRect returns left quarter", "[ui][numericinput]")
{
	const auto rect = numericInputMinusRect(INPUT_RECT);
	REQUIRE_THAT(rect.x(), WithinAbs(100.0f, 0.01f));
	REQUIRE_THAT(rect.width(), WithinAbs(50.0f, 0.01f));
}

TEST_CASE("numericInputPlusRect returns right quarter", "[ui][numericinput]")
{
	const auto rect = numericInputPlusRect(INPUT_RECT);
	REQUIRE_THAT(rect.x(), WithinAbs(250.0f, 0.01f));
	REQUIRE_THAT(rect.width(), WithinAbs(50.0f, 0.01f));
}

TEST_CASE("numericInputValueRect returns center half", "[ui][numericinput]")
{
	const auto rect = numericInputValueRect(INPUT_RECT);
	REQUIRE_THAT(rect.x(), WithinAbs(150.0f, 0.01f));
	REQUIRE_THAT(rect.width(), WithinAbs(100.0f, 0.01f));
}

// ── constexpr ───────────────────────────────────────────

TEST_CASE("evaluateNumericInput is constexpr evaluable", "[ui][numericinput]")
{
	constexpr Rectf rect{0.0f, 0.0f, 200.0f, 40.0f};
	constexpr auto result = evaluateNumericInput(
		rect, {10.0f, 20.0f}, true, true,
		50.0f, 0.0f, 100.0f, 1.0f);
	REQUIRE(result.changed);
	REQUIRE_THAT(result.value, WithinAbs(49.0f, 0.01f));
}
