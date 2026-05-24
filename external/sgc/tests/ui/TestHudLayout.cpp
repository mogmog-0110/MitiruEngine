/// @file TestHudLayout.cpp
/// @brief HudLayout system tests

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <stdexcept>

#include "sgc/core/Hash.hpp"
#include "sgc/ui/HudLayout.hpp"

using namespace sgc;
using namespace sgc::ui;
using namespace sgc::literals;
using Catch::Approx;

static const Rectf SCREEN = screenRect(800.0f, 600.0f);

// ── add + recalculate ────────────────────────────────────

TEST_CASE("HudLayout TopLeft element positioned correctly", "[ui][hud]")
{
	HudLayout hud;
	hud.add("score"_hash, {Anchor::TopLeft, {10.0f, 10.0f}});
	hud.recalculate(SCREEN);

	auto pos = hud.position("score"_hash);
	REQUIRE(pos.x == Approx(10.0f));
	REQUIRE(pos.y == Approx(10.0f));
}

TEST_CASE("HudLayout Center element positioned correctly", "[ui][hud]")
{
	HudLayout hud;
	hud.add("title"_hash, {Anchor::Center, {0.0f, 0.0f}});
	hud.recalculate(SCREEN);

	auto pos = hud.position("title"_hash);
	REQUIRE(pos.x == Approx(400.0f));
	REQUIRE(pos.y == Approx(300.0f));
}

TEST_CASE("HudLayout BottomRight element positioned correctly", "[ui][hud]")
{
	HudLayout hud;
	hud.add("version"_hash, {Anchor::BottomRight, {-10.0f, -10.0f}});
	hud.recalculate(SCREEN);

	auto pos = hud.position("version"_hash);
	REQUIRE(pos.x == Approx(790.0f));
	REQUIRE(pos.y == Approx(590.0f));
}

TEST_CASE("HudLayout Center with offset", "[ui][hud]")
{
	HudLayout hud;
	hud.add("prompt"_hash, {Anchor::Center, {0.0f, 50.0f}});
	hud.recalculate(SCREEN);

	auto pos = hud.position("prompt"_hash);
	REQUIRE(pos.x == Approx(400.0f));
	REQUIRE(pos.y == Approx(350.0f));
}

// ── Screen size change ───────────────────────────────────

TEST_CASE("HudLayout recalculate updates positions on screen resize", "[ui][hud]")
{
	HudLayout hud;
	hud.add("center"_hash, {Anchor::Center, {0.0f, 0.0f}});

	hud.recalculate(screenRect(800.0f, 600.0f));
	REQUIRE(hud.position("center"_hash).x == Approx(400.0f));

	hud.recalculate(screenRect(1920.0f, 1080.0f));
	REQUIRE(hud.position("center"_hash).x == Approx(960.0f));
	REQUIRE(hud.position("center"_hash).y == Approx(540.0f));
}

// ── Size specified ───────────────────────────────────────

TEST_CASE("HudLayout element with size has correct bounds", "[ui][hud]")
{
	HudLayout hud;
	hud.add("button"_hash, {Anchor::Center, {0.0f, 0.0f}, {200.0f, 50.0f}});
	hud.recalculate(SCREEN);

	auto b = hud.bounds("button"_hash);
	REQUIRE(b.width() == Approx(200.0f));
	REQUIRE(b.height() == Approx(50.0f));
	REQUIRE(b.x() == Approx(300.0f));  // (800-200)/2
	REQUIRE(b.y() == Approx(275.0f));  // (600-50)/2
}

TEST_CASE("HudLayout element without size has zero-size bounds", "[ui][hud]")
{
	HudLayout hud;
	hud.add("label"_hash, {Anchor::TopLeft, {5.0f, 5.0f}});
	hud.recalculate(SCREEN);

	auto b = hud.bounds("label"_hash);
	REQUIRE(b.width() == Approx(0.0f));
	REQUIRE(b.height() == Approx(0.0f));
}

// ── setVisible ───────────────────────────────────────────

TEST_CASE("HudLayout setVisible changes element visibility", "[ui][hud]")
{
	HudLayout hud;
	hud.add("info"_hash, {Anchor::TopLeft, {0.0f, 0.0f}});

	REQUIRE(hud.element("info"_hash).visible);
	hud.setVisible("info"_hash, false);
	REQUIRE_FALSE(hud.element("info"_hash).visible);
	hud.setVisible("info"_hash, true);
	REQUIRE(hud.element("info"_hash).visible);
}

// ── has / count / clear ──────────────────────────────────

TEST_CASE("HudLayout has returns correct existence", "[ui][hud]")
{
	HudLayout hud;
	REQUIRE_FALSE(hud.has("test"_hash));
	hud.add("test"_hash, {});
	REQUIRE(hud.has("test"_hash));
}

TEST_CASE("HudLayout count returns element count", "[ui][hud]")
{
	HudLayout hud;
	REQUIRE(hud.count() == 0);
	hud.add("a"_hash, {});
	hud.add("b"_hash, {});
	REQUIRE(hud.count() == 2);
}

TEST_CASE("HudLayout clear removes all elements", "[ui][hud]")
{
	HudLayout hud;
	hud.add("a"_hash, {});
	hud.add("b"_hash, {});
	hud.clear();
	REQUIRE(hud.count() == 0);
	REQUIRE_FALSE(hud.has("a"_hash));
}

// ── Error handling ───────────────────────────────────────

TEST_CASE("HudLayout position throws for unknown element", "[ui][hud]")
{
	HudLayout hud;
	REQUIRE_THROWS_AS(hud.position("nonexistent"_hash), std::out_of_range);
}

TEST_CASE("HudLayout bounds throws for unknown element", "[ui][hud]")
{
	HudLayout hud;
	REQUIRE_THROWS_AS(hud.bounds("nonexistent"_hash), std::out_of_range);
}

// ── Overwrite on duplicate key ──────────────────────────

TEST_CASE("HudLayout add with duplicate key overwrites", "[ui][hud]")
{
	HudLayout hud;
	hud.add("item"_hash, {Anchor::TopLeft, {10.0f, 10.0f}});
	hud.add("item"_hash, {Anchor::BottomRight, {-5.0f, -5.0f}});
	hud.recalculate(SCREEN);

	REQUIRE(hud.count() == 1);
	auto pos = hud.position("item"_hash);
	REQUIRE(pos.x == Approx(795.0f));
	REQUIRE(pos.y == Approx(595.0f));
}

// ── Multiple elements ────────────────────────────────────

TEST_CASE("HudLayout multiple elements positioned independently", "[ui][hud]")
{
	HudLayout hud;
	hud.add("tl"_hash, {Anchor::TopLeft, {5.0f, 5.0f}});
	hud.add("br"_hash, {Anchor::BottomRight, {-5.0f, -5.0f}});
	hud.add("c"_hash, {Anchor::Center, {0.0f, 0.0f}});
	hud.recalculate(SCREEN);

	REQUIRE(hud.position("tl"_hash).x == Approx(5.0f));
	REQUIRE(hud.position("br"_hash).x == Approx(795.0f));
	REQUIRE(hud.position("c"_hash).x == Approx(400.0f));
}
