/// @file TestTextLayout.cpp
/// @brief テキストレイアウト計算ユーティリティのテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/ui/TextLayout.hpp"
#include "sgc/graphics/ITextMeasure.hpp"

using namespace sgc;
using namespace sgc::ui;
using Catch::Matchers::WithinAbs;

namespace
{

/// @brief テスト用のモックテキスト計測
///
/// 幅 = 文字数 * fontSize * 0.6, 高さ = fontSize
/// 行の高さ = fontSize * 1.2
struct MockTextMeasure : sgc::ITextMeasure
{
	sgc::Vec2f measure(std::string_view text, float fontSize) const override
	{
		return {static_cast<float>(text.size()) * fontSize * 0.6f, fontSize};
	}

	float lineHeight(float fontSize) const override
	{
		return fontSize * 1.2f;
	}
};

} // namespace

static const MockTextMeasure MEASURE;

// ── textRect ──────────────────────────────────────────

TEST_CASE("textRect returns rect at origin with text size plus padding", "[ui][textlayout]")
{
	// "Hello" = 5 chars, fontSize=20 => width=5*20*0.6=60, height=20
	// padding uniform(8) => w=60+16=76, h=20+16=36
	auto rect = textRect(MEASURE, "Hello", 20.0f, Margin::uniform(8.0f));
	REQUIRE_THAT(rect.x(), WithinAbs(0.0f, 0.01f));
	REQUIRE_THAT(rect.y(), WithinAbs(0.0f, 0.01f));
	REQUIRE_THAT(rect.width(), WithinAbs(76.0f, 0.01f));
	REQUIRE_THAT(rect.height(), WithinAbs(36.0f, 0.01f));
}

TEST_CASE("textRect with zero padding returns text size only", "[ui][textlayout]")
{
	// "AB" = 2 chars, fontSize=10 => width=2*10*0.6=12, height=10
	auto rect = textRect(MEASURE, "AB", 10.0f);
	REQUIRE_THAT(rect.width(), WithinAbs(12.0f, 0.01f));
	REQUIRE_THAT(rect.height(), WithinAbs(10.0f, 0.01f));
}

// ── textRectAt ────────────────────────────────────────

TEST_CASE("textRectAt places rect at specified position", "[ui][textlayout]")
{
	// "OK" = 2 chars, fontSize=20 => width=24, height=20
	auto rect = textRectAt(MEASURE, "OK", 20.0f, {100.0f, 200.0f});
	REQUIRE_THAT(rect.x(), WithinAbs(100.0f, 0.01f));
	REQUIRE_THAT(rect.y(), WithinAbs(200.0f, 0.01f));
	REQUIRE_THAT(rect.width(), WithinAbs(24.0f, 0.01f));
	REQUIRE_THAT(rect.height(), WithinAbs(20.0f, 0.01f));
}

// ── textRectCentered ──────────────────────────────────

TEST_CASE("textRectCentered centers rect on given point", "[ui][textlayout]")
{
	// "Test" = 4 chars, fontSize=20 => width=48, height=20
	// center=(200,300) => x=200-24=176, y=300-10=290
	auto rect = textRectCentered(MEASURE, "Test", 20.0f, {200.0f, 300.0f});
	REQUIRE_THAT(rect.x(), WithinAbs(176.0f, 0.01f));
	REQUIRE_THAT(rect.y(), WithinAbs(290.0f, 0.01f));
	REQUIRE_THAT(rect.width(), WithinAbs(48.0f, 0.01f));
	REQUIRE_THAT(rect.height(), WithinAbs(20.0f, 0.01f));
}

// ── buttonSizeFromText ────────────────────────────────

TEST_CASE("buttonSizeFromText returns text size plus padding", "[ui][textlayout]")
{
	// "Go" = 2 chars, fontSize=16 => width=19.2, height=16
	// hPadding=10, vPadding=5 => w=19.2+20=39.2, h=16+10=26
	auto size = buttonSizeFromText(MEASURE, "Go", 16.0f, 10.0f, 5.0f);
	REQUIRE_THAT(size.x, WithinAbs(39.2f, 0.01f));
	REQUIRE_THAT(size.y, WithinAbs(26.0f, 0.01f));
}

// ── labelBounds ───────────────────────────────────────

TEST_CASE("labelBounds TopLeft keeps position unchanged", "[ui][textlayout]")
{
	// "Hi" = 2 chars, fontSize=20 => width=24, height=20
	auto rect = labelBounds(MEASURE, "Hi", 20.0f, {50.0f, 60.0f}, Anchor::TopLeft);
	REQUIRE_THAT(rect.x(), WithinAbs(50.0f, 0.01f));
	REQUIRE_THAT(rect.y(), WithinAbs(60.0f, 0.01f));
}

TEST_CASE("labelBounds Center centers on position", "[ui][textlayout]")
{
	// "Hi" = 2 chars, fontSize=20 => width=24, height=20
	// center: x=100-12=88, y=100-10=90
	auto rect = labelBounds(MEASURE, "Hi", 20.0f, {100.0f, 100.0f}, Anchor::Center);
	REQUIRE_THAT(rect.x(), WithinAbs(88.0f, 0.01f));
	REQUIRE_THAT(rect.y(), WithinAbs(90.0f, 0.01f));
	REQUIRE_THAT(rect.width(), WithinAbs(24.0f, 0.01f));
	REQUIRE_THAT(rect.height(), WithinAbs(20.0f, 0.01f));
}

TEST_CASE("labelBounds BottomRight positions so bottom-right is at position", "[ui][textlayout]")
{
	// "Hi" = 2 chars, fontSize=20 => width=24, height=20
	// bottomRight: x=200-24=176, y=200-20=180
	auto rect = labelBounds(MEASURE, "Hi", 20.0f, {200.0f, 200.0f}, Anchor::BottomRight);
	REQUIRE_THAT(rect.x(), WithinAbs(176.0f, 0.01f));
	REQUIRE_THAT(rect.y(), WithinAbs(180.0f, 0.01f));
}

// ── fitTextInRect ─────────────────────────────────────

TEST_CASE("fitTextInRect finds max fitting font size", "[ui][textlayout]")
{
	// "Hello" = 5 chars, target 100x50
	// At fontSize=f: width=5*f*0.6=3f, height=f
	// Fits when 3f<=100 (f<=33.3) and f<=50 => max ~33
	const Rectf target{0.0f, 0.0f, 100.0f, 50.0f};
	float bestSize = fitTextInRect(MEASURE, "Hello", 48.0f, target);
	// 二分探索で0.5刻み => ~33付近
	REQUIRE(bestSize >= 32.0f);
	REQUIRE(bestSize <= 34.0f);
}

TEST_CASE("fitTextInRect returns minSize when text cannot fit", "[ui][textlayout]")
{
	// "Hello" at fontSize=1 => width=3, height=1 => fits in 2x2? width=3>2 => no
	// But minSize is 1.0 so it returns 1.0
	const Rectf target{0.0f, 0.0f, 2.0f, 2.0f};
	float bestSize = fitTextInRect(MEASURE, "Hello", 48.0f, target);
	REQUIRE_THAT(bestSize, WithinAbs(1.0f, 0.5f));
}

// ── multiLineHeight ───────────────────────────────────

TEST_CASE("multiLineHeight with 3 lines and spacing", "[ui][textlayout]")
{
	// lineHeight(20) = 24, 3 lines + 4px spacing
	// total = 24*3 + 4*2 = 72 + 8 = 80
	float h = multiLineHeight(MEASURE, 3, 20.0f, 4.0f);
	REQUIRE_THAT(h, WithinAbs(80.0f, 0.01f));
}

TEST_CASE("multiLineHeight with 0 lines returns 0", "[ui][textlayout]")
{
	float h = multiLineHeight(MEASURE, 0, 20.0f, 4.0f);
	REQUIRE_THAT(h, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("multiLineHeight with 1 line has no spacing", "[ui][textlayout]")
{
	// lineHeight(16) = 19.2, 1 line + spacing*(1-1)=0
	float h = multiLineHeight(MEASURE, 1, 16.0f, 10.0f);
	REQUIRE_THAT(h, WithinAbs(19.2f, 0.01f));
}
