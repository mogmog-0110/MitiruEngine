/// @file TestStackLayout.cpp
/// @brief スタックレイアウト計算ユーティリティのテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/ui/StackLayout.hpp"

using namespace sgc;
using namespace sgc::ui;
using Catch::Matchers::WithinAbs;

static const Rectf PARENT{{10.0f, 10.0f}, {200.0f, 400.0f}};

// ── vstack ─────────────────────────────────────────────

TEST_CASE("vstack positions 3 items vertically with parent width", "[ui][stacklayout]")
{
	const float heights[] = {30.0f, 50.0f, 20.0f};
	const auto rects = vstack(PARENT, heights);

	REQUIRE(rects.size() == 3);
	REQUIRE_THAT(rects[0].x(), WithinAbs(10.0, 1e-5));
	REQUIRE_THAT(rects[0].y(), WithinAbs(10.0, 1e-5));
	REQUIRE_THAT(rects[0].width(), WithinAbs(200.0, 1e-5));
	REQUIRE_THAT(rects[0].height(), WithinAbs(30.0, 1e-5));

	REQUIRE_THAT(rects[1].y(), WithinAbs(40.0, 1e-5));
	REQUIRE_THAT(rects[1].height(), WithinAbs(50.0, 1e-5));

	REQUIRE_THAT(rects[2].y(), WithinAbs(90.0, 1e-5));
	REQUIRE_THAT(rects[2].height(), WithinAbs(20.0, 1e-5));
}

TEST_CASE("vstack with spacing separates items", "[ui][stacklayout]")
{
	const float heights[] = {30.0f, 50.0f, 20.0f};
	const auto rects = vstack(PARENT, heights, 8.0f);

	REQUIRE(rects.size() == 3);
	REQUIRE_THAT(rects[0].y(), WithinAbs(10.0, 1e-5));
	REQUIRE_THAT(rects[1].y(), WithinAbs(48.0, 1e-5));   // 10 + 30 + 8
	REQUIRE_THAT(rects[2].y(), WithinAbs(106.0, 1e-5));   // 48 + 50 + 8
}

TEST_CASE("vstack with empty span returns empty vector", "[ui][stacklayout]")
{
	const auto rects = vstack(PARENT, std::span<const float>{});
	REQUIRE(rects.empty());
}

// ── hstack ─────────────────────────────────────────────

TEST_CASE("hstack positions 3 items horizontally with parent height", "[ui][stacklayout]")
{
	const float widths[] = {60.0f, 80.0f, 40.0f};
	const auto rects = hstack(PARENT, widths);

	REQUIRE(rects.size() == 3);
	REQUIRE_THAT(rects[0].x(), WithinAbs(10.0, 1e-5));
	REQUIRE_THAT(rects[0].y(), WithinAbs(10.0, 1e-5));
	REQUIRE_THAT(rects[0].width(), WithinAbs(60.0, 1e-5));
	REQUIRE_THAT(rects[0].height(), WithinAbs(400.0, 1e-5));

	REQUIRE_THAT(rects[1].x(), WithinAbs(70.0, 1e-5));
	REQUIRE_THAT(rects[2].x(), WithinAbs(150.0, 1e-5));
}

TEST_CASE("hstack with spacing separates items", "[ui][stacklayout]")
{
	const float widths[] = {60.0f, 80.0f};
	const auto rects = hstack(PARENT, widths, 10.0f);

	REQUIRE(rects.size() == 2);
	REQUIRE_THAT(rects[0].x(), WithinAbs(10.0, 1e-5));
	REQUIRE_THAT(rects[1].x(), WithinAbs(80.0, 1e-5));   // 10 + 60 + 10
}

// ── vstackFixed ────────────────────────────────────────

TEST_CASE("vstackFixed with 3 items distributes height equally", "[ui][stacklayout]")
{
	const auto rects = vstackFixed(PARENT, 3, 10.0f);
	// 各アイテム高さ = (400 - 2*10) / 3 ≈ 126.667
	const float expectedH = (400.0f - 20.0f) / 3.0f;

	REQUIRE(rects.size() == 3);
	REQUIRE_THAT(rects[0].y(), WithinAbs(10.0, 1e-5));
	REQUIRE_THAT(rects[0].height(), WithinAbs(static_cast<double>(expectedH), 1e-3));
	REQUIRE_THAT(rects[0].width(), WithinAbs(200.0, 1e-5));

	REQUIRE_THAT(rects[1].y(), WithinAbs(static_cast<double>(10.0f + expectedH + 10.0f), 1e-3));
	REQUIRE_THAT(rects[2].y(), WithinAbs(static_cast<double>(10.0f + 2.0f * (expectedH + 10.0f)), 1e-3));
}

TEST_CASE("vstackFixed with 0 items returns empty vector", "[ui][stacklayout]")
{
	const auto rects = vstackFixed(PARENT, 0, 5.0f);
	REQUIRE(rects.empty());
}

TEST_CASE("vstackFixed with 1 item fills parent height", "[ui][stacklayout]")
{
	const auto rects = vstackFixed(PARENT, 1, 10.0f);
	REQUIRE(rects.size() == 1);
	REQUIRE_THAT(rects[0].y(), WithinAbs(10.0, 1e-5));
	REQUIRE_THAT(rects[0].height(), WithinAbs(400.0, 1e-5));
}

// ── hstackFixed ────────────────────────────────────────

TEST_CASE("hstackFixed with 4 items distributes width equally", "[ui][stacklayout]")
{
	const auto rects = hstackFixed(PARENT, 4);
	// spacing = 0 → 各アイテム幅 = 200 / 4 = 50
	REQUIRE(rects.size() == 4);
	REQUIRE_THAT(rects[0].x(), WithinAbs(10.0, 1e-5));
	REQUIRE_THAT(rects[0].width(), WithinAbs(50.0, 1e-5));
	REQUIRE_THAT(rects[1].x(), WithinAbs(60.0, 1e-5));
	REQUIRE_THAT(rects[2].x(), WithinAbs(110.0, 1e-5));
	REQUIRE_THAT(rects[3].x(), WithinAbs(160.0, 1e-5));
}

TEST_CASE("hstackFixed with spacing distributes width correctly", "[ui][stacklayout]")
{
	const auto rects = hstackFixed(PARENT, 4, 8.0f);
	// 各アイテム幅 = (200 - 3*8) / 4 = (200 - 24) / 4 = 44
	const float expectedW = (200.0f - 24.0f) / 4.0f;

	REQUIRE(rects.size() == 4);
	REQUIRE_THAT(rects[0].width(), WithinAbs(static_cast<double>(expectedW), 1e-3));
	REQUIRE_THAT(rects[0].height(), WithinAbs(400.0, 1e-5));
	REQUIRE_THAT(rects[1].x(), WithinAbs(static_cast<double>(10.0f + expectedW + 8.0f), 1e-3));
}
