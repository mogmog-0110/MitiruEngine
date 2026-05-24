/// @file TestPanel.cpp
/// @brief パネルレイアウト評価ユーティリティのテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/ui/Panel.hpp"

using namespace sgc;
using namespace sgc::ui;

static const Rectf PANEL_RECT{{10.0f, 20.0f}, {300.0f, 200.0f}};

// ── Title bar ───────────────────────────────────────────

TEST_CASE("evaluatePanel title bar with height 32", "[ui][panel]")
{
	auto result = evaluatePanel(PANEL_RECT, 32.0f, Padding::uniform(0.0f));
	REQUIRE_THAT(result.titleBounds.x(), Catch::Matchers::WithinAbs(10.0, 0.001));
	REQUIRE_THAT(result.titleBounds.y(), Catch::Matchers::WithinAbs(20.0, 0.001));
	REQUIRE_THAT(result.titleBounds.width(), Catch::Matchers::WithinAbs(300.0, 0.001));
	REQUIRE_THAT(result.titleBounds.height(), Catch::Matchers::WithinAbs(32.0, 0.001));
}

TEST_CASE("evaluatePanel title bar with height 0", "[ui][panel]")
{
	auto result = evaluatePanel(PANEL_RECT, 0.0f, Padding::uniform(0.0f));
	REQUIRE_THAT(result.titleBounds.height(), Catch::Matchers::WithinAbs(0.0, 0.001));
}

// ── Content bounds with padding ─────────────────────────

TEST_CASE("evaluatePanel content bounds with uniform padding 8", "[ui][panel]")
{
	auto result = evaluatePanel(PANEL_RECT, 32.0f, Padding::uniform(8.0f));
	// content x = 10 + 8 = 18
	// content y = 20 + 32 + 8 = 60
	// content w = 300 - 8 - 8 = 284
	// content h = 200 - 32 - 8 - 8 = 152
	REQUIRE_THAT(result.contentBounds.x(), Catch::Matchers::WithinAbs(18.0, 0.001));
	REQUIRE_THAT(result.contentBounds.y(), Catch::Matchers::WithinAbs(60.0, 0.001));
	REQUIRE_THAT(result.contentBounds.width(), Catch::Matchers::WithinAbs(284.0, 0.001));
	REQUIRE_THAT(result.contentBounds.height(), Catch::Matchers::WithinAbs(152.0, 0.001));
}

TEST_CASE("evaluatePanel content bounds with zero padding", "[ui][panel]")
{
	auto result = evaluatePanel(PANEL_RECT, 32.0f, Padding::uniform(0.0f));
	// content x = 10, y = 20 + 32 = 52, w = 300, h = 200 - 32 = 168
	REQUIRE_THAT(result.contentBounds.x(), Catch::Matchers::WithinAbs(10.0, 0.001));
	REQUIRE_THAT(result.contentBounds.y(), Catch::Matchers::WithinAbs(52.0, 0.001));
	REQUIRE_THAT(result.contentBounds.width(), Catch::Matchers::WithinAbs(300.0, 0.001));
	REQUIRE_THAT(result.contentBounds.height(), Catch::Matchers::WithinAbs(168.0, 0.001));
}

// ── Content clamped to 0 ────────────────────────────────

TEST_CASE("evaluatePanel content clamped to 0 when padding exceeds panel size", "[ui][panel]")
{
	const Rectf smallPanel{{0.0f, 0.0f}, {20.0f, 20.0f}};
	auto result = evaluatePanel(smallPanel, 10.0f, Padding::uniform(50.0f));
	REQUIRE_THAT(result.contentBounds.width(), Catch::Matchers::WithinAbs(0.0, 0.001));
	REQUIRE_THAT(result.contentBounds.height(), Catch::Matchers::WithinAbs(0.0, 0.001));
}

// ── panelWithBorder ─────────────────────────────────────

TEST_CASE("panelWithBorder with normal border width", "[ui][panel]")
{
	auto inner = panelWithBorder(PANEL_RECT, 4.0f);
	// inner x = 10 + 4 = 14, y = 20 + 4 = 24
	// inner w = 300 - 8 = 292, h = 200 - 8 = 192
	REQUIRE_THAT(inner.x(), Catch::Matchers::WithinAbs(14.0, 0.001));
	REQUIRE_THAT(inner.y(), Catch::Matchers::WithinAbs(24.0, 0.001));
	REQUIRE_THAT(inner.width(), Catch::Matchers::WithinAbs(292.0, 0.001));
	REQUIRE_THAT(inner.height(), Catch::Matchers::WithinAbs(192.0, 0.001));
}

TEST_CASE("panelWithBorder clamped to zero when border exceeds size", "[ui][panel]")
{
	const Rectf smallRect{{0.0f, 0.0f}, {10.0f, 6.0f}};
	auto inner = panelWithBorder(smallRect, 20.0f);
	REQUIRE_THAT(inner.width(), Catch::Matchers::WithinAbs(0.0, 0.001));
	REQUIRE_THAT(inner.height(), Catch::Matchers::WithinAbs(0.0, 0.001));
}

// ── constexpr ───────────────────────────────────────────

TEST_CASE("evaluatePanel is constexpr evaluable", "[ui][panel]")
{
	constexpr Rectf rect{{0.0f, 0.0f}, {200.0f, 100.0f}};
	constexpr auto result = evaluatePanel(rect, 24.0f, Padding::uniform(4.0f));
	REQUIRE_THAT(result.contentBounds.width(), Catch::Matchers::WithinAbs(192.0, 0.001));
	REQUIRE_THAT(result.contentBounds.height(), Catch::Matchers::WithinAbs(68.0, 0.001));
}
