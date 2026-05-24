/// @file TestComboBox.cpp
/// @brief ComboBox evaluation utility tests

#include <catch2/catch_test_macros.hpp>

#include "sgc/ui/ComboBox.hpp"

using namespace sgc;
using namespace sgc::ui;

static const Rectf COMBO_RECT{{100.0f, 200.0f}, {200.0f, 30.0f}};

TEST_CASE("evaluateComboBox returns Normal when mouse outside", "[ui][combobox]")
{
	ComboBoxConfig config{COMBO_RECT, 0, false, 30.0f, 5};
	auto result = evaluateComboBox(config, {0.0f, 0.0f}, false, false);
	REQUIRE(result.state == WidgetState::Normal);
	REQUIRE(result.selectedIndex == -1);
	REQUIRE_FALSE(result.isOpen);
}

TEST_CASE("evaluateComboBox returns Hovered when mouse over header", "[ui][combobox]")
{
	ComboBoxConfig config{COMBO_RECT, 0, false, 30.0f, 5};
	auto result = evaluateComboBox(config, {150.0f, 215.0f}, false, false);
	REQUIRE(result.state == WidgetState::Hovered);
}

TEST_CASE("evaluateComboBox opens dropdown on header click", "[ui][combobox]")
{
	ComboBoxConfig config{COMBO_RECT, 0, false, 30.0f, 5};
	auto result = evaluateComboBox(config, {150.0f, 215.0f}, true, true);
	REQUIRE(result.isOpen);
	REQUIRE(result.selectedIndex == -1);
}

TEST_CASE("evaluateComboBox closes dropdown on second header click", "[ui][combobox]")
{
	ComboBoxConfig config{COMBO_RECT, 0, true, 30.0f, 5};
	auto result = evaluateComboBox(config, {150.0f, 215.0f}, true, true);
	REQUIRE_FALSE(result.isOpen);
}

TEST_CASE("evaluateComboBox selects item on dropdown click", "[ui][combobox]")
{
	ComboBoxConfig config{COMBO_RECT, 0, true, 30.0f, 5};
	// Click on second item: y = 230 (header bottom) + 30 + 5 = within second item
	auto result = evaluateComboBox(config, {150.0f, 265.0f}, true, true);
	REQUIRE(result.selectedIndex == 1);
	REQUIRE_FALSE(result.isOpen);
}

TEST_CASE("evaluateComboBox closes on outside click when open", "[ui][combobox]")
{
	ComboBoxConfig config{COMBO_RECT, 0, true, 30.0f, 5};
	auto result = evaluateComboBox(config, {500.0f, 500.0f}, true, true);
	REQUIRE_FALSE(result.isOpen);
	REQUIRE(result.selectedIndex == -1);
}

TEST_CASE("evaluateComboBox calculates dropdown bounds correctly", "[ui][combobox]")
{
	ComboBoxConfig config{COMBO_RECT, 0, true, 30.0f, 10, 5.0f};
	auto result = evaluateComboBox(config, {0.0f, 0.0f}, false, false);
	// Dropdown: 5 visible items * 30.0f = 150.0f height
	REQUIRE(result.dropdownBounds.x() == 100.0f);
	REQUIRE(result.dropdownBounds.y() == 230.0f);
	REQUIRE(result.dropdownBounds.width() == 200.0f);
	REQUIRE(result.dropdownBounds.height() == 150.0f);
}

TEST_CASE("evaluateComboBox handles zero items", "[ui][combobox]")
{
	ComboBoxConfig config{COMBO_RECT, -1, false, 30.0f, 0};
	auto result = evaluateComboBox(config, {150.0f, 215.0f}, true, true);
	REQUIRE(result.isOpen);
	REQUIRE(result.selectedIndex == -1);
}
