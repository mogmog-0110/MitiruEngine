/// @file TestTextRenderer.cpp
/// @brief ITextRenderer interface tests

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <string>
#include <vector>

#include "sgc/graphics/ITextRenderer.hpp"

using namespace sgc;
using Catch::Approx;

// ── Mock implementation ─────────────────────────────────

namespace
{

struct TextDrawCall
{
	std::string text;
	float fontSize;
	Vec2f pos;
	Colorf color;
	bool centered;
};

class MockTextRenderer : public ITextRenderer
{
public:
	std::vector<TextDrawCall> calls;

	void drawText(
		std::string_view text, float fontSize,
		const Vec2f& pos, const Colorf& color) override
	{
		calls.push_back({std::string(text), fontSize, pos, color, false});
	}

	void drawTextCentered(
		std::string_view text, float fontSize,
		const Vec2f& center, const Colorf& color) override
	{
		calls.push_back({std::string(text), fontSize, center, color, true});
	}
};

} // anonymous namespace

// ── Tests ───────────────────────────────────────────────

TEST_CASE("ITextRenderer drawText records call correctly", "[graphics][text]")
{
	MockTextRenderer renderer;
	renderer.drawText("Hello", 24.0f, {10.0f, 20.0f}, Colorf::white());

	REQUIRE(renderer.calls.size() == 1);
	REQUIRE(renderer.calls[0].text == "Hello");
	REQUIRE(renderer.calls[0].fontSize == Approx(24.0f));
	REQUIRE(renderer.calls[0].pos.x == Approx(10.0f));
	REQUIRE(renderer.calls[0].pos.y == Approx(20.0f));
	REQUIRE_FALSE(renderer.calls[0].centered);
}

TEST_CASE("ITextRenderer drawTextCentered records call correctly", "[graphics][text]")
{
	MockTextRenderer renderer;
	renderer.drawTextCentered("Title", 60.0f, {400.0f, 300.0f}, Colorf::white());

	REQUIRE(renderer.calls.size() == 1);
	REQUIRE(renderer.calls[0].text == "Title");
	REQUIRE(renderer.calls[0].fontSize == Approx(60.0f));
	REQUIRE(renderer.calls[0].pos.x == Approx(400.0f));
	REQUIRE(renderer.calls[0].pos.y == Approx(300.0f));
	REQUIRE(renderer.calls[0].centered);
}

TEST_CASE("ITextRenderer drawText with empty string", "[graphics][text]")
{
	MockTextRenderer renderer;
	renderer.drawText("", 12.0f, {0.0f, 0.0f}, Colorf::black());

	REQUIRE(renderer.calls.size() == 1);
	REQUIRE(renderer.calls[0].text.empty());
}

TEST_CASE("ITextRenderer drawText preserves font size", "[graphics][text]")
{
	MockTextRenderer renderer;
	renderer.drawText("A", 8.0f, {0.0f, 0.0f}, Colorf::white());
	renderer.drawText("B", 72.0f, {0.0f, 0.0f}, Colorf::white());

	REQUIRE(renderer.calls[0].fontSize == Approx(8.0f));
	REQUIRE(renderer.calls[1].fontSize == Approx(72.0f));
}

TEST_CASE("ITextRenderer drawText preserves color", "[graphics][text]")
{
	MockTextRenderer renderer;
	const Colorf red = Colorf::red();
	renderer.drawText("Red", 24.0f, {0.0f, 0.0f}, red);

	REQUIRE(renderer.calls[0].color.r == Approx(1.0f));
	REQUIRE(renderer.calls[0].color.g == Approx(0.0f));
	REQUIRE(renderer.calls[0].color.b == Approx(0.0f));
}

TEST_CASE("ITextRenderer multiple calls accumulate", "[graphics][text]")
{
	MockTextRenderer renderer;
	renderer.drawText("A", 12.0f, {0.0f, 0.0f}, Colorf::white());
	renderer.drawTextCentered("B", 24.0f, {100.0f, 100.0f}, Colorf::white());
	renderer.drawText("C", 36.0f, {200.0f, 200.0f}, Colorf::white());

	REQUIRE(renderer.calls.size() == 3);
	REQUIRE_FALSE(renderer.calls[0].centered);
	REQUIRE(renderer.calls[1].centered);
	REQUIRE_FALSE(renderer.calls[2].centered);
}

TEST_CASE("ITextRenderer used via base pointer", "[graphics][text]")
{
	MockTextRenderer mock;
	ITextRenderer* renderer = &mock;

	renderer->drawText("Test", 16.0f, {5.0f, 5.0f}, Colorf::white());

	REQUIRE(mock.calls.size() == 1);
	REQUIRE(mock.calls[0].text == "Test");
}

TEST_CASE("ITextRenderer drawTextCentered with various positions", "[graphics][text]")
{
	MockTextRenderer renderer;
	renderer.drawTextCentered("A", 24.0f, {0.0f, 0.0f}, Colorf::white());
	renderer.drawTextCentered("B", 24.0f, {-100.0f, 500.0f}, Colorf::white());

	REQUIRE(renderer.calls[0].pos.x == Approx(0.0f));
	REQUIRE(renderer.calls[1].pos.x == Approx(-100.0f));
	REQUIRE(renderer.calls[1].pos.y == Approx(500.0f));
}
