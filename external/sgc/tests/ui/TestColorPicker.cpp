/// @file TestColorPicker.cpp
/// @brief ColorPicker evaluation utility tests

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/ui/ColorPicker.hpp"

using namespace sgc;
using namespace sgc::ui;
using Catch::Matchers::WithinAbs;

static const Rectf PICKER_RECT{{50.0f, 50.0f}, {300.0f, 280.0f}};

static ColorPickerConfig makeConfig(float h = 0.0f, float s = 1.0f, float v = 1.0f,
                                     float a = 1.0f, bool showAlpha = true)
{
	return {PICKER_RECT, h, s, v, a, showAlpha, false, false, false};
}

TEST_CASE("evaluateColorPicker returns Normal when mouse outside", "[ui][colorpicker]")
{
	auto config = makeConfig();
	auto result = evaluateColorPicker(config, {0.0f, 0.0f}, false, false, false);
	REQUIRE(result.state == WidgetState::Normal);
	REQUIRE_FALSE(result.changed);
	REQUIRE_FALSE(result.isDraggingSV);
	REQUIRE_FALSE(result.isDraggingHue);
}

TEST_CASE("evaluateColorPicker starts SV drag on press in SV area", "[ui][colorpicker]")
{
	auto config = makeConfig();
	// SV area is left portion of picker
	auto result = evaluateColorPicker(config, {100.0f, 100.0f}, true, true, false);
	REQUIRE(result.isDraggingSV);
	REQUIRE(result.changed);
}

TEST_CASE("evaluateColorPicker starts Hue drag on press in hue bar", "[ui][colorpicker]")
{
	auto config = makeConfig();
	// Calculate hue bar position
	const float totalBarWidth = HUE_BAR_WIDTH + BAR_MARGIN + ALPHA_BAR_WIDTH + BAR_MARGIN;
	const float svWidth = 300.0f - totalBarWidth;
	const float hueBarX = 50.0f + svWidth + BAR_MARGIN;
	auto result = evaluateColorPicker(config, Vec2f{hueBarX + 5.0f, 100.0f}, true, true, false);
	REQUIRE(result.isDraggingHue);
}

TEST_CASE("evaluateColorPicker releases drag on mouse release", "[ui][colorpicker]")
{
	ColorPickerConfig config{PICKER_RECT, 180.0f, 0.5f, 0.5f, 1.0f, true, true, false, false};
	auto result = evaluateColorPicker(config, {100.0f, 100.0f}, false, false, true);
	REQUIRE_FALSE(result.isDraggingSV);
}

TEST_CASE("evaluateColorPicker unchanged when no interaction", "[ui][colorpicker]")
{
	auto config = makeConfig(120.0f, 0.5f, 0.8f);
	auto result = evaluateColorPicker(config, {0.0f, 0.0f}, false, false, false);
	REQUIRE_FALSE(result.changed);
	REQUIRE_THAT(result.hue, WithinAbs(120.0f, 0.01f));
	REQUIRE_THAT(result.saturation, WithinAbs(0.5f, 0.01f));
	REQUIRE_THAT(result.value, WithinAbs(0.8f, 0.01f));
}

TEST_CASE("hsvToRgb converts pure red", "[ui][colorpicker]")
{
	auto color = hsvToRgb(0.0f, 1.0f, 1.0f);
	REQUIRE_THAT(color.r, WithinAbs(1.0f, 0.01f));
	REQUIRE_THAT(color.g, WithinAbs(0.0f, 0.01f));
	REQUIRE_THAT(color.b, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("hsvToRgb converts pure green", "[ui][colorpicker]")
{
	auto color = hsvToRgb(120.0f, 1.0f, 1.0f);
	REQUIRE_THAT(color.r, WithinAbs(0.0f, 0.01f));
	REQUIRE_THAT(color.g, WithinAbs(1.0f, 0.01f));
	REQUIRE_THAT(color.b, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("hsvToRgb converts grayscale when saturation is zero", "[ui][colorpicker]")
{
	auto color = hsvToRgb(0.0f, 0.0f, 0.5f);
	REQUIRE_THAT(color.r, WithinAbs(0.5f, 0.01f));
	REQUIRE_THAT(color.g, WithinAbs(0.5f, 0.01f));
	REQUIRE_THAT(color.b, WithinAbs(0.5f, 0.01f));
}
