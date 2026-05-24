/// @file TestToast.cpp
/// @brief トースト通知評価ユーティリティのテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/ui/Toast.hpp"

using namespace sgc;
using namespace sgc::ui;
using Catch::Matchers::WithinAbs;

// fadeIn=0.2, duration=2.0, fadeOut=0.5 => totalTime=2.7
static const ToastState DEFAULT_TOAST{0.0f, 2.0f, 0.2f, 0.5f};

// ── evaluateToast: before start ───────────────────────

TEST_CASE("evaluateToast returns inactive when elapsed is negative", "[ui][toast]")
{
	const ToastState state{-1.0f, 2.0f, 0.2f, 0.5f};
	auto result = evaluateToast(state);
	REQUIRE_FALSE(result.isActive);
	REQUIRE_FALSE(result.isFinished);
	REQUIRE_THAT(result.opacity, WithinAbs(0.0f, 0.01f));
}

// ── evaluateToast: fade-in ────────────────────────────

TEST_CASE("evaluateToast calculates opacity during fade-in", "[ui][toast]")
{
	// elapsed=0.1, fadeIn=0.2 => opacity = 0.1/0.2 = 0.5
	const ToastState state{0.1f, 2.0f, 0.2f, 0.5f};
	auto result = evaluateToast(state);
	REQUIRE(result.isActive);
	REQUIRE_FALSE(result.isFinished);
	REQUIRE_THAT(result.opacity, WithinAbs(0.5f, 0.01f));
}

// ── evaluateToast: full display ───────────────────────

TEST_CASE("evaluateToast returns full opacity during display phase", "[ui][toast]")
{
	// elapsed = fadeIn + duration/2 = 0.2 + 1.0 = 1.2
	const ToastState state{1.2f, 2.0f, 0.2f, 0.5f};
	auto result = evaluateToast(state);
	REQUIRE(result.isActive);
	REQUIRE_THAT(result.opacity, WithinAbs(1.0f, 0.01f));
}

// ── evaluateToast: fade-out ───────────────────────────

TEST_CASE("evaluateToast decreases opacity during fade-out", "[ui][toast]")
{
	// fadeOutStart = 0.2+2.0 = 2.2, elapsed=2.45 => fadeOutElapsed=0.25
	// opacity = 1.0 - 0.25/0.5 = 0.5
	const ToastState state{2.45f, 2.0f, 0.2f, 0.5f};
	auto result = evaluateToast(state);
	REQUIRE(result.isActive);
	REQUIRE_FALSE(result.isFinished);
	REQUIRE_THAT(result.opacity, WithinAbs(0.5f, 0.01f));
}

// ── evaluateToast: completed ──────────────────────────

TEST_CASE("evaluateToast returns finished after total time", "[ui][toast]")
{
	// totalTime = 0.2+2.0+0.5 = 2.7
	const ToastState state{3.0f, 2.0f, 0.2f, 0.5f};
	auto result = evaluateToast(state);
	REQUIRE(result.isFinished);
	REQUIRE_FALSE(result.isActive);
	REQUIRE_THAT(result.opacity, WithinAbs(0.0f, 0.01f));
}

// ── advanceToast: immutability ────────────────────────

TEST_CASE("advanceToast returns new state without modifying original", "[ui][toast]")
{
	const ToastState original{1.0f, 2.0f, 0.2f, 0.5f};
	const auto advanced = advanceToast(original, 0.016f);
	REQUIRE(original.elapsed == 1.0f);
	REQUIRE_THAT(advanced.elapsed, WithinAbs(1.016f, 0.001f));
	REQUIRE(advanced.duration == original.duration);
	REQUIRE(advanced.fadeIn == original.fadeIn);
	REQUIRE(advanced.fadeOut == original.fadeOut);
}

// ── advanceToast: elapsed increment ───────────────────

TEST_CASE("advanceToast increments elapsed by dt", "[ui][toast]")
{
	const ToastState state{0.5f, 2.0f, 0.2f, 0.5f};
	const auto result = advanceToast(state, 0.1f);
	REQUIRE_THAT(result.elapsed, WithinAbs(0.6f, 0.001f));
}

// ── toastPosition: TopCenter slot 0 ──────────────────

TEST_CASE("toastPosition TopCenter slot 0 is centered at top", "[ui][toast]")
{
	const Rectf screen{0.0f, 0.0f, 800.0f, 600.0f};
	const Vec2f toastSize{200.0f, 40.0f};
	auto pos = toastPosition(screen, toastSize, 0, Anchor::TopCenter, 8.0f);
	// x centered: 400 - 100 = 300
	REQUIRE_THAT(pos.x, WithinAbs(300.0f, 0.01f));
	// y at top: 0
	REQUIRE_THAT(pos.y, WithinAbs(0.0f, 0.01f));
}

// ── toastPosition: TopCenter slot 1 ──────────────────

TEST_CASE("toastPosition TopCenter slot 1 is offset by toast height plus spacing", "[ui][toast]")
{
	const Rectf screen{0.0f, 0.0f, 800.0f, 600.0f};
	const Vec2f toastSize{200.0f, 40.0f};
	auto pos = toastPosition(screen, toastSize, 1, Anchor::TopCenter, 8.0f);
	// y = 0 + 1*(40+8) = 48
	REQUIRE_THAT(pos.y, WithinAbs(48.0f, 0.01f));
}

// ── toastPosition: BottomRight slot 0 ─────────────────

TEST_CASE("toastPosition BottomRight slot 0 is right-aligned at bottom", "[ui][toast]")
{
	const Rectf screen{0.0f, 0.0f, 800.0f, 600.0f};
	const Vec2f toastSize{200.0f, 40.0f};
	auto pos = toastPosition(screen, toastSize, 0, Anchor::BottomRight, 8.0f);
	// x = 800 - 200 = 600
	REQUIRE_THAT(pos.x, WithinAbs(600.0f, 0.01f));
	// y = 600 - 40 = 560
	REQUIRE_THAT(pos.y, WithinAbs(560.0f, 0.01f));
}

// ── Zero fadeIn/fadeOut ────────────────────────────────

TEST_CASE("evaluateToast with zero fadeIn and fadeOut shows full opacity immediately", "[ui][toast]")
{
	const ToastState state{0.5f, 2.0f, 0.0f, 0.0f};
	auto result = evaluateToast(state);
	REQUIRE(result.isActive);
	REQUIRE_THAT(result.opacity, WithinAbs(1.0f, 0.01f));
}

// ── constexpr evaluability ────────────────────────────

TEST_CASE("evaluateToast is constexpr evaluable", "[ui][toast]")
{
	constexpr ToastState state{1.0f, 2.0f, 0.2f, 0.5f};
	constexpr auto result = evaluateToast(state);
	REQUIRE(result.isActive);
	REQUIRE_THAT(result.opacity, WithinAbs(1.0f, 0.01f));
}
