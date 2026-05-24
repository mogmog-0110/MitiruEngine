#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/vn/TextEffects.hpp"

using namespace sgc::vn;
using Catch::Approx;

TEST_CASE("TextEffects - Plain text returns single segment", "[vn][texteffects]")
{
	auto segments = parseTextEffects("Hello, world!");
	REQUIRE(segments.size() == 1);
	REQUIRE(segments[0].text == "Hello, world!");
	REQUIRE(segments[0].effect == TextEffect::None);
	REQUIRE(segments[0].speedMultiplier == Approx(1.0f));
}

TEST_CASE("TextEffects - Shake tag creates shake effect segment", "[vn][texteffects]")
{
	auto segments = parseTextEffects("{shake}Earthquake!{/shake}");
	REQUIRE(segments.size() == 1);
	REQUIRE(segments[0].text == "Earthquake!");
	REQUIRE(segments[0].effect == TextEffect::Shake);
}

TEST_CASE("TextEffects - Wave tag creates wave effect segment", "[vn][texteffects]")
{
	auto segments = parseTextEffects("{wave}Flowing{/wave}");
	REQUIRE(segments.size() == 1);
	REQUIRE(segments[0].text == "Flowing");
	REQUIRE(segments[0].effect == TextEffect::Wave);
}

TEST_CASE("TextEffects - Fade tag creates fade effect segment", "[vn][texteffects]")
{
	auto segments = parseTextEffects("{fade}Vanish{/fade}");
	REQUIRE(segments.size() == 1);
	REQUIRE(segments[0].text == "Vanish");
	REQUIRE(segments[0].effect == TextEffect::Fade);
}

TEST_CASE("TextEffects - Color tag sets segment color", "[vn][texteffects]")
{
	auto segments = parseTextEffects("{color=FF0000}Red text{/color}");
	REQUIRE(segments.size() == 1);
	REQUIRE(segments[0].text == "Red text");
	REQUIRE(segments[0].color.r == Approx(1.0f));
	REQUIRE(segments[0].color.g == Approx(0.0f));
	REQUIRE(segments[0].color.b == Approx(0.0f));
}

TEST_CASE("TextEffects - Speed tag sets multiplier", "[vn][texteffects]")
{
	auto segments = parseTextEffects("{speed=2.5}Fast text{/speed}");
	REQUIRE(segments.size() == 1);
	REQUIRE(segments[0].text == "Fast text");
	REQUIRE(segments[0].speedMultiplier == Approx(2.5f));
}

TEST_CASE("TextEffects - Pause tag sets pause duration", "[vn][texteffects]")
{
	auto segments = parseTextEffects("Before{pause=1.5}After");
	REQUIRE(segments.size() == 2);
	REQUIRE(segments[0].text == "Before");
	REQUIRE(segments[0].pauseDuration == Approx(0.0f));
	REQUIRE(segments[1].text == "After");
	REQUIRE(segments[1].pauseDuration == Approx(1.5f));
}

TEST_CASE("TextEffects - Sequential tags", "[vn][texteffects]")
{
	auto segments = parseTextEffects("Normal {shake}shaky{/shake} more");
	REQUIRE(segments.size() == 3);
	REQUIRE(segments[0].text == "Normal ");
	REQUIRE(segments[0].effect == TextEffect::None);
	REQUIRE(segments[1].text == "shaky");
	REQUIRE(segments[1].effect == TextEffect::Shake);
	REQUIRE(segments[2].text == " more");
	REQUIRE(segments[2].effect == TextEffect::None);
}

TEST_CASE("TextEffects - Malformed tag treated as plain text", "[vn][texteffects]")
{
	auto segments = parseTextEffects("{unknown}text{/unknown}");
	// {unknown} is not recognized, so '{' is plain text
	REQUIRE_FALSE(segments.empty());
	// The text should contain the original characters
	std::string combined;
	for (const auto& seg : segments)
	{
		combined += seg.text;
	}
	REQUIRE(combined == "{unknown}text{/unknown}");
}

TEST_CASE("TextEffects - Empty input returns empty segments", "[vn][texteffects]")
{
	auto segments = parseTextEffects("");
	REQUIRE(segments.empty());
}

TEST_CASE("TextEffects - Unclosed brace treated as plain text", "[vn][texteffects]")
{
	auto segments = parseTextEffects("Hello {world");
	REQUIRE(segments.size() == 1);
	REQUIRE(segments[0].text == "Hello {world");
}
