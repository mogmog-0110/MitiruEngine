/// @file TestAnchor.cpp
/// @brief Anchor + Margin layout system tests

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/ui/Anchor.hpp"

using namespace sgc;
using namespace sgc::ui;
using Catch::Approx;

// ── anchorPoint tests ────────────────────────────────────

TEST_CASE("anchorPoint returns correct position for all 9 anchors", "[ui][anchor]")
{
	const Rectf parent{{100.0f, 200.0f}, {400.0f, 300.0f}};

	SECTION("TopLeft")
	{
		auto p = anchorPoint(parent, Anchor::TopLeft);
		REQUIRE(p.x == Approx(100.0f));
		REQUIRE(p.y == Approx(200.0f));
	}

	SECTION("TopCenter")
	{
		auto p = anchorPoint(parent, Anchor::TopCenter);
		REQUIRE(p.x == Approx(300.0f));
		REQUIRE(p.y == Approx(200.0f));
	}

	SECTION("TopRight")
	{
		auto p = anchorPoint(parent, Anchor::TopRight);
		REQUIRE(p.x == Approx(500.0f));
		REQUIRE(p.y == Approx(200.0f));
	}

	SECTION("CenterLeft")
	{
		auto p = anchorPoint(parent, Anchor::CenterLeft);
		REQUIRE(p.x == Approx(100.0f));
		REQUIRE(p.y == Approx(350.0f));
	}

	SECTION("Center")
	{
		auto p = anchorPoint(parent, Anchor::Center);
		REQUIRE(p.x == Approx(300.0f));
		REQUIRE(p.y == Approx(350.0f));
	}

	SECTION("CenterRight")
	{
		auto p = anchorPoint(parent, Anchor::CenterRight);
		REQUIRE(p.x == Approx(500.0f));
		REQUIRE(p.y == Approx(350.0f));
	}

	SECTION("BottomLeft")
	{
		auto p = anchorPoint(parent, Anchor::BottomLeft);
		REQUIRE(p.x == Approx(100.0f));
		REQUIRE(p.y == Approx(500.0f));
	}

	SECTION("BottomCenter")
	{
		auto p = anchorPoint(parent, Anchor::BottomCenter);
		REQUIRE(p.x == Approx(300.0f));
		REQUIRE(p.y == Approx(500.0f));
	}

	SECTION("BottomRight")
	{
		auto p = anchorPoint(parent, Anchor::BottomRight);
		REQUIRE(p.x == Approx(500.0f));
		REQUIRE(p.y == Approx(500.0f));
	}
}

// ── anchoredPosition tests ───────────────────────────────

TEST_CASE("anchoredPosition without offset matches anchorPoint", "[ui][anchor]")
{
	const Rectf parent{{0.0f, 0.0f}, {800.0f, 600.0f}};
	auto p = anchoredPosition(parent, Anchor::Center);
	REQUIRE(p.x == Approx(400.0f));
	REQUIRE(p.y == Approx(300.0f));
}

TEST_CASE("anchoredPosition with offset applies correctly", "[ui][anchor]")
{
	const Rectf parent{{0.0f, 0.0f}, {800.0f, 600.0f}};
	auto p = anchoredPosition(parent, Anchor::TopLeft, {10.0f, 20.0f});
	REQUIRE(p.x == Approx(10.0f));
	REQUIRE(p.y == Approx(20.0f));
}

TEST_CASE("anchoredPosition with negative offset", "[ui][anchor]")
{
	const Rectf parent{{0.0f, 0.0f}, {800.0f, 600.0f}};
	auto p = anchoredPosition(parent, Anchor::BottomRight, {-10.0f, -20.0f});
	REQUIRE(p.x == Approx(790.0f));
	REQUIRE(p.y == Approx(580.0f));
}

// ── alignedRect tests ────────────────────────────────────

TEST_CASE("alignedRect TopLeft places child at top-left", "[ui][anchor]")
{
	const Rectf parent{{0.0f, 0.0f}, {800.0f, 600.0f}};
	auto r = alignedRect(parent, {100.0f, 50.0f}, Anchor::TopLeft);
	REQUIRE(r.x() == Approx(0.0f));
	REQUIRE(r.y() == Approx(0.0f));
	REQUIRE(r.width() == Approx(100.0f));
	REQUIRE(r.height() == Approx(50.0f));
}

TEST_CASE("alignedRect Center places child centered", "[ui][anchor]")
{
	const Rectf parent{{0.0f, 0.0f}, {800.0f, 600.0f}};
	auto r = alignedRect(parent, {200.0f, 100.0f}, Anchor::Center);
	REQUIRE(r.x() == Approx(300.0f));
	REQUIRE(r.y() == Approx(250.0f));
	REQUIRE(r.width() == Approx(200.0f));
	REQUIRE(r.height() == Approx(100.0f));
}

TEST_CASE("alignedRect BottomRight places child at bottom-right", "[ui][anchor]")
{
	const Rectf parent{{0.0f, 0.0f}, {800.0f, 600.0f}};
	auto r = alignedRect(parent, {100.0f, 40.0f}, Anchor::BottomRight);
	REQUIRE(r.x() == Approx(700.0f));
	REQUIRE(r.y() == Approx(560.0f));
}

TEST_CASE("alignedRect TopLeft with margin offsets correctly", "[ui][anchor]")
{
	const Rectf parent{{0.0f, 0.0f}, {800.0f, 600.0f}};
	auto r = alignedRect(parent, {100.0f, 50.0f}, Anchor::TopLeft,
		Margin{10.0f, 0.0f, 0.0f, 20.0f});
	REQUIRE(r.x() == Approx(20.0f));
	REQUIRE(r.y() == Approx(10.0f));
}

TEST_CASE("alignedRect BottomRight with margin offsets correctly", "[ui][anchor]")
{
	const Rectf parent{{0.0f, 0.0f}, {800.0f, 600.0f}};
	auto r = alignedRect(parent, {100.0f, 50.0f}, Anchor::BottomRight,
		Margin{0.0f, 15.0f, 10.0f, 0.0f});
	REQUIRE(r.x() == Approx(685.0f));  // 800 - 100 - 15
	REQUIRE(r.y() == Approx(540.0f));  // 600 - 50 - 10
}

TEST_CASE("alignedRect TopCenter centers horizontally", "[ui][anchor]")
{
	const Rectf parent{{0.0f, 0.0f}, {800.0f, 600.0f}};
	auto r = alignedRect(parent, {300.0f, 60.0f}, Anchor::TopCenter,
		Margin{20.0f, 0.0f, 0.0f, 0.0f});
	REQUIRE(r.x() == Approx(250.0f));  // (800-300)/2
	REQUIRE(r.y() == Approx(20.0f));   // margin.top
}

TEST_CASE("alignedRect CenterLeft positions at left edge vertically centered", "[ui][anchor]")
{
	const Rectf parent{{0.0f, 0.0f}, {800.0f, 600.0f}};
	auto r = alignedRect(parent, {120.0f, 80.0f}, Anchor::CenterLeft,
		Margin{0.0f, 0.0f, 0.0f, 10.0f});
	REQUIRE(r.x() == Approx(10.0f));
	REQUIRE(r.y() == Approx(260.0f));  // (600-80)/2
}

TEST_CASE("alignedRect BottomCenter centers horizontally at bottom", "[ui][anchor]")
{
	const Rectf parent{{0.0f, 0.0f}, {800.0f, 600.0f}};
	auto r = alignedRect(parent, {200.0f, 40.0f}, Anchor::BottomCenter,
		Margin{0.0f, 0.0f, 30.0f, 0.0f});
	REQUIRE(r.x() == Approx(300.0f));  // (800-200)/2
	REQUIRE(r.y() == Approx(530.0f));  // 600 - 40 - 30
}

// ── applyMargin / applyPadding tests ─────────────────────

TEST_CASE("applyMargin shrinks rect by 4-side margins", "[ui][anchor]")
{
	const Rectf rect{{0.0f, 0.0f}, {400.0f, 300.0f}};
	auto r = applyMargin(rect, {10.0f, 20.0f, 30.0f, 40.0f});
	REQUIRE(r.x() == Approx(40.0f));
	REQUIRE(r.y() == Approx(10.0f));
	REQUIRE(r.width() == Approx(340.0f));   // 400-40-20
	REQUIRE(r.height() == Approx(260.0f));  // 300-10-30
}

TEST_CASE("applyPadding produces same result as applyMargin", "[ui][anchor]")
{
	const Rectf rect{{50.0f, 50.0f}, {200.0f, 100.0f}};
	const Margin m{5.0f, 10.0f, 15.0f, 20.0f};
	auto rm = applyMargin(rect, m);
	auto rp = applyPadding(rect, m);
	REQUIRE(rm == rp);
}

// ── Margin factory tests ─────────────────────────────────

TEST_CASE("Margin::uniform sets all sides equal", "[ui][anchor]")
{
	auto m = Margin::uniform(10.0f);
	REQUIRE(m.top == Approx(10.0f));
	REQUIRE(m.right == Approx(10.0f));
	REQUIRE(m.bottom == Approx(10.0f));
	REQUIRE(m.left == Approx(10.0f));
}

TEST_CASE("Margin::symmetric sets horizontal and vertical", "[ui][anchor]")
{
	auto m = Margin::symmetric(20.0f, 10.0f);
	REQUIRE(m.top == Approx(10.0f));
	REQUIRE(m.right == Approx(20.0f));
	REQUIRE(m.bottom == Approx(10.0f));
	REQUIRE(m.left == Approx(20.0f));
}

TEST_CASE("Margin::zero returns all zeros", "[ui][anchor]")
{
	auto m = Margin::zero();
	REQUIRE(m.top == Approx(0.0f));
	REQUIRE(m.right == Approx(0.0f));
	REQUIRE(m.bottom == Approx(0.0f));
	REQUIRE(m.left == Approx(0.0f));
}

// ── screenRect tests ─────────────────────────────────────

TEST_CASE("screenRect creates rect from origin", "[ui][anchor]")
{
	auto r = screenRect(800.0f, 600.0f);
	REQUIRE(r.x() == Approx(0.0f));
	REQUIRE(r.y() == Approx(0.0f));
	REQUIRE(r.width() == Approx(800.0f));
	REQUIRE(r.height() == Approx(600.0f));
}

// ── Edge cases ───────────────────────────────────────────

TEST_CASE("anchorPoint with zero-size parent returns position for all anchors", "[ui][anchor]")
{
	const Rectf parent{{50.0f, 50.0f}, {0.0f, 0.0f}};
	auto p = anchorPoint(parent, Anchor::Center);
	REQUIRE(p.x == Approx(50.0f));
	REQUIRE(p.y == Approx(50.0f));
}

TEST_CASE("alignedRect with non-origin parent works correctly", "[ui][anchor]")
{
	const Rectf parent{{100.0f, 100.0f}, {400.0f, 300.0f}};
	auto r = alignedRect(parent, {80.0f, 40.0f}, Anchor::Center);
	REQUIRE(r.x() == Approx(260.0f));  // 100+200-40
	REQUIRE(r.y() == Approx(230.0f));  // 100+150-20
}

// ── constexpr validation ─────────────────────────────────

TEST_CASE("Anchor functions are constexpr", "[ui][anchor]")
{
	constexpr Rectf parent{{0.0f, 0.0f}, {800.0f, 600.0f}};
	constexpr auto p = anchorPoint(parent, Anchor::Center);
	static_assert(p.x == 400.0f);
	static_assert(p.y == 300.0f);

	constexpr auto r = alignedRect(parent, {200.0f, 100.0f}, Anchor::Center);
	static_assert(r.x() == 300.0f);
	static_assert(r.y() == 250.0f);

	constexpr auto m = Margin::uniform(5.0f);
	static_assert(m.top == 5.0f);

	constexpr auto s = screenRect(1920.0f, 1080.0f);
	static_assert(s.width() == 1920.0f);

	REQUIRE(true);  // constexprテストが通ればOK
}
