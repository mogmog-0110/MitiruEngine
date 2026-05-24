/// @file TestTooltip.cpp
/// @brief ツールチップ配置ユーティリティのテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/ui/Tooltip.hpp"

using namespace sgc;
using namespace sgc::ui;

static const Rectf SCREEN{{0.0f, 0.0f}, {800.0f, 600.0f}};
static const Rectf WIDGET{{300.0f, 200.0f}, {120.0f, 30.0f}};
static const Vec2f TIP_SIZE{150.0f, 40.0f};

// ── Above ──────────────────────────────────────────────

TEST_CASE("evaluateTooltip Above fits places tooltip above widget", "[ui][tooltip]")
{
	const auto result = evaluateTooltip(WIDGET, SCREEN, TIP_SIZE, TooltipSide::Above);
	REQUIRE(result.actualSide == TooltipSide::Above);
	// y = widget.y - gap - tipH = 200 - 4 - 40 = 156
	REQUIRE(result.bounds.y() == 156.0f);
	// x = widget centerX - tipW/2 = (300 + 60) - 75 = 285
	REQUIRE(result.bounds.x() == 285.0f);
	REQUIRE(result.bounds.width() == 150.0f);
	REQUIRE(result.bounds.height() == 40.0f);
}

TEST_CASE("evaluateTooltip Above flips to Below when near top edge", "[ui][tooltip]")
{
	// ウィジェットが画面上端付近
	const Rectf topWidget{{300.0f, 10.0f}, {120.0f, 30.0f}};
	const auto result = evaluateTooltip(topWidget, SCREEN, TIP_SIZE, TooltipSide::Above);
	// y = 10 - 4 - 40 = -34 → はみ出すのでBelow にフリップ
	REQUIRE(result.actualSide == TooltipSide::Below);
	// y = widget.bottom + gap = 40 + 4 = 44
	REQUIRE(result.bounds.y() == 44.0f);
}

// ── Below ──────────────────────────────────────────────

TEST_CASE("evaluateTooltip Below fits places tooltip below widget", "[ui][tooltip]")
{
	const auto result = evaluateTooltip(WIDGET, SCREEN, TIP_SIZE, TooltipSide::Below);
	REQUIRE(result.actualSide == TooltipSide::Below);
	// y = widget.bottom + gap = 230 + 4 = 234
	REQUIRE(result.bounds.y() == 234.0f);
}

// ── Left ───────────────────────────────────────────────

TEST_CASE("evaluateTooltip Left fits places tooltip left of widget", "[ui][tooltip]")
{
	const auto result = evaluateTooltip(WIDGET, SCREEN, TIP_SIZE, TooltipSide::Left);
	REQUIRE(result.actualSide == TooltipSide::Left);
	// x = widget.x - gap - tipW = 300 - 4 - 150 = 146
	REQUIRE(result.bounds.x() == 146.0f);
	// y = widget centerY - tipH/2 = (200 + 15) - 20 = 195
	REQUIRE(result.bounds.y() == 195.0f);
}

// ── Right ──────────────────────────────────────────────

TEST_CASE("evaluateTooltip Right fits places tooltip right of widget", "[ui][tooltip]")
{
	const auto result = evaluateTooltip(WIDGET, SCREEN, TIP_SIZE, TooltipSide::Right);
	REQUIRE(result.actualSide == TooltipSide::Right);
	// x = widget.right + gap = 420 + 4 = 424
	REQUIRE(result.bounds.x() == 424.0f);
}

TEST_CASE("evaluateTooltip Right flips to Left when near right edge", "[ui][tooltip]")
{
	// ウィジェットが画面右端付近
	const Rectf rightWidget{{700.0f, 200.0f}, {80.0f, 30.0f}};
	const auto result = evaluateTooltip(rightWidget, SCREEN, TIP_SIZE, TooltipSide::Right);
	// x = 780 + 4 = 784 → 784 + 150 = 934 > 800 → はみ出すのでLeftにフリップ
	REQUIRE(result.actualSide == TooltipSide::Left);
	// x = 700 - 4 - 150 = 546
	REQUIRE(result.bounds.x() == 546.0f);
}

// ── Neither side fits ──────────────────────────────────

TEST_CASE("evaluateTooltip stays preferred and clamps when neither side fits", "[ui][tooltip]")
{
	// 非常に小さい画面で両方はみ出す
	const Rectf smallScreen{{0.0f, 0.0f}, {200.0f, 100.0f}};
	const Rectf widget{{50.0f, 30.0f}, {100.0f, 40.0f}};
	const Vec2f bigTip{180.0f, 50.0f};
	const auto result = evaluateTooltip(widget, smallScreen, bigTip, TooltipSide::Above);
	// 両方はみ出す → preferredSide (Above) を維持、クランプ
	REQUIRE(result.actualSide == TooltipSide::Above);
	// クランプにより画面内に収まる
	REQUIRE(result.bounds.x() >= smallScreen.x());
	REQUIRE(result.bounds.y() >= smallScreen.y());
	REQUIRE(result.bounds.x() + bigTip.x <= smallScreen.right());
	REQUIRE(result.bounds.y() + bigTip.y <= smallScreen.bottom());
}

// ── Horizontal centering ───────────────────────────────

TEST_CASE("evaluateTooltip centers tooltip horizontally on widget", "[ui][tooltip]")
{
	const auto result = evaluateTooltip(WIDGET, SCREEN, TIP_SIZE, TooltipSide::Above);
	// widget center x = 300 + 60 = 360、tooltip x = 360 - 75 = 285
	const float expectedX = WIDGET.x() + WIDGET.width() * 0.5f - TIP_SIZE.x * 0.5f;
	REQUIRE(result.bounds.x() == expectedX);
}

// ── Screen edge clamping ───────────────────────────────

TEST_CASE("evaluateTooltip clamps tooltip within screen bounds", "[ui][tooltip]")
{
	// ウィジェットが画面左端付近 → 水平中央揃えではみ出す
	const Rectf leftWidget{{5.0f, 200.0f}, {30.0f, 30.0f}};
	const auto result = evaluateTooltip(leftWidget, SCREEN, TIP_SIZE, TooltipSide::Above);
	// x = (5 + 15) - 75 = -55 → クランプで0に
	REQUIRE(result.bounds.x() >= SCREEN.x());
	REQUIRE(result.bounds.y() >= SCREEN.y());
}

// ── constexpr ──────────────────────────────────────────

TEST_CASE("evaluateTooltip is constexpr evaluable", "[ui][tooltip]")
{
	constexpr Rectf screen{{0.0f, 0.0f}, {800.0f, 600.0f}};
	constexpr Rectf widget{{100.0f, 100.0f}, {80.0f, 30.0f}};
	constexpr auto result = evaluateTooltip(widget, screen, {100.0f, 30.0f}, TooltipSide::Below);
	REQUIRE(result.actualSide == TooltipSide::Below);
	REQUIRE(result.bounds.y() == 134.0f);
}
