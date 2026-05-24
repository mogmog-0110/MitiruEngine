#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/vn/TextDisplay.hpp"

using namespace sgc::vn;

TEST_CASE("TextDisplay - Initial state has zero visible chars", "[vn][textdisplay]")
{
	TextDisplayState state;
	state.fullText = "Hello, world!";

	REQUIRE(state.visibleChars == 0);
	REQUIRE_FALSE(state.isComplete);
	REQUIRE_FALSE(state.advanced);
}

TEST_CASE("TextDisplay - Update advances visible chars", "[vn][textdisplay]")
{
	TextDisplayConfig config;
	config.charsPerSecond = 10.0f;

	TextDisplayState state;
	state.fullText = "Hello, world!";

	// 0.5s at 10 chars/sec = 5 chars
	state = updateTextDisplay(state, 0.5f, config, false, false);
	REQUIRE(state.visibleChars == 5);
	REQUIRE_FALSE(state.isComplete);
}

TEST_CASE("TextDisplay - Skip shows all text instantly", "[vn][textdisplay]")
{
	TextDisplayConfig config;
	config.charsPerSecond = 10.0f;

	TextDisplayState state;
	state.fullText = "Hello, world!";

	state = updateTextDisplay(state, 0.0f, config, false, true);
	REQUIRE(state.visibleChars == 13);
	REQUIRE(state.isComplete);
}

TEST_CASE("TextDisplay - Complete text sets isComplete", "[vn][textdisplay]")
{
	TextDisplayConfig config;
	config.charsPerSecond = 100.0f;

	TextDisplayState state;
	state.fullText = "Hi";

	// 1s at 100 chars/sec >> 2 chars
	state = updateTextDisplay(state, 1.0f, config, false, false);
	REQUIRE(state.isComplete);
	REQUIRE(state.visibleChars == 2);
}

TEST_CASE("TextDisplay - Auto advance triggers after delay", "[vn][textdisplay]")
{
	TextDisplayConfig config;
	config.charsPerSecond = 100.0f;
	config.autoAdvance = true;
	config.autoAdvanceDelay = 1.0f;

	TextDisplayState state;
	state.fullText = "Hi";

	// Complete the text
	state = updateTextDisplay(state, 1.0f, config, false, false);
	REQUIRE(state.isComplete);
	REQUIRE_FALSE(state.advanced);

	// Wait less than delay
	state = updateTextDisplay(state, 0.5f, config, false, false);
	REQUIRE_FALSE(state.advanced);

	// Wait past delay
	state = updateTextDisplay(state, 0.6f, config, false, false);
	REQUIRE(state.advanced);
}

TEST_CASE("TextDisplay - Manual advance on input", "[vn][textdisplay]")
{
	TextDisplayConfig config;
	config.charsPerSecond = 100.0f;
	config.autoAdvance = false;

	TextDisplayState state;
	state.fullText = "Hi";

	// Complete the text
	state = updateTextDisplay(state, 1.0f, config, false, false);
	REQUIRE(state.isComplete);
	REQUIRE(state.waitingForInput);
	REQUIRE_FALSE(state.advanced);

	// Press advance
	state = updateTextDisplay(state, 0.0f, config, true, false);
	REQUIRE(state.advanced);
}

TEST_CASE("TextDisplay - Empty text is immediately complete", "[vn][textdisplay]")
{
	TextDisplayConfig config;
	TextDisplayState state;
	state.fullText = "";

	state = updateTextDisplay(state, 0.1f, config, false, false);
	REQUIRE(state.isComplete);
	REQUIRE(state.visibleChars == 0);
}
