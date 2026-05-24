/// @file TestProgressBar.cpp
/// @brief ProgressBar evaluation utility tests

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/ui/ProgressBar.hpp"

using namespace sgc;
using namespace sgc::ui;
using Catch::Matchers::WithinAbs;

static const Rectf BAR_RECT{{10.0f, 10.0f}, {200.0f, 20.0f}};

// ── Basic percentages ───────────────────────────────────

TEST_CASE("evaluateProgressBar returns 0% for minimum value", "[ui][progressbar]")
{
	auto result = evaluateProgressBar(BAR_RECT, 0.0f, 0.0f, 100.0f);
	REQUIRE_THAT(result.ratio, WithinAbs(0.0f, 0.001f));
	REQUIRE_THAT(result.filledWidth, WithinAbs(0.0f, 0.01f));
	REQUIRE_THAT(result.filledRect.width(), WithinAbs(0.0f, 0.01f));
}

TEST_CASE("evaluateProgressBar returns 50% for midpoint value", "[ui][progressbar]")
{
	auto result = evaluateProgressBar(BAR_RECT, 50.0f, 0.0f, 100.0f);
	REQUIRE_THAT(result.ratio, WithinAbs(0.5f, 0.001f));
	REQUIRE_THAT(result.filledWidth, WithinAbs(100.0f, 0.01f));
	REQUIRE_THAT(result.filledRect.width(), WithinAbs(100.0f, 0.01f));
	REQUIRE_THAT(result.filledRect.height(), WithinAbs(20.0f, 0.01f));
}

TEST_CASE("evaluateProgressBar returns 100% for maximum value", "[ui][progressbar]")
{
	auto result = evaluateProgressBar(BAR_RECT, 100.0f, 0.0f, 100.0f);
	REQUIRE_THAT(result.ratio, WithinAbs(1.0f, 0.001f));
	REQUIRE_THAT(result.filledWidth, WithinAbs(200.0f, 0.01f));
	REQUIRE_THAT(result.filledRect.width(), WithinAbs(200.0f, 0.01f));
}

// ── Filled rect position ────────────────────────────────

TEST_CASE("evaluateProgressBar filledRect starts at bar position", "[ui][progressbar]")
{
	auto result = evaluateProgressBar(BAR_RECT, 50.0f, 0.0f, 100.0f);
	REQUIRE_THAT(result.filledRect.x(), WithinAbs(10.0f, 0.01f));
	REQUIRE_THAT(result.filledRect.y(), WithinAbs(10.0f, 0.01f));
}

// ── Clamping ────────────────────────────────────────────

TEST_CASE("evaluateProgressBar clamps value below minimum", "[ui][progressbar]")
{
	auto result = evaluateProgressBar(BAR_RECT, -20.0f, 0.0f, 100.0f);
	REQUIRE_THAT(result.ratio, WithinAbs(0.0f, 0.001f));
	REQUIRE_THAT(result.filledWidth, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("evaluateProgressBar clamps value above maximum", "[ui][progressbar]")
{
	auto result = evaluateProgressBar(BAR_RECT, 150.0f, 0.0f, 100.0f);
	REQUIRE_THAT(result.ratio, WithinAbs(1.0f, 0.001f));
	REQUIRE_THAT(result.filledWidth, WithinAbs(200.0f, 0.01f));
}

// ── Zero range ──────────────────────────────────────────

TEST_CASE("evaluateProgressBar handles zero range", "[ui][progressbar]")
{
	auto result = evaluateProgressBar(BAR_RECT, 50.0f, 50.0f, 50.0f);
	REQUIRE_THAT(result.ratio, WithinAbs(0.0f, 0.001f));
	REQUIRE_THAT(result.filledWidth, WithinAbs(0.0f, 0.01f));
}

// ── Custom range ────────────────────────────────────────

TEST_CASE("evaluateProgressBar works with non-zero minimum", "[ui][progressbar]")
{
	// value=75, range=[50,100] => ratio=0.5
	auto result = evaluateProgressBar(BAR_RECT, 75.0f, 50.0f, 100.0f);
	REQUIRE_THAT(result.ratio, WithinAbs(0.5f, 0.001f));
	REQUIRE_THAT(result.filledWidth, WithinAbs(100.0f, 0.01f));
}

TEST_CASE("evaluateProgressBar is constexpr evaluable", "[ui][progressbar]")
{
	constexpr Rectf rect{{0.0f, 0.0f}, {100.0f, 10.0f}};
	constexpr auto result = evaluateProgressBar(rect, 50.0f, 0.0f, 100.0f);
	REQUIRE_THAT(result.ratio, WithinAbs(0.5f, 0.001f));
}
