#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <sgc/ui/ListView.hpp>

using namespace sgc;
using namespace sgc::ui;
using Catch::Matchers::WithinAbs;

TEST_CASE("evaluateListView - click selects item", "[ui][listview]")
{
	Rectf bounds{0.0f, 0.0f, 200.0f, 200.0f};
	// アイテム3の中央をクリック (y = 3 * 32 + 16 = 112)
	Vec2f clickPos{100.0f, 112.0f};
	auto result = evaluateListView(bounds, 10, 32.0f, -1, 0.0f,
	                                clickPos, true, true);
	REQUIRE(result.selectedIndex == 3);
	REQUIRE(result.selectionChanged);
}

TEST_CASE("evaluateListView - hover without click", "[ui][listview]")
{
	Rectf bounds{0.0f, 0.0f, 200.0f, 200.0f};
	Vec2f hoverPos{100.0f, 50.0f}; // アイテム1
	auto result = evaluateListView(bounds, 10, 32.0f, -1, 0.0f,
	                                hoverPos, false, false);
	REQUIRE(result.hoveredIndex == 1);
	REQUIRE(result.selectedIndex == -1);
	REQUIRE_FALSE(result.selectionChanged);
}

TEST_CASE("evaluateListView - empty list", "[ui][listview]")
{
	Rectf bounds{0.0f, 0.0f, 200.0f, 200.0f};
	auto result = evaluateListView(bounds, 0, 32.0f, -1, 0.0f,
	                                {100.0f, 100.0f}, false, false);
	REQUIRE(result.selectedIndex == -1);
	REQUIRE(result.hoveredIndex == -1);
}

TEST_CASE("evaluateListView - scrollbar appears when needed", "[ui][listview]")
{
	Rectf bounds{0.0f, 0.0f, 200.0f, 100.0f};
	// 10 items x 32px = 320px > 100px viewport → scroll needed
	auto result = evaluateListView(bounds, 10, 32.0f, -1, 0.0f,
	                                {0.0f, 0.0f}, false, false);
	// 可視領域はスクロールバー分だけ狭い
	REQUIRE(result.visibleArea.width() < bounds.width());
}

TEST_CASE("evaluateListView - no scrollbar when content fits", "[ui][listview]")
{
	Rectf bounds{0.0f, 0.0f, 200.0f, 200.0f};
	// 3 items x 32px = 96px < 200px viewport → no scroll
	auto result = evaluateListView(bounds, 3, 32.0f, -1, 0.0f,
	                                {0.0f, 0.0f}, false, false);
	REQUIRE_THAT(result.visibleArea.width(), WithinAbs(200.0f, 0.01f));
}

TEST_CASE("evaluateListView - scrolled hover maps correctly", "[ui][listview]")
{
	Rectf bounds{0.0f, 0.0f, 200.0f, 100.0f};
	// scrollOffset=64 → 上位2アイテムがスクロールアウト
	// mousePos.y=20 + scrollOffset=64 → relativeY=84 → index=2
	auto result = evaluateListView(bounds, 10, 32.0f, -1, 64.0f,
	                                {100.0f, 20.0f}, false, false);
	REQUIRE(result.hoveredIndex == 2);
}

TEST_CASE("evaluateListView - click outside list area", "[ui][listview]")
{
	Rectf bounds{0.0f, 0.0f, 200.0f, 200.0f};
	Vec2f outside{300.0f, 100.0f};
	auto result = evaluateListView(bounds, 10, 32.0f, 0, 0.0f,
	                                outside, true, true);
	// 選択は変わらない
	REQUIRE(result.selectedIndex == 0);
	REQUIRE_FALSE(result.selectionChanged);
}

TEST_CASE("listItemBounds - correct position", "[ui][listview]")
{
	Rectf area{10.0f, 20.0f, 180.0f, 200.0f};
	auto itemRect = listItemBounds(area, 2, 32.0f, 0.0f);
	REQUIRE_THAT(itemRect.x(), WithinAbs(10.0f, 0.01f));
	REQUIRE_THAT(itemRect.y(), WithinAbs(84.0f, 0.01f)); // 20 + 2*32
	REQUIRE_THAT(itemRect.width(), WithinAbs(180.0f, 0.01f));
	REQUIRE_THAT(itemRect.height(), WithinAbs(32.0f, 0.01f));
}

TEST_CASE("listItemBounds - with scroll offset", "[ui][listview]")
{
	Rectf area{0.0f, 0.0f, 200.0f, 200.0f};
	auto itemRect = listItemBounds(area, 3, 32.0f, 64.0f);
	// y = 0 + 3*32 - 64 = 32
	REQUIRE_THAT(itemRect.y(), WithinAbs(32.0f, 0.01f));
}
