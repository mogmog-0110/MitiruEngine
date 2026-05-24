/// @file TestTheme.cpp
/// @brief UI Theme system tests

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/ui/Theme.hpp"

using namespace sgc;
using namespace sgc::ui;
using Catch::Approx;

// ── dark preset ─────────────────────────────────────────

TEST_CASE("Theme dark preset has valid background", "[ui][theme]")
{
	constexpr auto theme = Theme::dark();

	REQUIRE(theme.background.r == Approx(0.1f));
	REQUIRE(theme.background.a == Approx(1.0f));
}

TEST_CASE("Theme dark preset has readable text contrast", "[ui][theme]")
{
	constexpr auto theme = Theme::dark();

	// テキストは背景より十分明るいこと
	REQUIRE(theme.text.r > theme.background.r + 0.5f);
}

// ── light preset ────────────────────────────────────────

TEST_CASE("Theme light preset has valid background", "[ui][theme]")
{
	constexpr auto theme = Theme::light();

	REQUIRE(theme.background.r == Approx(0.95f));
	REQUIRE(theme.background.a == Approx(1.0f));
}

TEST_CASE("Theme light preset has readable text contrast", "[ui][theme]")
{
	constexpr auto theme = Theme::light();

	// テキストは背景より十分暗いこと
	REQUIRE(theme.text.r < theme.background.r - 0.5f);
}

// ── ButtonTheme colorFor ────────────────────────────────

TEST_CASE("ButtonTheme colorFor Normal returns normal color", "[ui][theme]")
{
	constexpr auto theme = Theme::dark();
	constexpr auto color = theme.button.colorFor(WidgetState::Normal);

	REQUIRE(color == theme.button.normal);
}

TEST_CASE("ButtonTheme colorFor Hovered returns hovered color", "[ui][theme]")
{
	constexpr auto theme = Theme::dark();
	constexpr auto color = theme.button.colorFor(WidgetState::Hovered);

	REQUIRE(color == theme.button.hovered);
}

TEST_CASE("ButtonTheme colorFor Pressed returns pressed color", "[ui][theme]")
{
	constexpr auto theme = Theme::dark();
	constexpr auto color = theme.button.colorFor(WidgetState::Pressed);

	REQUIRE(color == theme.button.pressed);
}

TEST_CASE("ButtonTheme colorFor Disabled returns disabled color", "[ui][theme]")
{
	constexpr auto theme = Theme::dark();
	constexpr auto color = theme.button.colorFor(WidgetState::Disabled);

	REQUIRE(color == theme.button.disabled);
}

// ── font sizes ──────────────────────────────────────────

TEST_CASE("Theme font sizes are positive and ordered", "[ui][theme]")
{
	constexpr auto theme = Theme::dark();

	REQUIRE(theme.fontSizeSmall > 0.0f);
	REQUIRE(theme.fontSizeBody > theme.fontSizeSmall);
	REQUIRE(theme.fontSizeTitle > theme.fontSizeBody);
}

// ── constexpr verification ──────────────────────────────

TEST_CASE("Theme is fully constexpr constructible", "[ui][theme]")
{
	constexpr auto dark = Theme::dark();
	constexpr auto light = Theme::light();
	constexpr auto hoveredColor = dark.button.colorFor(WidgetState::Hovered);

	// これらがコンパイル時に評価されることを確認
	static_assert(dark.fontSizeTitle == 48.0f);
	static_assert(light.fontSizeTitle == 48.0f);
	(void)hoveredColor;

	REQUIRE(true);  // コンパイルが通ればOK
}
