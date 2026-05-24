#include <catch2/catch_test_macros.hpp>
#include <sgc/ui/TabBar.hpp>

using namespace sgc;
using namespace sgc::ui;

TEST_CASE("evaluateTabBar - initial selection preserved", "[ui][tabbar]")
{
	Rectf bounds{0.0f, 0.0f, 300.0f, 40.0f};
	auto result = evaluateTabBar(bounds, 3, 1, {0.0f, 0.0f}, false, false);
	REQUIRE(result.selectedIndex == 1);
	REQUIRE_FALSE(result.changed);
}

TEST_CASE("evaluateTabBar - click changes selection", "[ui][tabbar]")
{
	Rectf bounds{0.0f, 0.0f, 300.0f, 40.0f};
	// タブ幅100px、3番目のタブ中央をクリック
	Vec2f clickPos{250.0f, 20.0f};
	auto result = evaluateTabBar(bounds, 3, 0, clickPos, true, true);
	REQUIRE(result.selectedIndex == 2);
	REQUIRE(result.changed);
}

TEST_CASE("evaluateTabBar - click on current tab no change", "[ui][tabbar]")
{
	Rectf bounds{0.0f, 0.0f, 300.0f, 40.0f};
	Vec2f clickPos{50.0f, 20.0f}; // タブ0の中央
	auto result = evaluateTabBar(bounds, 3, 0, clickPos, true, true);
	REQUIRE(result.selectedIndex == 0);
	REQUIRE_FALSE(result.changed);
}

TEST_CASE("evaluateTabBar - tab bounds count matches tabCount", "[ui][tabbar]")
{
	Rectf bounds{0.0f, 0.0f, 400.0f, 40.0f};
	auto result = evaluateTabBar(bounds, 4, 0, {0.0f, 0.0f}, false, false);
	REQUIRE(result.tabBounds.size() == 4);
	REQUIRE(result.tabStates.size() == 4);
}

TEST_CASE("evaluateTabBar - zero tabs returns empty", "[ui][tabbar]")
{
	Rectf bounds{0.0f, 0.0f, 300.0f, 40.0f};
	auto result = evaluateTabBar(bounds, 0, 0, {0.0f, 0.0f}, false, false);
	REQUIRE(result.tabBounds.empty());
	REQUIRE_FALSE(result.changed);
}

TEST_CASE("evaluateTabBar - selected tab has Focused state", "[ui][tabbar]")
{
	Rectf bounds{0.0f, 0.0f, 300.0f, 40.0f};
	// マウスをタブ外に置く
	auto result = evaluateTabBar(bounds, 3, 1, {-10.0f, -10.0f}, false, false);
	REQUIRE(result.tabStates[1] == WidgetState::Focused);
	REQUIRE(result.tabStates[0] == WidgetState::Normal);
}

TEST_CASE("evaluateTabBar - hover state on non-selected tab", "[ui][tabbar]")
{
	Rectf bounds{0.0f, 0.0f, 300.0f, 40.0f};
	Vec2f hoverPos{250.0f, 20.0f}; // タブ2上
	auto result = evaluateTabBar(bounds, 3, 0, hoverPos, false, false);
	REQUIRE(result.tabStates[2] == WidgetState::Hovered);
}
