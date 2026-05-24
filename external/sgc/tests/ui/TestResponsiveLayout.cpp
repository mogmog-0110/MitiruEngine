#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/ui/ResponsiveLayout.hpp"

using namespace sgc;
using namespace sgc::ui;

static constexpr auto approx = [](float expected) {
	return Catch::Matchers::WithinAbs(expected, 0.1f);
};

// ── ResponsiveBreakpoints ──────────────────────────────

TEST_CASE("ResponsiveBreakpoints - empty returns default", "[ui][responsive]")
{
	ResponsiveBreakpoints bp;
	auto params = bp.resolve(1280.0f);

	REQUIRE(params.columns == 1);
	REQUIRE(bp.count() == 0);
}

TEST_CASE("ResponsiveBreakpoints - single breakpoint", "[ui][responsive]")
{
	ResponsiveBreakpoints bp;
	bp.add(0.0f, LayoutParams{2, 10.0f, 20.0f, 18.0f});

	auto params = bp.resolve(640.0f);
	REQUIRE(params.columns == 2);
	REQUIRE_THAT(params.gap, approx(10.0f));
	REQUIRE_THAT(params.padding, approx(20.0f));
	REQUIRE_THAT(params.fontSize, approx(18.0f));
}

TEST_CASE("ResponsiveBreakpoints - selects correct breakpoint", "[ui][responsive]")
{
	ResponsiveBreakpoints bp;
	bp.add(0.0f,    LayoutParams{1, 4.0f, 8.0f});
	bp.add(800.0f,  LayoutParams{2, 8.0f, 12.0f});
	bp.add(1280.0f, LayoutParams{3, 12.0f, 16.0f});

	// Small screen
	auto small = bp.resolve(640.0f);
	REQUIRE(small.columns == 1);

	// Medium screen
	auto medium = bp.resolve(1024.0f);
	REQUIRE(medium.columns == 2);

	// Large screen
	auto large = bp.resolve(1920.0f);
	REQUIRE(large.columns == 3);

	// Exact boundary
	auto boundary = bp.resolve(800.0f);
	REQUIRE(boundary.columns == 2);
}

TEST_CASE("ResponsiveBreakpoints - add order doesn't matter", "[ui][responsive]")
{
	ResponsiveBreakpoints bp;
	bp.add(1280.0f, LayoutParams{3, 12.0f, 16.0f});
	bp.add(0.0f,    LayoutParams{1, 4.0f, 8.0f});
	bp.add(800.0f,  LayoutParams{2, 8.0f, 12.0f});

	auto result = bp.resolve(1024.0f);
	REQUIRE(result.columns == 2);
}

TEST_CASE("ResponsiveBreakpoints - clear removes all", "[ui][responsive]")
{
	ResponsiveBreakpoints bp;
	bp.add(0.0f, LayoutParams{2});
	bp.clear();
	REQUIRE(bp.count() == 0);
}

// ── safeRect ────────────────────────────────────────────

TEST_CASE("safeRect - no margins returns full screen", "[ui][responsive]")
{
	auto rect = safeRect(1280.0f, 720.0f);
	REQUIRE_THAT(rect.x(), approx(0.0f));
	REQUIRE_THAT(rect.y(), approx(0.0f));
	REQUIRE_THAT(rect.width(), approx(1280.0f));
	REQUIRE_THAT(rect.height(), approx(720.0f));
}

TEST_CASE("safeRect - with margins", "[ui][responsive]")
{
	auto rect = safeRect(1280.0f, 720.0f, Margin{40, 20, 20, 20});
	REQUIRE_THAT(rect.x(), approx(20.0f));     // left
	REQUIRE_THAT(rect.y(), approx(40.0f));      // top
	REQUIRE_THAT(rect.width(), approx(1240.0f));  // 1280 - 20 - 20
	REQUIRE_THAT(rect.height(), approx(660.0f));  // 720 - 40 - 20
}

// ── gridLayout ──────────────────────────────────────────

TEST_CASE("gridLayout - single column", "[ui][responsive]")
{
	auto cells = gridLayout(Rectf{0, 0, 200, 300}, 3, 1, 10.0f);
	REQUIRE(cells.size() == 3);
	REQUIRE_THAT(cells[0].width(), approx(200.0f));
	// Height: (300 - 2*10) / 3 = 93.33
	REQUIRE_THAT(cells[0].height(), approx(93.3f));
	REQUIRE_THAT(cells[1].y(), approx(103.3f)); // 93.3 + 10
}

TEST_CASE("gridLayout - 2 columns with gap", "[ui][responsive]")
{
	auto cells = gridLayout(Rectf{10, 20, 400, 300}, 4, 2, 10.0f);
	REQUIRE(cells.size() == 4);

	// Width: (400 - 10) / 2 = 195
	REQUIRE_THAT(cells[0].width(), approx(195.0f));
	REQUIRE_THAT(cells[0].x(), approx(10.0f));
	REQUIRE_THAT(cells[1].x(), approx(215.0f)); // 10 + 195 + 10

	// Second row
	REQUIRE_THAT(cells[2].y(), approx(cells[0].y() + cells[0].height() + 10.0f));
}

TEST_CASE("gridLayout - empty returns empty", "[ui][responsive]")
{
	auto cells = gridLayout(Rectf{0, 0, 400, 300}, 0, 3);
	REQUIRE(cells.empty());

	cells = gridLayout(Rectf{0, 0, 400, 300}, 5, 0);
	REQUIRE(cells.empty());
}

TEST_CASE("gridLayout - items not divisible by columns", "[ui][responsive]")
{
	auto cells = gridLayout(Rectf{0, 0, 300, 200}, 5, 3);
	REQUIRE(cells.size() == 5);
	// 2 rows (ceil(5/3) = 2)
	REQUIRE_THAT(cells[3].y(), approx(cells[1].y() + cells[1].height()));
}

// ── percentOf / percentRect ─────────────────────────────

TEST_CASE("percentOf - basic calculation", "[ui][responsive]")
{
	REQUIRE_THAT(percentOf(1280.0f, 50.0f), approx(640.0f));
	REQUIRE_THAT(percentOf(720.0f, 25.0f), approx(180.0f));
	REQUIRE_THAT(percentOf(100.0f, 100.0f), approx(100.0f));
}

TEST_CASE("percentRect - positions correctly", "[ui][responsive]")
{
	auto parent = Rectf{100, 50, 800, 600};
	auto child = percentRect(parent, 10, 20, 50, 30);

	REQUIRE_THAT(child.x(), approx(180.0f));     // 100 + 800*0.1
	REQUIRE_THAT(child.y(), approx(170.0f));      // 50 + 600*0.2
	REQUIRE_THAT(child.width(), approx(400.0f));  // 800*0.5
	REQUIRE_THAT(child.height(), approx(180.0f)); // 600*0.3
}
