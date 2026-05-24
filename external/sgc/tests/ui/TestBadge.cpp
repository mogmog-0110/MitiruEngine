/// @file TestBadge.cpp
/// @brief バッジ配置ユーティリティのテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/ui/Badge.hpp"

using namespace sgc;
using namespace sgc::ui;

static const Rectf BASE_RECT{{100.0f, 100.0f}, {200.0f, 40.0f}};
static const Vec2f BADGE_SIZE{24.0f, 24.0f};

// ── TopRight anchor ────────────────────────────────────

TEST_CASE("evaluateBadge TopRight no offset centers on corner", "[ui][badge]")
{
	const auto badge = evaluateBadge(BASE_RECT, Anchor::TopRight, BADGE_SIZE);
	// アンカーポイント = (300, 100)、バッジ中心 = アンカーポイント
	REQUIRE(badge.x() == 300.0f - 12.0f);
	REQUIRE(badge.y() == 100.0f - 12.0f);
	REQUIRE(badge.width() == 24.0f);
	REQUIRE(badge.height() == 24.0f);
}

TEST_CASE("evaluateBadge TopRight with offset shifts from corner", "[ui][badge]")
{
	const auto badge = evaluateBadge(BASE_RECT, Anchor::TopRight, BADGE_SIZE, {4.0f, -4.0f});
	REQUIRE(badge.x() == 300.0f - 12.0f + 4.0f);
	REQUIRE(badge.y() == 100.0f - 12.0f - 4.0f);
	REQUIRE(badge.width() == 24.0f);
	REQUIRE(badge.height() == 24.0f);
}

// ── Center anchor ──────────────────────────────────────

TEST_CASE("evaluateBadge Center anchor centers on base rect center", "[ui][badge]")
{
	const auto badge = evaluateBadge(BASE_RECT, Anchor::Center, BADGE_SIZE);
	// アンカーポイント = (200, 120)
	REQUIRE(badge.x() == 200.0f - 12.0f);
	REQUIRE(badge.y() == 120.0f - 12.0f);
}

// ── TopLeft anchor ─────────────────────────────────────

TEST_CASE("evaluateBadge TopLeft anchor centers on top-left corner", "[ui][badge]")
{
	const auto badge = evaluateBadge(BASE_RECT, Anchor::TopLeft, BADGE_SIZE);
	// アンカーポイント = (100, 100)
	REQUIRE(badge.x() == 100.0f - 12.0f);
	REQUIRE(badge.y() == 100.0f - 12.0f);
}

// ── BottomRight anchor ─────────────────────────────────

TEST_CASE("evaluateBadge BottomRight anchor centers on bottom-right corner", "[ui][badge]")
{
	const auto badge = evaluateBadge(BASE_RECT, Anchor::BottomRight, BADGE_SIZE);
	// アンカーポイント = (300, 140)
	REQUIRE(badge.x() == 300.0f - 12.0f);
	REQUIRE(badge.y() == 140.0f - 12.0f);
}

// ── badgeFromText ──────────────────────────────────────

TEST_CASE("badgeFromText computes size from text dimensions and padding", "[ui][badge]")
{
	const auto badge = badgeFromText(
		BASE_RECT, Anchor::TopRight, 30.0f, 16.0f, 6.0f, 4.0f, {4.0f, -4.0f});
	// バッジサイズ = (30 + 6*2, 16 + 4*2) = (42, 24)
	REQUIRE(badge.width() == 42.0f);
	REQUIRE(badge.height() == 24.0f);
	// 位置 = anchorPoint(TopRight) - size/2 + offset
	REQUIRE(badge.x() == 300.0f - 21.0f + 4.0f);
	REQUIRE(badge.y() == 100.0f - 12.0f - 4.0f);
}

TEST_CASE("badgeFromText with zero text width uses padding only for width", "[ui][badge]")
{
	const auto badge = badgeFromText(
		BASE_RECT, Anchor::TopRight, 0.0f, 16.0f, 6.0f, 4.0f);
	// バッジサイズ = (0 + 12, 16 + 8) = (12, 24)
	REQUIRE(badge.width() == 12.0f);
	REQUIRE(badge.height() == 24.0f);
}

// ── constexpr ──────────────────────────────────────────

TEST_CASE("evaluateBadge is constexpr evaluable", "[ui][badge]")
{
	constexpr Rectf rect{{0.0f, 0.0f}, {100.0f, 50.0f}};
	constexpr auto badge = evaluateBadge(rect, Anchor::TopRight, {20.0f, 20.0f});
	REQUIRE(badge.x() == 100.0f - 10.0f);
	REQUIRE(badge.y() == 0.0f - 10.0f);
}
