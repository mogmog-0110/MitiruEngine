/// @file TestColor.cpp
/// @brief Color.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/types/Color.hpp"

using Catch::Approx;

// ── Construction ──────────────────────────────────────────────────

TEST_CASE("Color default construction is black opaque", "[types][color]")
{
	constexpr sgc::Colorf c;
	STATIC_REQUIRE(c.r == 0.0f);
	STATIC_REQUIRE(c.g == 0.0f);
	STATIC_REQUIRE(c.b == 0.0f);
	STATIC_REQUIRE(c.a == 1.0f);
}

TEST_CASE("Color RGBA construction", "[types][color]")
{
	constexpr sgc::Colorf c{0.5f, 0.6f, 0.7f, 0.8f};
	STATIC_REQUIRE(c.r == 0.5f);
	STATIC_REQUIRE(c.g == 0.6f);
	STATIC_REQUIRE(c.b == 0.7f);
	STATIC_REQUIRE(c.a == 0.8f);
}

TEST_CASE("Color RGB construction defaults alpha to 1", "[types][color]")
{
	constexpr sgc::Colorf c{0.1f, 0.2f, 0.3f};
	STATIC_REQUIRE(c.a == 1.0f);
}

// ── RGBA8 conversion ─────────────────────────────────────────────

TEST_CASE("Color fromRGBA8 and toRGBA8 roundtrip", "[types][color]")
{
	const auto c = sgc::Colorf::fromRGBA8(128, 200, 50, 255);
	const auto packed = c.toRGBA8();
	REQUIRE(packed.r == 128);
	REQUIRE(packed.g == 200);
	REQUIRE(packed.b == 50);
	REQUIRE(packed.a == 255);
}

TEST_CASE("Color fromRGBA8 maps 0-255 to 0-1 range", "[types][color]")
{
	const auto c = sgc::Colorf::fromRGBA8(0, 255, 128, 64);
	REQUIRE(c.r == Approx(0.0f));
	REQUIRE(c.g == Approx(1.0f));
	REQUIRE(c.b == Approx(128.0f / 255.0f));
	REQUIRE(c.a == Approx(64.0f / 255.0f));
}

// ── Hex conversion ───────────────────────────────────────────────

TEST_CASE("Color fromHex and toHex roundtrip", "[types][color]")
{
	constexpr auto c = sgc::Colorf::fromHex(0xFF8040FF);
	constexpr auto hex = c.toHex();
	STATIC_REQUIRE(hex == 0xFF8040FF);
}

TEST_CASE("Color fromHex pure red", "[types][color]")
{
	const auto c = sgc::Colorf::fromHex(0xFF0000FF);
	REQUIRE(c.r == Approx(1.0f));
	REQUIRE(c.g == Approx(0.0f));
	REQUIRE(c.b == Approx(0.0f));
	REQUIRE(c.a == Approx(1.0f));
}

// ── HSV conversion ───────────────────────────────────────────────

TEST_CASE("Color fromHSV pure red", "[types][color]")
{
	const auto c = sgc::Colorf::fromHSV(0.0f, 1.0f, 1.0f);
	REQUIRE(c.r == Approx(1.0f));
	REQUIRE(c.g == Approx(0.0f));
	REQUIRE(c.b == Approx(0.0f));
}

TEST_CASE("Color fromHSV pure green", "[types][color]")
{
	const auto c = sgc::Colorf::fromHSV(120.0f, 1.0f, 1.0f);
	REQUIRE(c.r == Approx(0.0f));
	REQUIRE(c.g == Approx(1.0f));
	REQUIRE(c.b == Approx(0.0f));
}

TEST_CASE("Color toHSV roundtrip", "[types][color]")
{
	const auto original = sgc::Colorf{0.8f, 0.2f, 0.5f};
	const auto hsv = original.toHSV();
	const auto restored = sgc::Colorf::fromHSV(hsv.h, hsv.s, hsv.v);
	REQUIRE(restored.r == Approx(original.r).margin(0.01f));
	REQUIRE(restored.g == Approx(original.g).margin(0.01f));
	REQUIRE(restored.b == Approx(original.b).margin(0.01f));
}

// ── Component operations ─────────────────────────────────────────

TEST_CASE("Color withAlpha creates new color", "[types][color]")
{
	constexpr auto c = sgc::Colorf::red().withAlpha(0.5f);
	STATIC_REQUIRE(c.r == 1.0f);
	STATIC_REQUIRE(c.a == 0.5f);
}

TEST_CASE("Color inverted", "[types][color]")
{
	constexpr auto c = sgc::Colorf{0.2f, 0.3f, 0.7f, 0.9f}.inverted();
	REQUIRE(c.r == Approx(0.8f));
	REQUIRE(c.g == Approx(0.7f));
	REQUIRE(c.b == Approx(0.3f));
	STATIC_REQUIRE(c.a == 0.9f);  // アルファは保持
}

TEST_CASE("Color premultiplied", "[types][color]")
{
	constexpr auto c = sgc::Colorf{1.0f, 0.5f, 0.0f, 0.5f}.premultiplied();
	STATIC_REQUIRE(c.r == 0.5f);
	STATIC_REQUIRE(c.g == 0.25f);
	STATIC_REQUIRE(c.b == 0.0f);
	STATIC_REQUIRE(c.a == 0.5f);
}

// ── Interpolation ────────────────────────────────────────────────

TEST_CASE("Color lerp halfway", "[types][color]")
{
	constexpr auto result = sgc::Colorf::black().lerp(sgc::Colorf::white(), 0.5f);
	STATIC_REQUIRE(result.r == 0.5f);
	STATIC_REQUIRE(result.g == 0.5f);
	STATIC_REQUIRE(result.b == 0.5f);
}

TEST_CASE("Color lerpHSV interpolates through hue", "[types][color]")
{
	const auto result = sgc::Colorf::red().lerpHSV(sgc::Colorf::green(), 0.5f);
	// 赤(0) → 緑(120) の中間は 60度 = 黄色
	REQUIRE(result.r == Approx(1.0f).margin(0.05f));
	REQUIRE(result.g == Approx(1.0f).margin(0.05f));
	REQUIRE(result.b == Approx(0.0f).margin(0.05f));
}

// ── Constants ────────────────────────────────────────────────────

TEST_CASE("Color constants are correct", "[types][color]")
{
	STATIC_REQUIRE(sgc::Colorf::white() == sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f});
	STATIC_REQUIRE(sgc::Colorf::black() == sgc::Colorf{0.0f, 0.0f, 0.0f, 1.0f});
	STATIC_REQUIRE(sgc::Colorf::red() == sgc::Colorf{1.0f, 0.0f, 0.0f, 1.0f});
	STATIC_REQUIRE(sgc::Colorf::green() == sgc::Colorf{0.0f, 1.0f, 0.0f, 1.0f});
	STATIC_REQUIRE(sgc::Colorf::blue() == sgc::Colorf{0.0f, 0.0f, 1.0f, 1.0f});
	STATIC_REQUIRE(sgc::Colorf::yellow() == sgc::Colorf{1.0f, 1.0f, 0.0f, 1.0f});
	STATIC_REQUIRE(sgc::Colorf::cyan() == sgc::Colorf{0.0f, 1.0f, 1.0f, 1.0f});
	STATIC_REQUIRE(sgc::Colorf::magenta() == sgc::Colorf{1.0f, 0.0f, 1.0f, 1.0f});
	STATIC_REQUIRE(sgc::Colorf::transparent() == sgc::Colorf{0.0f, 0.0f, 0.0f, 0.0f});
}

// ── Type conversion ──────────────────────────────────────────────

TEST_CASE("Color float to double conversion", "[types][color]")
{
	constexpr sgc::Colorf cf{0.5f, 0.6f, 0.7f, 0.8f};
	constexpr sgc::Colord cd{cf};
	REQUIRE(cd.r == Approx(0.5));
	REQUIRE(cd.g == Approx(0.6));
}

TEST_CASE("Color double to float roundtrip", "[types][color]")
{
	constexpr sgc::Colord cd{0.25, 0.5, 0.75, 1.0};
	constexpr sgc::Colorf cf{cd};
	REQUIRE(cf.r == Approx(0.25f));
	REQUIRE(cf.g == Approx(0.5f));
	REQUIRE(cf.b == Approx(0.75f));
	REQUIRE(cf.a == Approx(1.0f));
}

// ── withRed/Green/Blue ──────────────────────────────────

TEST_CASE("Color withRed creates new color", "[types][color]")
{
	constexpr auto c = sgc::Colorf{0.1f, 0.2f, 0.3f, 0.4f}.withRed(0.9f);
	STATIC_REQUIRE(c.r == 0.9f);
	STATIC_REQUIRE(c.g == 0.2f);
	STATIC_REQUIRE(c.b == 0.3f);
	STATIC_REQUIRE(c.a == 0.4f);
}

TEST_CASE("Color withGreen creates new color", "[types][color]")
{
	constexpr auto c = sgc::Colorf{0.1f, 0.2f, 0.3f, 0.4f}.withGreen(0.9f);
	STATIC_REQUIRE(c.r == 0.1f);
	STATIC_REQUIRE(c.g == 0.9f);
	STATIC_REQUIRE(c.b == 0.3f);
	STATIC_REQUIRE(c.a == 0.4f);
}

TEST_CASE("Color withBlue creates new color", "[types][color]")
{
	constexpr auto c = sgc::Colorf{0.1f, 0.2f, 0.3f, 0.4f}.withBlue(0.9f);
	STATIC_REQUIRE(c.r == 0.1f);
	STATIC_REQUIRE(c.g == 0.2f);
	STATIC_REQUIRE(c.b == 0.9f);
	STATIC_REQUIRE(c.a == 0.4f);
}

// ── lerp boundary ───────────────────────────────────────

TEST_CASE("Color lerp at t=0 returns start", "[types][color]")
{
	constexpr auto result = sgc::Colorf::red().lerp(sgc::Colorf::blue(), 0.0f);
	STATIC_REQUIRE(result.r == 1.0f);
	STATIC_REQUIRE(result.g == 0.0f);
	STATIC_REQUIRE(result.b == 0.0f);
}

TEST_CASE("Color lerp at t=1 returns end", "[types][color]")
{
	constexpr auto result = sgc::Colorf::red().lerp(sgc::Colorf::blue(), 1.0f);
	STATIC_REQUIRE(result.r == 0.0f);
	STATIC_REQUIRE(result.g == 0.0f);
	STATIC_REQUIRE(result.b == 1.0f);
}

// ── HSV grayscale ───────────────────────────────────────

TEST_CASE("Color fromHSV grayscale has zero saturation", "[types][color]")
{
	const auto gray = sgc::Colorf::fromHSV(0.0f, 0.0f, 0.5f);
	REQUIRE(gray.r == Approx(0.5f));
	REQUIRE(gray.g == Approx(0.5f));
	REQUIRE(gray.b == Approx(0.5f));
}

// ── brighten / darken ───────────────────────────────────

TEST_CASE("Color brighten increases value", "[types][color]")
{
	const auto dark = sgc::Colorf::fromHSV(0.0f, 1.0f, 0.5f);
	const auto bright = dark.brighten(0.3f);
	auto hsvOrig = dark.toHSV();
	auto hsvBright = bright.toHSV();
	REQUIRE(hsvBright.v > hsvOrig.v);
	REQUIRE(hsvBright.v == Approx(0.8f).margin(0.05f));
}

TEST_CASE("Color darken decreases value", "[types][color]")
{
	const auto bright = sgc::Colorf::fromHSV(0.0f, 1.0f, 0.8f);
	const auto dark = bright.darken(0.3f);
	auto hsvDark = dark.toHSV();
	REQUIRE(hsvDark.v == Approx(0.5f).margin(0.05f));
}

// ── saturate / desaturate ───────────────────────────────

TEST_CASE("Color saturate increases saturation", "[types][color]")
{
	const auto muted = sgc::Colorf::fromHSV(120.0f, 0.3f, 1.0f);
	const auto vivid = muted.saturate(0.5f);
	auto hsvVivid = vivid.toHSV();
	REQUIRE(hsvVivid.s == Approx(0.8f).margin(0.05f));
}

TEST_CASE("Color desaturate decreases saturation", "[types][color]")
{
	const auto vivid = sgc::Colorf::fromHSV(120.0f, 0.8f, 1.0f);
	const auto muted = vivid.desaturate(0.5f);
	auto hsvMuted = muted.toHSV();
	REQUIRE(hsvMuted.s == Approx(0.3f).margin(0.05f));
}

// ── complement ──────────────────────────────────────────

TEST_CASE("Color complement shifts hue by 180", "[types][color]")
{
	const auto red = sgc::Colorf::red();
	const auto comp = red.complement();
	auto hsv = comp.toHSV();
	REQUIRE(hsv.h == Approx(180.0f).margin(1.0f));
}
