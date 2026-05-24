#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/ui/FlexLayout.hpp"

using namespace sgc;
using namespace sgc::ui;

static constexpr auto approx = [](float expected) {
	return Catch::Matchers::WithinAbs(expected, 0.5f);
};

TEST_CASE("FlexLayout - empty items returns empty", "[ui][flex]")
{
	auto result = flexLayout(Rectf{0, 0, 400, 300}, std::span<const FlexItem>{});
	REQUIRE(result.empty());
}

TEST_CASE("FlexLayout - single item row start", "[ui][flex]")
{
	FlexItem items[] = {{100, 40}};
	auto result = flexLayout(Rectf{10, 20, 400, 300}, std::span{items}, {});

	REQUIRE(result.size() == 1);
	REQUIRE_THAT(result[0].x(), approx(10.0f));
	REQUIRE_THAT(result[0].y(), approx(20.0f));
	REQUIRE_THAT(result[0].width(), approx(100.0f));
	REQUIRE_THAT(result[0].height(), approx(40.0f));
}

TEST_CASE("FlexLayout - row with gap", "[ui][flex]")
{
	FlexItem items[] = {{100, 40}, {80, 40}, {60, 40}};
	FlexConfig config;
	config.gap = 10.0f;

	auto result = flexLayout(Rectf{0, 0, 400, 100}, std::span{items}, config);

	REQUIRE(result.size() == 3);
	REQUIRE_THAT(result[0].x(), approx(0.0f));
	REQUIRE_THAT(result[1].x(), approx(110.0f)); // 100 + 10 gap
	REQUIRE_THAT(result[2].x(), approx(200.0f)); // 110 + 80 + 10 gap
}

TEST_CASE("FlexLayout - justify center", "[ui][flex]")
{
	FlexItem items[] = {{100, 40}};
	FlexConfig config;
	config.justify = FlexJustify::Center;

	auto result = flexLayout(Rectf{0, 0, 400, 100}, std::span{items}, config);

	// 400 - 100 = 300 free, center offset = 150
	REQUIRE_THAT(result[0].x(), approx(150.0f));
}

TEST_CASE("FlexLayout - justify end", "[ui][flex]")
{
	FlexItem items[] = {{100, 40}};
	FlexConfig config;
	config.justify = FlexJustify::End;

	auto result = flexLayout(Rectf{0, 0, 400, 100}, std::span{items}, config);

	REQUIRE_THAT(result[0].x(), approx(300.0f));
}

TEST_CASE("FlexLayout - justify space-between", "[ui][flex]")
{
	FlexItem items[] = {{80, 40}, {80, 40}, {80, 40}};
	FlexConfig config;
	config.justify = FlexJustify::SpaceBetween;

	auto result = flexLayout(Rectf{0, 0, 400, 100}, std::span{items}, config);

	// 400 - 240 = 160, divided by 2 gaps = 80 each
	REQUIRE_THAT(result[0].x(), approx(0.0f));
	REQUIRE_THAT(result[1].x(), approx(160.0f));
	REQUIRE_THAT(result[2].x(), approx(320.0f));
}

TEST_CASE("FlexLayout - justify space-evenly", "[ui][flex]")
{
	FlexItem items[] = {{60, 40}, {60, 40}};
	FlexConfig config;
	config.justify = FlexJustify::SpaceEvenly;

	auto result = flexLayout(Rectf{0, 0, 300, 100}, std::span{items}, config);

	// 300 - 120 = 180, divided by 3 spaces = 60 each
	REQUIRE_THAT(result[0].x(), approx(60.0f));
	REQUIRE_THAT(result[1].x(), approx(180.0f));
}

TEST_CASE("FlexLayout - align center", "[ui][flex]")
{
	FlexItem items[] = {{100, 30}};
	FlexConfig config;
	config.align = FlexAlign::Center;

	auto result = flexLayout(Rectf{0, 0, 400, 100}, std::span{items}, config);

	// Cross axis: (30 max cross - 30) / 2 = 0 (centered within line height)
	REQUIRE_THAT(result[0].y(), approx(0.0f));
}

TEST_CASE("FlexLayout - align center with different sizes", "[ui][flex]")
{
	FlexItem items[] = {{100, 20}, {100, 60}};
	FlexConfig config;
	config.align = FlexAlign::Center;

	auto result = flexLayout(Rectf{0, 0, 400, 100}, std::span{items}, config);

	// Line maxCross = 60
	// Item 0: (60-20)/2 = 20 offset
	// Item 1: (60-60)/2 = 0 offset
	REQUIRE_THAT(result[0].y(), approx(20.0f));
	REQUIRE_THAT(result[1].y(), approx(0.0f));
}

TEST_CASE("FlexLayout - align stretch", "[ui][flex]")
{
	FlexItem items[] = {{100, 30}, {100, 50}};
	FlexConfig config;
	config.align = FlexAlign::Stretch;

	auto result = flexLayout(Rectf{0, 0, 400, 100}, std::span{items}, config);

	// Both items stretch to line maxCross = 50
	REQUIRE_THAT(result[0].height(), approx(50.0f));
	REQUIRE_THAT(result[1].height(), approx(50.0f));
}

TEST_CASE("FlexLayout - column direction", "[ui][flex]")
{
	FlexItem items[] = {{40, 100}, {40, 80}};
	FlexConfig config;
	config.direction = FlexDirection::Column;
	config.gap = 10.0f;

	auto result = flexLayout(Rectf{10, 20, 200, 400}, std::span{items}, config);

	// Column: mainSize=height, crossSize=width
	REQUIRE_THAT(result[0].y(), approx(20.0f));
	REQUIRE_THAT(result[0].height(), approx(40.0f));
	REQUIRE_THAT(result[0].width(), approx(100.0f));

	REQUIRE_THAT(result[1].y(), approx(70.0f)); // 20 + 40 + 10
	REQUIRE_THAT(result[1].height(), approx(40.0f));
}

TEST_CASE("FlexLayout - row reverse", "[ui][flex]")
{
	FlexItem items[] = {{100, 40}, {80, 40}};
	FlexConfig config;
	config.direction = FlexDirection::RowReverse;

	auto result = flexLayout(Rectf{0, 0, 400, 100}, std::span{items}, config);

	// Reversed: item[1] at start, item[0] after
	REQUIRE_THAT(result[1].x(), approx(0.0f));
	REQUIRE_THAT(result[0].x(), approx(80.0f));
}

TEST_CASE("FlexLayout - flex grow distributes space", "[ui][flex]")
{
	FlexItem items[] = {{100, 40, 1.0f}, {100, 40, 1.0f}};
	FlexConfig config;

	auto result = flexLayout(Rectf{0, 0, 400, 100}, std::span{items}, config);

	// 400 - 200 = 200 free, split evenly
	REQUIRE_THAT(result[0].width(), approx(200.0f));
	REQUIRE_THAT(result[1].width(), approx(200.0f));
}

TEST_CASE("FlexLayout - flex grow weighted", "[ui][flex]")
{
	FlexItem items[] = {{0, 40, 1.0f}, {0, 40, 2.0f}};
	FlexConfig config;

	auto result = flexLayout(Rectf{0, 0, 300, 100}, std::span{items}, config);

	// 300 free, 1:2 ratio => 100 and 200
	REQUIRE_THAT(result[0].width(), approx(100.0f));
	REQUIRE_THAT(result[1].width(), approx(200.0f));
}

TEST_CASE("FlexLayout - wrap creates new line", "[ui][flex]")
{
	FlexItem items[] = {{150, 40}, {150, 40}, {150, 40}};
	FlexConfig config;
	config.wrap = FlexWrap::Wrap;
	config.crossGap = 5.0f;

	auto result = flexLayout(Rectf{0, 0, 320, 200}, std::span{items}, config);

	// 320 fits 2 items (300), 3rd wraps
	REQUIRE_THAT(result[0].x(), approx(0.0f));
	REQUIRE_THAT(result[0].y(), approx(0.0f));
	REQUIRE_THAT(result[1].x(), approx(150.0f));
	REQUIRE_THAT(result[1].y(), approx(0.0f));
	REQUIRE_THAT(result[2].x(), approx(0.0f));
	REQUIRE_THAT(result[2].y(), approx(45.0f)); // 40 + 5 crossGap
}
