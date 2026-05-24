/// @file TestTable.cpp
/// @brief Table evaluation utility tests

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/ui/Table.hpp"

using namespace sgc;
using namespace sgc::ui;
using Catch::Matchers::WithinAbs;

static const Rectf TABLE_RECT{{50.0f, 50.0f}, {300.0f, 200.0f}};
static const float COL_WIDTHS[] = {100.0f, 120.0f, 80.0f};

static TableConfig makeConfig(int32_t rows = 5, int32_t selectedRow = -1,
                              int32_t sortCol = -1, float scroll = 0.0f)
{
	return {TABLE_RECT, 3, rows, COL_WIDTHS, 30.0f, 25.0f, selectedRow, sortCol, true, scroll};
}

TEST_CASE("evaluateTable returns Normal when mouse outside", "[ui][table]")
{
	auto config = makeConfig();
	auto result = evaluateTable(config, {0.0f, 0.0f}, false, false, 0.0f);
	REQUIRE(result.state == WidgetState::Normal);
	REQUIRE(result.hoveredRow == -1);
	REQUIRE(result.clickedRow == -1);
}

TEST_CASE("evaluateTable detects hovered row", "[ui][table]")
{
	auto config = makeConfig();
	// Body starts at y=80 (50+30), first row at y=80..105
	auto result = evaluateTable(config, {100.0f, 90.0f}, false, false, 0.0f);
	REQUIRE(result.hoveredRow == 0);
}

TEST_CASE("evaluateTable detects clicked row", "[ui][table]")
{
	auto config = makeConfig();
	// Second row: y = 80 + 25 = 105..130, click at y=110
	auto result = evaluateTable(config, {100.0f, 110.0f}, true, true, 0.0f);
	REQUIRE(result.clickedRow == 1);
}

TEST_CASE("evaluateTable toggles sort column on header click", "[ui][table]")
{
	auto config = makeConfig();
	// Header area: y=50..80, first column x=50..150
	auto result = evaluateTable(config, {100.0f, 65.0f}, true, true, 0.0f);
	REQUIRE(result.sortColumn == 0);
	REQUIRE(result.sortAscending);
}

TEST_CASE("evaluateTable toggles sort direction on same column click", "[ui][table]")
{
	auto config = makeConfig(5, -1, 0);
	config.sortAscending = true;
	auto result = evaluateTable(config, {100.0f, 65.0f}, true, true, 0.0f);
	REQUIRE(result.sortColumn == 0);
	REQUIRE_FALSE(result.sortAscending);
}

TEST_CASE("evaluateTable scrolls content", "[ui][table]")
{
	auto config = makeConfig(20);
	auto result = evaluateTable(config, {100.0f, 150.0f}, false, false, -10.0f);
	REQUIRE_THAT(result.scrollOffset, WithinAbs(10.0f, 0.01f));
}

TEST_CASE("evaluateTable clamps scroll to zero", "[ui][table]")
{
	auto config = makeConfig(5);
	auto result = evaluateTable(config, {100.0f, 150.0f}, false, false, 100.0f);
	REQUIRE_THAT(result.scrollOffset, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("evaluateTable calculates cell bounds", "[ui][table]")
{
	auto config = makeConfig();
	auto result = evaluateTable(config, {0.0f, 0.0f}, false, false, 0.0f);
	auto cell = tableCellBounds(config, result, 1, 1);
	// col 1 starts at x = 50 + 100 = 150
	// row 1 starts at y = 80 + 25 = 105
	REQUIRE_THAT(cell.x(), WithinAbs(150.0f, 0.01f));
	REQUIRE_THAT(cell.y(), WithinAbs(105.0f, 0.01f));
	REQUIRE_THAT(cell.width(), WithinAbs(120.0f, 0.01f));
	REQUIRE_THAT(cell.height(), WithinAbs(25.0f, 0.01f));
}
