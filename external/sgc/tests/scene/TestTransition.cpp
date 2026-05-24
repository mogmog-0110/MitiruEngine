#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/scene/Transition.hpp"

using namespace sgc;

// ── FadeTransition ──────────────────────────────────────

TEST_CASE("FadeTransition - duration is correct", "[scene][transition]")
{
	FadeTransition fade{0.5f};
	REQUIRE_THAT(fade.totalDuration(), Catch::Matchers::WithinAbs(1.0f, 0.001f));
	REQUIRE_THAT(fade.outDuration(), Catch::Matchers::WithinAbs(0.5f, 0.001f));
	REQUIRE_THAT(fade.inDuration(), Catch::Matchers::WithinAbs(0.5f, 0.001f));
}

TEST_CASE("FadeTransition - out alpha goes 0 to 1", "[scene][transition]")
{
	FadeTransition fade{0.5f};
	REQUIRE_THAT(fade.computeOutAlpha(0.0f), Catch::Matchers::WithinAbs(0.0f, 0.001f));
	REQUIRE_THAT(fade.computeOutAlpha(0.5f), Catch::Matchers::WithinAbs(0.5f, 0.001f));
	REQUIRE_THAT(fade.computeOutAlpha(1.0f), Catch::Matchers::WithinAbs(1.0f, 0.001f));
}

TEST_CASE("FadeTransition - in alpha goes 1 to 0", "[scene][transition]")
{
	FadeTransition fade{0.5f};
	REQUIRE_THAT(fade.computeInAlpha(0.0f), Catch::Matchers::WithinAbs(1.0f, 0.001f));
	REQUIRE_THAT(fade.computeInAlpha(0.5f), Catch::Matchers::WithinAbs(0.5f, 0.001f));
	REQUIRE_THAT(fade.computeInAlpha(1.0f), Catch::Matchers::WithinAbs(0.0f, 0.001f));
}

TEST_CASE("FadeTransition - negative duration clamped to zero", "[scene][transition]")
{
	FadeTransition fade{-1.0f};
	REQUIRE_THAT(fade.totalDuration(), Catch::Matchers::WithinAbs(0.0f, 0.001f));
}

// ── SlideTransition ─────────────────────────────────────

TEST_CASE("SlideTransition - out offset slides left", "[scene][transition]")
{
	SlideTransition slide{0.3f, SlideDirection::Left};
	auto [x, y] = slide.computeOutOffset(1.0f, 800.0f, 600.0f);
	REQUIRE_THAT(x, Catch::Matchers::WithinAbs(-800.0f, 0.1f));
	REQUIRE_THAT(y, Catch::Matchers::WithinAbs(0.0f, 0.1f));
}

TEST_CASE("SlideTransition - in offset slides from right", "[scene][transition]")
{
	SlideTransition slide{0.3f, SlideDirection::Left};
	// progress=0 => new scene at full offset (right side)
	auto [x0, y0] = slide.computeInOffset(0.0f, 800.0f, 600.0f);
	REQUIRE_THAT(x0, Catch::Matchers::WithinAbs(800.0f, 0.1f));

	// progress=1 => new scene at position 0
	auto [x1, y1] = slide.computeInOffset(1.0f, 800.0f, 600.0f);
	REQUIRE_THAT(x1, Catch::Matchers::WithinAbs(0.0f, 0.1f));
}

TEST_CASE("SlideTransition - vertical slide", "[scene][transition]")
{
	SlideTransition slide{0.3f, SlideDirection::Up};
	auto [x, y] = slide.computeOutOffset(1.0f, 800.0f, 600.0f);
	REQUIRE_THAT(x, Catch::Matchers::WithinAbs(0.0f, 0.1f));
	REQUIRE_THAT(y, Catch::Matchers::WithinAbs(-600.0f, 0.1f));
}

TEST_CASE("SlideTransition - down direction", "[scene][transition]")
{
	SlideTransition slide{0.3f, SlideDirection::Down};
	auto [x, y] = slide.computeOutOffset(0.5f, 800.0f, 600.0f);
	REQUIRE_THAT(x, Catch::Matchers::WithinAbs(0.0f, 0.1f));
	REQUIRE_THAT(y, Catch::Matchers::WithinAbs(300.0f, 0.1f));
}

TEST_CASE("SlideTransition - right direction", "[scene][transition]")
{
	SlideTransition slide{0.3f, SlideDirection::Right};
	auto [x, y] = slide.computeOutOffset(1.0f, 800.0f, 600.0f);
	REQUIRE_THAT(x, Catch::Matchers::WithinAbs(800.0f, 0.1f));
}

// ── WipeTransition ──────────────────────────────────────

TEST_CASE("WipeTransition - coverage goes 0 to 1 during out", "[scene][transition]")
{
	WipeTransition wipe{0.4f, WipeDirection::Horizontal};
	REQUIRE_THAT(wipe.computeOutAlpha(0.0f), Catch::Matchers::WithinAbs(0.0f, 0.001f));
	REQUIRE_THAT(wipe.computeOutAlpha(1.0f), Catch::Matchers::WithinAbs(1.0f, 0.001f));
}

TEST_CASE("WipeTransition - coverage goes 1 to 0 during in", "[scene][transition]")
{
	WipeTransition wipe{0.4f, WipeDirection::Vertical};
	REQUIRE_THAT(wipe.computeInAlpha(0.0f), Catch::Matchers::WithinAbs(1.0f, 0.001f));
	REQUIRE_THAT(wipe.computeInAlpha(1.0f), Catch::Matchers::WithinAbs(0.0f, 0.001f));
	REQUIRE(wipe.direction() == WipeDirection::Vertical);
}

// ── TransitionManager ───────────────────────────────────

TEST_CASE("TransitionManager - initial state is idle", "[scene][transition]")
{
	TransitionManager tm;
	REQUIRE(tm.phase() == TransitionPhase::Idle);
	REQUIRE_FALSE(tm.isActive());
	REQUIRE_FALSE(tm.isComplete());
	REQUIRE(tm.getAlpha() == 0.0f);
}

TEST_CASE("TransitionManager - start begins out phase", "[scene][transition]")
{
	TransitionManager tm;
	tm.start(std::make_unique<FadeTransition>(0.5f));

	REQUIRE(tm.phase() == TransitionPhase::Out);
	REQUIRE(tm.isActive());
	REQUIRE_THAT(tm.getProgress(), Catch::Matchers::WithinAbs(0.0f, 0.001f));
}

TEST_CASE("TransitionManager - progresses through phases", "[scene][transition]")
{
	TransitionManager tm;
	tm.start(std::make_unique<FadeTransition>(0.5f));

	// Out phase: 0.25s of 0.5s = 50% progress
	tm.update(0.25f);
	REQUIRE(tm.phase() == TransitionPhase::Out);
	REQUIRE_THAT(tm.getProgress(), Catch::Matchers::WithinAbs(0.5f, 0.01f));
	REQUIRE_THAT(tm.getAlpha(), Catch::Matchers::WithinAbs(0.5f, 0.01f));

	// Complete out phase => transition to In
	tm.update(0.3f);
	REQUIRE(tm.phase() == TransitionPhase::In);

	// Midpoint should be consumable
	REQUIRE(tm.consumeMidpoint());
	REQUIRE_FALSE(tm.consumeMidpoint()); // 2回目はfalse

	// Complete in phase
	tm.update(0.6f);
	REQUIRE(tm.phase() == TransitionPhase::Complete);
	REQUIRE(tm.isComplete());
	REQUIRE_FALSE(tm.isActive());
}

TEST_CASE("TransitionManager - reset returns to idle", "[scene][transition]")
{
	TransitionManager tm;
	tm.start(std::make_unique<FadeTransition>(0.5f));
	tm.update(0.25f);
	tm.reset();

	REQUIRE(tm.phase() == TransitionPhase::Idle);
	REQUIRE_FALSE(tm.isActive());
	REQUIRE(tm.transition() == nullptr);
}

TEST_CASE("TransitionManager - offset with slide transition", "[scene][transition]")
{
	TransitionManager tm;
	tm.start(std::make_unique<SlideTransition>(0.5f, SlideDirection::Left));

	tm.update(0.25f); // 50% of out phase
	auto [x, y] = tm.getOffset(800.0f, 600.0f);
	REQUIRE_THAT(x, Catch::Matchers::WithinAbs(-400.0f, 5.0f));
	REQUIRE_THAT(y, Catch::Matchers::WithinAbs(0.0f, 0.1f));
}

TEST_CASE("TransitionManager - zero duration completes immediately", "[scene][transition]")
{
	TransitionManager tm;
	tm.start(std::make_unique<FadeTransition>(0.0f));

	// Any update should complete the out phase
	tm.update(0.01f);
	REQUIRE(tm.phase() == TransitionPhase::In);

	tm.update(0.01f);
	REQUIRE(tm.phase() == TransitionPhase::Complete);
}
