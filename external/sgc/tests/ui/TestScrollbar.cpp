/// @file TestScrollbar.cpp
/// @brief スクロールバー評価ユーティリティのテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/ui/Scrollbar.hpp"

using namespace sgc;
using namespace sgc::ui;
using Catch::Matchers::WithinAbs;

static const Rectf TRACK{{10.0f, 10.0f}, {20.0f, 200.0f}};

// ── isScrollNeeded ─────────────────────────────────────

TEST_CASE("isScrollNeeded returns true when content exceeds viewport", "[ui][scrollbar]")
{
	REQUIRE(isScrollNeeded(500.0f, 200.0f));
}

TEST_CASE("isScrollNeeded returns false when content fits in viewport", "[ui][scrollbar]")
{
	REQUIRE_FALSE(isScrollNeeded(100.0f, 200.0f));
	REQUIRE_FALSE(isScrollNeeded(200.0f, 200.0f));
}

// ── maxScrollPos ───────────────────────────────────────

TEST_CASE("maxScrollPos returns positive when content exceeds viewport", "[ui][scrollbar]")
{
	REQUIRE_THAT(maxScrollPos(300.0f, 100.0f), WithinAbs(200.0f, 0.01f));
}

TEST_CASE("maxScrollPos returns zero when content fits in viewport", "[ui][scrollbar]")
{
	REQUIRE_THAT(maxScrollPos(100.0f, 200.0f), WithinAbs(0.0f, 0.01f));
	REQUIRE_THAT(maxScrollPos(200.0f, 200.0f), WithinAbs(0.0f, 0.01f));
}

// ── No scroll needed ──────────────────────────────────

TEST_CASE("evaluateScrollbar returns full-size thumb when no scroll needed", "[ui][scrollbar]")
{
	auto result = evaluateScrollbar(
		TRACK, {15.0f, 100.0f}, false, false,
		0.0f, 100.0f, 200.0f);
	REQUIRE(result.scrollPos == 0.0f);
	REQUIRE_THAT(result.thumbRect.height(), WithinAbs(TRACK.height(), 0.01f));
	REQUIRE_FALSE(result.changed);
	REQUIRE_FALSE(result.dragging);
}

// ── Disabled state ────────────────────────────────────

TEST_CASE("evaluateScrollbar returns Disabled when not enabled", "[ui][scrollbar]")
{
	auto result = evaluateScrollbar(
		TRACK, {15.0f, 100.0f}, true, true,
		50.0f, 400.0f, 200.0f, false, false);
	REQUIRE(result.state == WidgetState::Disabled);
	REQUIRE_FALSE(result.changed);
	REQUIRE_FALSE(result.dragging);
}

// ── Thumb size proportional ───────────────────────────

TEST_CASE("evaluateScrollbar thumb height is proportional to viewport/content", "[ui][scrollbar]")
{
	// content=400, viewport=200, trackH=200 => thumbH = 200*200/400 = 100
	auto result = evaluateScrollbar(
		TRACK, {0.0f, 0.0f}, false, false,
		0.0f, 400.0f, 200.0f);
	REQUIRE_THAT(result.thumbRect.height(), WithinAbs(100.0f, 0.01f));
}

TEST_CASE("evaluateScrollbar thumb height respects minThumbSize", "[ui][scrollbar]")
{
	// content=10000, viewport=200, trackH=200 => thumbH = 200*200/10000 = 4 < minThumb(20)
	auto result = evaluateScrollbar(
		TRACK, {0.0f, 0.0f}, false, false,
		0.0f, 10000.0f, 200.0f, false, true, 20.0f);
	REQUIRE_THAT(result.thumbRect.height(), WithinAbs(20.0f, 0.01f));
}

// ── Drag start ────────────────────────────────────────

TEST_CASE("evaluateScrollbar starts drag on mousePressed on thumb", "[ui][scrollbar]")
{
	// scrollPos=0 => thumb at top of track
	// content=400, viewport=200 => thumbH=100, thumb at y=10..110
	auto result = evaluateScrollbar(
		TRACK, {15.0f, 50.0f}, true, true,
		0.0f, 400.0f, 200.0f);
	REQUIRE(result.dragging);
	REQUIRE(result.state == WidgetState::Pressed);
}

// ── Drag continue ─────────────────────────────────────

TEST_CASE("evaluateScrollbar continues drag when wasDragging and mouseDown", "[ui][scrollbar]")
{
	auto result = evaluateScrollbar(
		TRACK, {15.0f, 150.0f}, true, false,
		50.0f, 400.0f, 200.0f, true);
	REQUIRE(result.dragging);
}

// ── Drag release ──────────────────────────────────────

TEST_CASE("evaluateScrollbar stops drag when mouse released", "[ui][scrollbar]")
{
	auto result = evaluateScrollbar(
		TRACK, {15.0f, 150.0f}, false, false,
		50.0f, 400.0f, 200.0f, true);
	REQUIRE_FALSE(result.dragging);
}

// ── Track click above thumb ───────────────────────────

TEST_CASE("evaluateScrollbar page up on track click above thumb", "[ui][scrollbar]")
{
	// scrollPos=150 with content=400, viewport=200, maxScroll=200
	// thumbH = 100, scrollableTrack=100, ratio=150/200=0.75, thumbY = 10+75=85
	// Click at y=20 (above thumb at 85) => page up: 150-200 = clamped to 0
	auto result = evaluateScrollbar(
		TRACK, {15.0f, 20.0f}, true, true,
		150.0f, 400.0f, 200.0f);
	REQUIRE(result.changed);
	REQUIRE(result.scrollPos < 150.0f);
}

// ── Track click below thumb ───────────────────────────

TEST_CASE("evaluateScrollbar page down on track click below thumb", "[ui][scrollbar]")
{
	// scrollPos=0 => thumbY=10, thumbBottom=110
	// Click at y=170 (below thumb) => page down: 0+200 = 200 (=maxScroll)
	auto result = evaluateScrollbar(
		TRACK, {15.0f, 170.0f}, true, true,
		0.0f, 400.0f, 200.0f);
	REQUIRE(result.changed);
	REQUIRE(result.scrollPos > 0.0f);
}

// ── Scroll position clamping ──────────────────────────

TEST_CASE("evaluateScrollbar clamps scroll position within valid range", "[ui][scrollbar]")
{
	// Provide scrollPos beyond maxScroll (200) => should clamp
	auto result = evaluateScrollbar(
		TRACK, {0.0f, 0.0f}, false, false,
		500.0f, 400.0f, 200.0f);
	REQUIRE_THAT(result.scrollPos, WithinAbs(200.0f, 0.01f));
}

// ── constexpr evaluability ────────────────────────────

TEST_CASE("evaluateScrollbar is constexpr evaluable", "[ui][scrollbar]")
{
	constexpr Rectf track{10.0f, 10.0f, 20.0f, 200.0f};
	constexpr auto result = evaluateScrollbar(
		track, {15.0f, 50.0f}, false, false,
		0.0f, 400.0f, 200.0f);
	REQUIRE_FALSE(result.dragging);
	REQUIRE_FALSE(result.changed);
}
