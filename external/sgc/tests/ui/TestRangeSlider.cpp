/// @file TestRangeSlider.cpp
/// @brief RangeSlider evaluation utility tests

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/ui/RangeSlider.hpp"

using namespace sgc;
using namespace sgc::ui;
using Catch::Matchers::WithinAbs;

static const Rectf SLIDER_RECT{100.0f, 200.0f, 200.0f, 20.0f};

// ── Disabled state ──────────────────────────────────────

TEST_CASE("evaluateRangeSlider returns Disabled when not enabled", "[ui][rangeslider]")
{
	auto result = evaluateRangeSlider(
		SLIDER_RECT, {150.0f, 210.0f}, true, true,
		0.2f, 0.8f, 0.0f, 1.0f,
		RangeSliderDragTarget::None, 10.0f, false);
	REQUIRE(result.state == WidgetState::Disabled);
	REQUIRE(result.low == 0.2f);
	REQUIRE(result.high == 0.8f);
	REQUIRE_FALSE(result.changed);
}

// ── Normal / Hover state ────────────────────────────────

TEST_CASE("evaluateRangeSlider returns Normal when mouse outside", "[ui][rangeslider]")
{
	auto result = evaluateRangeSlider(
		SLIDER_RECT, {0.0f, 0.0f}, false, false,
		0.2f, 0.8f, 0.0f, 1.0f);
	REQUIRE(result.state == WidgetState::Normal);
	REQUIRE_FALSE(result.changed);
	REQUIRE(result.dragTarget == RangeSliderDragTarget::None);
}

TEST_CASE("evaluateRangeSlider detects low handle hover", "[ui][rangeslider]")
{
	// low=0.2 => lowX = 100 + 0.2*200 = 140
	auto result = evaluateRangeSlider(
		SLIDER_RECT, {140.0f, 210.0f}, false, false,
		0.2f, 0.8f, 0.0f, 1.0f);
	REQUIRE(result.lowHandleFocused);
	REQUIRE_FALSE(result.highHandleFocused);
}

TEST_CASE("evaluateRangeSlider detects high handle hover", "[ui][rangeslider]")
{
	// high=0.8 => highX = 100 + 0.8*200 = 260
	auto result = evaluateRangeSlider(
		SLIDER_RECT, {260.0f, 210.0f}, false, false,
		0.2f, 0.8f, 0.0f, 1.0f);
	REQUIRE_FALSE(result.lowHandleFocused);
	REQUIRE(result.highHandleFocused);
}

// ── Drag low handle ─────────────────────────────────────

TEST_CASE("evaluateRangeSlider starts drag on low handle press", "[ui][rangeslider]")
{
	// low=0.2 => lowX=140, press at 140
	auto result = evaluateRangeSlider(
		SLIDER_RECT, {140.0f, 210.0f}, true, true,
		0.2f, 0.8f, 0.0f, 1.0f);
	REQUIRE(result.dragTarget == RangeSliderDragTarget::Low);
	// value at x=140: t=(140-100)/200=0.2 => unchanged
	REQUIRE_THAT(result.low, WithinAbs(0.2f, 0.01f));
}

TEST_CASE("evaluateRangeSlider drags low handle to new position", "[ui][rangeslider]")
{
	// Continue drag from previous frame, mouse at x=160 => t=0.3
	auto result = evaluateRangeSlider(
		SLIDER_RECT, {160.0f, 210.0f}, true, false,
		0.2f, 0.8f, 0.0f, 1.0f,
		RangeSliderDragTarget::Low);
	REQUIRE(result.dragTarget == RangeSliderDragTarget::Low);
	REQUIRE(result.changed);
	REQUIRE_THAT(result.low, WithinAbs(0.3f, 0.01f));
	REQUIRE_THAT(result.high, WithinAbs(0.8f, 0.01f));
}

TEST_CASE("evaluateRangeSlider low handle cannot exceed high", "[ui][rangeslider]")
{
	// Drag low handle beyond high=0.8 => clamped to 0.8
	auto result = evaluateRangeSlider(
		SLIDER_RECT, {280.0f, 210.0f}, true, false,
		0.2f, 0.8f, 0.0f, 1.0f,
		RangeSliderDragTarget::Low);
	REQUIRE_THAT(result.low, WithinAbs(0.8f, 0.01f));
}

// ── Drag high handle ────────────────────────────────────

TEST_CASE("evaluateRangeSlider drags high handle to new position", "[ui][rangeslider]")
{
	// Continue drag, mouse at x=240 => t=0.7
	auto result = evaluateRangeSlider(
		SLIDER_RECT, {240.0f, 210.0f}, true, false,
		0.2f, 0.8f, 0.0f, 1.0f,
		RangeSliderDragTarget::High);
	REQUIRE(result.changed);
	REQUIRE_THAT(result.high, WithinAbs(0.7f, 0.01f));
	REQUIRE_THAT(result.low, WithinAbs(0.2f, 0.01f));
}

TEST_CASE("evaluateRangeSlider high handle cannot go below low", "[ui][rangeslider]")
{
	// Drag high handle below low=0.2 => clamped to 0.2
	auto result = evaluateRangeSlider(
		SLIDER_RECT, {110.0f, 210.0f}, true, false,
		0.2f, 0.8f, 0.0f, 1.0f,
		RangeSliderDragTarget::High);
	REQUIRE_THAT(result.high, WithinAbs(0.2f, 0.01f));
}

// ── Drag stop ───────────────────────────────────────────

TEST_CASE("evaluateRangeSlider stops drag on mouse release", "[ui][rangeslider]")
{
	auto result = evaluateRangeSlider(
		SLIDER_RECT, {160.0f, 210.0f}, false, false,
		0.3f, 0.8f, 0.0f, 1.0f,
		RangeSliderDragTarget::Low);
	REQUIRE(result.dragTarget == RangeSliderDragTarget::None);
	REQUIRE_FALSE(result.changed);
}

// ── Helper functions ────────────────────────────────────

TEST_CASE("rangeSliderValueToX calculates correct position", "[ui][rangeslider]")
{
	// value=0.5, range [0,1], bounds x=100 w=200 => 100+0.5*200=200
	const float x = rangeSliderValueToX(SLIDER_RECT, 0.5f, 0.0f, 1.0f);
	REQUIRE_THAT(x, WithinAbs(200.0f, 0.01f));
}

TEST_CASE("rangeSliderXToValue calculates correct value", "[ui][rangeslider]")
{
	// x=200, bounds x=100 w=200 => t=0.5, range [0,1] => 0.5
	const float v = rangeSliderXToValue(SLIDER_RECT, 200.0f, 0.0f, 1.0f);
	REQUIRE_THAT(v, WithinAbs(0.5f, 0.01f));
}

TEST_CASE("rangeSliderXToValue clamps to range", "[ui][rangeslider]")
{
	const float vLow = rangeSliderXToValue(SLIDER_RECT, 0.0f, 0.0f, 1.0f);
	REQUIRE_THAT(vLow, WithinAbs(0.0f, 0.01f));

	const float vHigh = rangeSliderXToValue(SLIDER_RECT, 500.0f, 0.0f, 1.0f);
	REQUIRE_THAT(vHigh, WithinAbs(1.0f, 0.01f));
}

// ── constexpr ───────────────────────────────────────────

TEST_CASE("evaluateRangeSlider is constexpr evaluable", "[ui][rangeslider]")
{
	constexpr Rectf rect{0.0f, 0.0f, 100.0f, 20.0f};
	constexpr auto result = evaluateRangeSlider(
		rect, {50.0f, 10.0f}, false, false,
		0.2f, 0.8f, 0.0f, 1.0f);
	REQUIRE_FALSE(result.changed);
	REQUIRE(result.state == WidgetState::Hovered);
}
