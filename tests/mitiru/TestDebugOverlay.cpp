#include <catch2/catch_test_macros.hpp>

#include <mitiru/debug/IDebugOverlay.hpp>
#include <mitiru/debug/NullDebugOverlay.hpp>
#include <mitiru/debug/DebugPanel.hpp>

// --- NullDebugOverlay tests ---

TEST_CASE("NullDebugOverlay methods do not crash", "[mitiru][debug]")
{
	mitiru::debug::NullDebugOverlay overlay;

	SECTION("beginFrame and endFrame")
	{
		overlay.beginFrame();
		overlay.endFrame();
	}

	SECTION("text")
	{
		overlay.text("label", "value");
	}

	SECTION("slider returns false")
	{
		float val = 0.5f;
		REQUIRE_FALSE(overlay.slider("speed", val, 0.0f, 1.0f));
		REQUIRE(val == 0.5f);
	}

	SECTION("checkbox returns false")
	{
		bool checked = true;
		REQUIRE_FALSE(overlay.checkbox("enabled", checked));
		REQUIRE(checked == true);
	}

	SECTION("button returns false")
	{
		REQUIRE_FALSE(overlay.button("click me"));
	}

	SECTION("separator")
	{
		overlay.separator();
	}

	SECTION("beginWindow returns false")
	{
		REQUIRE_FALSE(overlay.beginWindow("Test Window"));
	}

	SECTION("endWindow")
	{
		overlay.endWindow();
	}
}

// --- DebugPanel tests ---

TEST_CASE("DebugPanel isVisible defaults to false", "[mitiru][debug]")
{
	mitiru::debug::NullDebugOverlay overlay;
	mitiru::debug::DebugPanel panel(&overlay);

	REQUIRE_FALSE(panel.isVisible());
}

TEST_CASE("DebugPanel toggle switches visibility", "[mitiru][debug]")
{
	mitiru::debug::NullDebugOverlay overlay;
	mitiru::debug::DebugPanel panel(&overlay);

	REQUIRE_FALSE(panel.isVisible());

	panel.toggle();
	REQUIRE(panel.isVisible());

	panel.toggle();
	REQUIRE_FALSE(panel.isVisible());
}

TEST_CASE("DebugPanel draw with null overlay does not crash", "[mitiru][debug]")
{
	mitiru::debug::DebugPanel panel(nullptr);
	panel.toggle();

	mitiru::Engine engine;
	mitiru::EngineConfig config;
	config.headless = true;

	/// draw with null overlay should be safe
	panel.draw(engine);
}

TEST_CASE("DebugPanel draw when not visible is a no-op", "[mitiru][debug]")
{
	mitiru::debug::NullDebugOverlay overlay;
	mitiru::debug::DebugPanel panel(&overlay);

	mitiru::Engine engine;

	/// panel is not visible, draw should return immediately
	panel.draw(engine);
}
