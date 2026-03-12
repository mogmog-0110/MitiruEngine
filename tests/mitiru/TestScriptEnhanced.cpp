/// @file TestScriptEnhanced.cpp
/// @brief SimpleScriptEngine拡張機能のテスト（while, for, function, executeFile）

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <mitiru/scripting/SimpleScriptEngine.hpp>

#include <cstdio>
#include <fstream>
#include <string>

using namespace mitiru::scripting;
using Catch::Approx;

// ── while文テスト ──────────────────────────────────

TEST_CASE("Script - while loop basic", "[scripting][enhanced]")
{
	SimpleScriptEngine engine;
	engine.execute("x = 0; while (x < 5) { x = x + 1 }");
	REQUIRE(engine.getGlobalNumber("x") == Approx(5.0));
}

TEST_CASE("Script - while loop accumulator", "[scripting][enhanced]")
{
	SimpleScriptEngine engine;
	engine.execute("sum = 0; i = 1; while (i <= 10) { sum = sum + i; i = i + 1 }");
	REQUIRE(engine.getGlobalNumber("sum") == Approx(55.0));
}

TEST_CASE("Script - while loop with false condition skips body", "[scripting][enhanced]")
{
	SimpleScriptEngine engine;
	engine.execute("x = 100; while (0) { x = 0 }");
	REQUIRE(engine.getGlobalNumber("x") == Approx(100.0));
}

// ── for文テスト ─────────────────────────────────────

TEST_CASE("Script - for loop basic", "[scripting][enhanced]")
{
	SimpleScriptEngine engine;
	engine.execute("sum = 0; for (i = 0; i < 5; i = i + 1) { sum = sum + i }");
	REQUIRE(engine.getGlobalNumber("sum") == Approx(10.0)); // 0+1+2+3+4
}

TEST_CASE("Script - for loop with initial false condition", "[scripting][enhanced]")
{
	SimpleScriptEngine engine;
	engine.execute("x = 99; for (i = 10; i < 5; i = i + 1) { x = 0 }");
	REQUIRE(engine.getGlobalNumber("x") == Approx(99.0));
}

// ── function定義テスト ──────────────────────────────

TEST_CASE("Script - function definition and call", "[scripting][enhanced]")
{
	SimpleScriptEngine engine;
	engine.execute("function double(x) { return x * 2 }");
	engine.execute("result = double(5)");
	REQUIRE(engine.getGlobalNumber("result") == Approx(10.0));
}

TEST_CASE("Script - function with multiple params", "[scripting][enhanced]")
{
	SimpleScriptEngine engine;
	engine.execute("function add(a, b) { return a + b }");
	engine.execute("result = add(3, 7)");
	REQUIRE(engine.getGlobalNumber("result") == Approx(10.0));
}

TEST_CASE("Script - function params dont leak", "[scripting][enhanced]")
{
	SimpleScriptEngine engine;
	engine.setGlobal("x", 42.0);
	engine.execute("function setX(x) { return x * 2 }");
	engine.execute("result = setX(5)");

	/// 関数内のxは外側のxを汚染しない
	REQUIRE(engine.getGlobalNumber("result") == Approx(10.0));
	REQUIRE(engine.getGlobalNumber("x") == Approx(42.0));
}

TEST_CASE("Script - nested function calls", "[scripting][enhanced]")
{
	SimpleScriptEngine engine;
	engine.execute("function square(x) { return x * x }");
	engine.execute("result = square(3) + square(4)");
	REQUIRE(engine.getGlobalNumber("result") == Approx(25.0));
}

// ── return文テスト ──────────────────────────────────

TEST_CASE("Script - return stops execution in function", "[scripting][enhanced]")
{
	SimpleScriptEngine engine;
	engine.execute("function test(x) { return x; x = 999 }");
	engine.execute("result = test(42)");
	REQUIRE(engine.getGlobalNumber("result") == Approx(42.0));
}

// ── executeFileテスト ───────────────────────────────

TEST_CASE("Script - executeFile reads and runs script", "[scripting][enhanced]")
{
	/// 一時ファイルを作成する
	const std::string tmpPath = "test_script_temp.txt";
	{
		std::ofstream f(tmpPath);
		f << "x = 10; y = x * 3";
	}

	SimpleScriptEngine engine;
	REQUIRE(engine.executeFile(tmpPath));
	REQUIRE(engine.getGlobalNumber("x") == Approx(10.0));
	REQUIRE(engine.getGlobalNumber("y") == Approx(30.0));

	std::remove(tmpPath.c_str());
}

TEST_CASE("Script - executeFile with invalid path returns false", "[scripting][enhanced]")
{
	SimpleScriptEngine engine;
	REQUIRE_FALSE(engine.executeFile("nonexistent_script.txt"));
	REQUIRE_FALSE(engine.lastError().empty());
}

// ── ブロック文テスト ────────────────────────────────

TEST_CASE("Script - block statement executes all lines", "[scripting][enhanced]")
{
	SimpleScriptEngine engine;
	engine.execute("{ x = 1; y = 2; z = x + y }");
	REQUIRE(engine.getGlobalNumber("z") == Approx(3.0));
}

// ── 複合テスト ──────────────────────────────────────

TEST_CASE("Script - while with function call", "[scripting][enhanced]")
{
	SimpleScriptEngine engine;
	engine.execute("function inc(x) { return x + 1 }");
	engine.execute("n = 0; while (n < 3) { n = inc(n) }");
	REQUIRE(engine.getGlobalNumber("n") == Approx(3.0));
}
