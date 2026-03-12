#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <mitiru/scripting/IScriptEngine.hpp>
#include <mitiru/scripting/NullScriptEngine.hpp>
#include <mitiru/scripting/ScriptBindings.hpp>

using namespace mitiru::scripting;

// --- NullScriptEngine ---

TEST_CASE("NullScriptEngine execute returns true", "[mitiru][scripting]")
{
	NullScriptEngine engine;
	REQUIRE(engine.execute("print('hello')"));
}

TEST_CASE("NullScriptEngine executeFile returns true", "[mitiru][scripting]")
{
	NullScriptEngine engine;
	REQUIRE(engine.executeFile("nonexistent.lua"));
}

TEST_CASE("NullScriptEngine getGlobalNumber returns 0", "[mitiru][scripting]")
{
	NullScriptEngine engine;
	REQUIRE(engine.getGlobalNumber("x") == Catch::Approx(0.0));
}

TEST_CASE("NullScriptEngine getGlobalString returns empty", "[mitiru][scripting]")
{
	NullScriptEngine engine;
	REQUIRE(engine.getGlobalString("name").empty());
}

TEST_CASE("NullScriptEngine callFunction returns true", "[mitiru][scripting]")
{
	NullScriptEngine engine;
	REQUIRE(engine.callFunction("onUpdate"));
}

TEST_CASE("NullScriptEngine lastError returns empty", "[mitiru][scripting]")
{
	NullScriptEngine engine;
	REQUIRE(engine.lastError().empty());
}

TEST_CASE("NullScriptEngine setGlobal does not crash", "[mitiru][scripting]")
{
	NullScriptEngine engine;
	engine.setGlobal("hp", 100.0);
	engine.setGlobal("name", "player");
	REQUIRE(engine.getGlobalNumber("hp") == Catch::Approx(0.0));
	REQUIRE(engine.getGlobalString("name").empty());
}

// --- ScriptBindings ---

TEST_CASE("ScriptBindings availableBindings is not empty", "[mitiru][scripting]")
{
	auto bindings = ScriptBindings::availableBindings();
	REQUIRE_FALSE(bindings.empty());
}

TEST_CASE("ScriptBindings contain expected categories", "[mitiru][scripting]")
{
	auto bindings = ScriptBindings::availableBindings();

	bool hasEcs = false;
	bool hasInput = false;
	bool hasAudio = false;

	for (const auto& b : bindings)
	{
		if (b.category == "ecs") hasEcs = true;
		if (b.category == "input") hasInput = true;
		if (b.category == "audio") hasAudio = true;
	}

	REQUIRE(hasEcs);
	REQUIRE(hasInput);
	REQUIRE(hasAudio);
}
