/// @file TestBreadcrumb.cpp
/// @brief Breadcrumb evaluation utility tests

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/ui/Breadcrumb.hpp"

using namespace sgc;
using namespace sgc::ui;
using Catch::Matchers::WithinAbs;

static const Rectf BREADCRUMB_RECT{100.0f, 200.0f, 300.0f, 30.0f};

// ── Disabled state ──────────────────────────────────────

TEST_CASE("evaluateBreadcrumb returns Disabled when not enabled", "[ui][breadcrumb]")
{
	auto result = evaluateBreadcrumb(
		BREADCRUMB_RECT, 3,
		{150.0f, 215.0f}, true, true, false);
	REQUIRE(result.state == WidgetState::Disabled);
	REQUIRE(result.clickedIndex == -1);
}

// ── Normal state ────────────────────────────────────────

TEST_CASE("evaluateBreadcrumb returns Normal when mouse outside", "[ui][breadcrumb]")
{
	auto result = evaluateBreadcrumb(
		BREADCRUMB_RECT, 3,
		{0.0f, 0.0f}, false, false);
	REQUIRE(result.state == WidgetState::Normal);
	REQUIRE(result.clickedIndex == -1);
	REQUIRE(result.hoveredIndex == -1);
}

// ── Click detection ─────────────────────────────────────

TEST_CASE("evaluateBreadcrumb detects click on first item", "[ui][breadcrumb]")
{
	// 3 items, each 100px wide: [100,200), [200,300), [300,400)
	auto result = evaluateBreadcrumb(
		BREADCRUMB_RECT, 3,
		{150.0f, 215.0f}, true, true);
	REQUIRE(result.clickedIndex == 0);
	REQUIRE(result.hoveredIndex == 0);
}

TEST_CASE("evaluateBreadcrumb detects click on middle item", "[ui][breadcrumb]")
{
	auto result = evaluateBreadcrumb(
		BREADCRUMB_RECT, 3,
		{250.0f, 215.0f}, true, true);
	REQUIRE(result.clickedIndex == 1);
}

TEST_CASE("evaluateBreadcrumb detects click on last item", "[ui][breadcrumb]")
{
	auto result = evaluateBreadcrumb(
		BREADCRUMB_RECT, 3,
		{350.0f, 215.0f}, true, true);
	REQUIRE(result.clickedIndex == 2);
}

// ── Hover detection ─────────────────────────────────────

TEST_CASE("evaluateBreadcrumb detects hover without click", "[ui][breadcrumb]")
{
	auto result = evaluateBreadcrumb(
		BREADCRUMB_RECT, 3,
		{250.0f, 215.0f}, false, false);
	REQUIRE(result.hoveredIndex == 1);
	REQUIRE(result.clickedIndex == -1);
	REQUIRE(result.state == WidgetState::Hovered);
}

// ── Edge cases ──────────────────────────────────────────

TEST_CASE("evaluateBreadcrumb handles zero items", "[ui][breadcrumb]")
{
	auto result = evaluateBreadcrumb(
		BREADCRUMB_RECT, 0,
		{150.0f, 215.0f}, true, true);
	REQUIRE(result.clickedIndex == -1);
	REQUIRE(result.state == WidgetState::Normal);
}

// ── Helper ──────────────────────────────────────────────

TEST_CASE("breadcrumbItemRect calculates correct rects", "[ui][breadcrumb]")
{
	// 3 items in 300px wide rect starting at x=100
	const auto r0 = breadcrumbItemRect(BREADCRUMB_RECT, 3, 0);
	REQUIRE_THAT(r0.x(), WithinAbs(100.0f, 0.01f));
	REQUIRE_THAT(r0.width(), WithinAbs(100.0f, 0.01f));

	const auto r1 = breadcrumbItemRect(BREADCRUMB_RECT, 3, 1);
	REQUIRE_THAT(r1.x(), WithinAbs(200.0f, 0.01f));

	const auto r2 = breadcrumbItemRect(BREADCRUMB_RECT, 3, 2);
	REQUIRE_THAT(r2.x(), WithinAbs(300.0f, 0.01f));
}

// ── constexpr ───────────────────────────────────────────

TEST_CASE("evaluateBreadcrumb is constexpr evaluable", "[ui][breadcrumb]")
{
	constexpr Rectf rect{0.0f, 0.0f, 300.0f, 30.0f};
	constexpr auto result = evaluateBreadcrumb(
		rect, 3,
		{50.0f, 15.0f}, true, true);
	REQUIRE(result.clickedIndex == 0);
}
