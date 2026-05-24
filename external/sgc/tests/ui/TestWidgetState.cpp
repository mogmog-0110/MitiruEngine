/// @file TestWidgetState.cpp
/// @brief WidgetState + HitTest utility tests

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/ui/WidgetState.hpp"

using namespace sgc;
using namespace sgc::ui;

// ── resolveWidgetState tests ─────────────────────────────

TEST_CASE("resolveWidgetState returns Normal when nothing active", "[ui][widget]")
{
	auto state = resolveWidgetState(true, false, false, false);
	REQUIRE(state == WidgetState::Normal);
}

TEST_CASE("resolveWidgetState returns Hovered when mouse over", "[ui][widget]")
{
	auto state = resolveWidgetState(true, true, false, false);
	REQUIRE(state == WidgetState::Hovered);
}

TEST_CASE("resolveWidgetState returns Pressed when mouse pressed", "[ui][widget]")
{
	auto state = resolveWidgetState(true, true, true, false);
	REQUIRE(state == WidgetState::Pressed);
}

TEST_CASE("resolveWidgetState returns Focused when focused", "[ui][widget]")
{
	auto state = resolveWidgetState(true, false, false, true);
	REQUIRE(state == WidgetState::Focused);
}

TEST_CASE("resolveWidgetState returns Disabled when not enabled", "[ui][widget]")
{
	auto state = resolveWidgetState(false, true, true, true);
	REQUIRE(state == WidgetState::Disabled);
}

TEST_CASE("resolveWidgetState Pressed takes priority over Hovered", "[ui][widget]")
{
	auto state = resolveWidgetState(true, true, true, false);
	REQUIRE(state == WidgetState::Pressed);
}

TEST_CASE("resolveWidgetState Pressed without hover still returns Pressed", "[ui][widget]")
{
	auto state = resolveWidgetState(true, false, true, false);
	REQUIRE(state == WidgetState::Pressed);
}

TEST_CASE("resolveWidgetState Hovered takes priority over Focused", "[ui][widget]")
{
	auto state = resolveWidgetState(true, true, false, true);
	REQUIRE(state == WidgetState::Hovered);
}

TEST_CASE("resolveWidgetState default isFocused parameter is false", "[ui][widget]")
{
	auto state = resolveWidgetState(true, false, false);
	REQUIRE(state == WidgetState::Normal);
}

// ── isMouseOver tests ────────────────────────────────────

TEST_CASE("isMouseOver returns true for point inside rect", "[ui][widget]")
{
	const Rectf rect{{100.0f, 100.0f}, {200.0f, 100.0f}};
	REQUIRE(isMouseOver({150.0f, 150.0f}, rect));
}

TEST_CASE("isMouseOver returns false for point outside rect", "[ui][widget]")
{
	const Rectf rect{{100.0f, 100.0f}, {200.0f, 100.0f}};
	REQUIRE_FALSE(isMouseOver({50.0f, 150.0f}, rect));
}

TEST_CASE("isMouseOver returns true on boundary", "[ui][widget]")
{
	const Rectf rect{{100.0f, 100.0f}, {200.0f, 100.0f}};
	REQUIRE(isMouseOver({100.0f, 100.0f}, rect));   // 左上角
	REQUIRE(isMouseOver({300.0f, 200.0f}, rect));   // 右下角
}

// ── isMouseOverCircle tests ──────────────────────────────

TEST_CASE("isMouseOverCircle returns true inside circle", "[ui][widget]")
{
	REQUIRE(isMouseOverCircle({100.0f, 100.0f}, {100.0f, 100.0f}, 50.0f));
	REQUIRE(isMouseOverCircle({120.0f, 100.0f}, {100.0f, 100.0f}, 50.0f));
}

TEST_CASE("isMouseOverCircle returns false outside circle", "[ui][widget]")
{
	REQUIRE_FALSE(isMouseOverCircle({200.0f, 200.0f}, {100.0f, 100.0f}, 50.0f));
}

TEST_CASE("isMouseOverCircle returns true on boundary", "[ui][widget]")
{
	REQUIRE(isMouseOverCircle({150.0f, 100.0f}, {100.0f, 100.0f}, 50.0f));
}

// ── isClicked tests ──────────────────────────────────────

TEST_CASE("isClicked returns true when hovering and button pressed", "[ui][widget]")
{
	const Rectf rect{{100.0f, 100.0f}, {200.0f, 100.0f}};
	REQUIRE(isClicked({150.0f, 150.0f}, rect, true));
}

TEST_CASE("isClicked returns false when outside rect even if button pressed", "[ui][widget]")
{
	const Rectf rect{{100.0f, 100.0f}, {200.0f, 100.0f}};
	REQUIRE_FALSE(isClicked({50.0f, 50.0f}, rect, true));
}

TEST_CASE("isClicked returns false when hovering but button not pressed", "[ui][widget]")
{
	const Rectf rect{{100.0f, 100.0f}, {200.0f, 100.0f}};
	REQUIRE_FALSE(isClicked({150.0f, 150.0f}, rect, false));
}

// ── constexpr validation ─────────────────────────────────

TEST_CASE("WidgetState functions are constexpr", "[ui][widget]")
{
	constexpr auto state = resolveWidgetState(true, true, false);
	static_assert(state == WidgetState::Hovered);

	constexpr Rectf rect{{0.0f, 0.0f}, {100.0f, 100.0f}};
	constexpr bool hit = isMouseOver({50.0f, 50.0f}, rect);
	static_assert(hit);

	constexpr bool clicked = isClicked({50.0f, 50.0f}, rect, true);
	static_assert(clicked);

	REQUIRE(true);
}
