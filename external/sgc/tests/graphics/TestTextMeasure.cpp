/// @file TestTextMeasure.cpp
/// @brief ITextMeasure interface tests with mock implementation

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <string_view>

#include "sgc/graphics/ITextMeasure.hpp"

using namespace sgc;
using Catch::Approx;

namespace
{

/// @brief テスト用のモックテキスト計測
///
/// fontSize に比例するサイズを返す単純実装。
/// 幅 = 文字数 * fontSize * 0.6f、高さ = fontSize
class MockTextMeasure : public ITextMeasure
{
public:
	[[nodiscard]] Vec2f measure(
		std::string_view text, float fontSize) const override
	{
		const float width = static_cast<float>(text.size()) * fontSize * 0.6f;
		return {width, fontSize};
	}

	[[nodiscard]] float lineHeight(float fontSize) const override
	{
		return fontSize;
	}
};

} // anonymous namespace

// ── measure tests ────────────────────────────────────────

TEST_CASE("MockTextMeasure measure returns proportional size", "[graphics][textmeasure]")
{
	MockTextMeasure tm;
	auto size = tm.measure("Hello", 20.0f);
	REQUIRE(size.x == Approx(5.0f * 20.0f * 0.6f));  // 60.0
	REQUIRE(size.y == Approx(20.0f));
}

TEST_CASE("MockTextMeasure measure with different font size", "[graphics][textmeasure]")
{
	MockTextMeasure tm;
	auto size = tm.measure("AB", 32.0f);
	REQUIRE(size.x == Approx(2.0f * 32.0f * 0.6f));  // 38.4
	REQUIRE(size.y == Approx(32.0f));
}

TEST_CASE("MockTextMeasure measure empty string returns zero width", "[graphics][textmeasure]")
{
	MockTextMeasure tm;
	auto size = tm.measure("", 24.0f);
	REQUIRE(size.x == Approx(0.0f));
	REQUIRE(size.y == Approx(24.0f));
}

// ── lineHeight tests ─────────────────────────────────────

TEST_CASE("MockTextMeasure lineHeight returns fontSize", "[graphics][textmeasure]")
{
	MockTextMeasure tm;
	REQUIRE(tm.lineHeight(16.0f) == Approx(16.0f));
	REQUIRE(tm.lineHeight(48.0f) == Approx(48.0f));
}

// ── Polymorphism test ────────────────────────────────────

TEST_CASE("ITextMeasure polymorphic dispatch works", "[graphics][textmeasure]")
{
	MockTextMeasure mock;
	ITextMeasure& tm = mock;

	auto size = tm.measure("Test", 10.0f);
	REQUIRE(size.x == Approx(4.0f * 10.0f * 0.6f));  // 24.0
	REQUIRE(size.y == Approx(10.0f));

	REQUIRE(tm.lineHeight(10.0f) == Approx(10.0f));
}

// ── Single character test ────────────────────────────────

TEST_CASE("MockTextMeasure single character measurement", "[graphics][textmeasure]")
{
	MockTextMeasure tm;
	auto size = tm.measure("X", 40.0f);
	REQUIRE(size.x == Approx(1.0f * 40.0f * 0.6f));  // 24.0
	REQUIRE(size.y == Approx(40.0f));
}

// ── Long text test ───────────────────────────────────────

TEST_CASE("MockTextMeasure long text measurement", "[graphics][textmeasure]")
{
	MockTextMeasure tm;
	auto size = tm.measure("Hello, World!", 16.0f);
	REQUIRE(size.x == Approx(13.0f * 16.0f * 0.6f));  // 124.8
	REQUIRE(size.y == Approx(16.0f));
}
