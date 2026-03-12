/// @file TestValidate.cpp
/// @brief mitiru検証モジュールのテスト
/// @details InvariantRegistry, HealthCheck, CoverageTracker, TestHarnessの単体テスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <string>
#include <thread>

#include "mitiru/validate/Invariant.hpp"
#include "mitiru/validate/HealthCheck.hpp"
#include "mitiru/validate/CoverageTracker.hpp"
#include "mitiru/validate/TestHarness.hpp"

// ============================================================
// InvariantRegistry tests
// ============================================================

TEST_CASE("InvariantRegistry - initially empty", "[mitiru][validate][invariant]")
{
	mitiru::validate::InvariantRegistry registry;
	CHECK(registry.size() == 0);
}

TEST_CASE("InvariantRegistry - add increases size", "[mitiru][validate][invariant]")
{
	mitiru::validate::InvariantRegistry registry;
	registry.add("inv_1", []() { return true; });
	CHECK(registry.size() == 1);

	registry.add("inv_2", []() { return true; });
	CHECK(registry.size() == 2);
}

TEST_CASE("InvariantRegistry - checkAll passes when all predicates return true", "[mitiru][validate][invariant]")
{
	mitiru::validate::InvariantRegistry registry;
	registry.add("always_true_1", []() { return true; });
	registry.add("always_true_2", []() { return true; });

	const auto results = registry.checkAll(10);
	REQUIRE(results.size() == 2);

	for (const auto& r : results)
	{
		CHECK(r.passed == true);
		CHECK(r.frame == 10);
		CHECK(r.message == "OK");
	}
}

TEST_CASE("InvariantRegistry - checkAll detects failure", "[mitiru][validate][invariant]")
{
	mitiru::validate::InvariantRegistry registry;

	int health = 100;
	registry.add("health_positive", [&health]() { return health > 0; });
	registry.add("health_bounded", [&health]() { return health <= 100; });

	/// All pass initially
	{
		const auto results = registry.checkAll(0);
		CHECK(results[0].passed == true);
		CHECK(results[1].passed == true);
	}

	/// Make health negative -> first invariant fails
	health = -10;
	{
		const auto results = registry.checkAll(5);
		REQUIRE(results.size() == 2);
		CHECK(results[0].passed == false);
		CHECK(results[0].name == "health_positive");
		CHECK(results[0].message.find("FAILED") != std::string::npos);
		CHECK(results[1].passed == true);
	}
}

TEST_CASE("InvariantRegistry - remove removes invariant by name", "[mitiru][validate][invariant]")
{
	mitiru::validate::InvariantRegistry registry;
	registry.add("inv_a", []() { return true; });
	registry.add("inv_b", []() { return false; });
	registry.add("inv_c", []() { return true; });
	CHECK(registry.size() == 3);

	registry.remove("inv_b");
	CHECK(registry.size() == 2);

	/// All remaining should pass
	const auto results = registry.checkAll(0);
	for (const auto& r : results)
	{
		CHECK(r.passed == true);
	}
}

TEST_CASE("InvariantRegistry - remove nonexistent name is safe", "[mitiru][validate][invariant]")
{
	mitiru::validate::InvariantRegistry registry;
	registry.add("inv_1", []() { return true; });

	registry.remove("nonexistent");
	CHECK(registry.size() == 1);
}

TEST_CASE("InvariantRegistry - clear removes all", "[mitiru][validate][invariant]")
{
	mitiru::validate::InvariantRegistry registry;
	registry.add("a", []() { return true; });
	registry.add("b", []() { return true; });
	registry.clear();
	CHECK(registry.size() == 0);
}

TEST_CASE("InvariantRegistry - toJson produces valid JSON", "[mitiru][validate][invariant]")
{
	mitiru::validate::InvariantRegistry registry;
	registry.add("inv_pass", []() { return true; });
	registry.add("inv_fail", []() { return false; });

	const auto json = registry.toJson(42);
	CHECK(json.front() == '[');
	CHECK(json.back() == ']');
	CHECK(json.find("\"name\":\"inv_pass\"") != std::string::npos);
	CHECK(json.find("\"name\":\"inv_fail\"") != std::string::npos);
	CHECK(json.find("\"passed\":true") != std::string::npos);
	CHECK(json.find("\"passed\":false") != std::string::npos);
	CHECK(json.find("\"frame\":42") != std::string::npos);
}

TEST_CASE("InvariantRegistry - toJson with empty registry", "[mitiru][validate][invariant]")
{
	mitiru::validate::InvariantRegistry registry;
	const auto json = registry.toJson(0);
	CHECK(json == "[]");
}

TEST_CASE("InvariantRegistry - checkAll returns results in registration order", "[mitiru][validate][invariant]")
{
	mitiru::validate::InvariantRegistry registry;
	registry.add("first", []() { return true; });
	registry.add("second", []() { return false; });
	registry.add("third", []() { return true; });

	const auto results = registry.checkAll(0);
	REQUIRE(results.size() == 3);
	CHECK(results[0].name == "first");
	CHECK(results[1].name == "second");
	CHECK(results[2].name == "third");
}

TEST_CASE("ValidationResult - toJson produces correct output", "[mitiru][validate][invariant]")
{
	mitiru::validate::ValidationResult result;
	result.passed = true;
	result.name = "test_inv";
	result.message = "OK";
	result.frame = 99;

	const auto json = result.toJson();
	CHECK(json.find("\"passed\":true") != std::string::npos);
	CHECK(json.find("\"name\":\"test_inv\"") != std::string::npos);
	CHECK(json.find("\"message\":\"OK\"") != std::string::npos);
	CHECK(json.find("\"frame\":99") != std::string::npos);
}

// ============================================================
// HealthCheck tests
// ============================================================

TEST_CASE("HealthCheck - initial metrics are zero", "[mitiru][validate][health]")
{
	mitiru::validate::HealthCheck health;
	const auto& m = health.metrics();
	CHECK(m.fps == Catch::Approx(0.0f));
	CHECK(m.updateMs == Catch::Approx(0.0f));
	CHECK(m.drawMs == Catch::Approx(0.0f));
	CHECK(m.entityCount == 0);
	CHECK(m.memoryUsageMB == Catch::Approx(0.0f));
	CHECK(m.drawCallCount == 0);
}

TEST_CASE("HealthCheck - beginFrame/endFrame measures positive time", "[mitiru][validate][health]")
{
	mitiru::validate::HealthCheck health;

	health.beginFrame();
	/// Small work to ensure non-zero duration
	volatile int sum = 0;
	for (int i = 0; i < 1000; ++i)
	{
		sum += i;
	}
	health.endFrame();

	const auto& m = health.metrics();
	CHECK(m.updateMs >= 0.0f);
}

TEST_CASE("HealthCheck - beginDraw/endDraw measures draw time", "[mitiru][validate][health]")
{
	mitiru::validate::HealthCheck health;

	health.beginDraw();
	volatile int sum = 0;
	for (int i = 0; i < 1000; ++i)
	{
		sum += i;
	}
	health.endDraw();

	const auto& m = health.metrics();
	CHECK(m.drawMs >= 0.0f);
}

TEST_CASE("HealthCheck - recordEntityCount stores value", "[mitiru][validate][health]")
{
	mitiru::validate::HealthCheck health;
	health.recordEntityCount(150);
	CHECK(health.metrics().entityCount == 150);

	health.recordEntityCount(200);
	CHECK(health.metrics().entityCount == 200);
}

TEST_CASE("HealthCheck - recordDrawCalls stores value", "[mitiru][validate][health]")
{
	mitiru::validate::HealthCheck health;
	health.recordDrawCalls(42);
	CHECK(health.metrics().drawCallCount == 42);
}

TEST_CASE("HealthCheck - recordMemoryUsage stores value", "[mitiru][validate][health]")
{
	mitiru::validate::HealthCheck health;
	health.recordMemoryUsage(256.5f);
	CHECK(health.metrics().memoryUsageMB == Catch::Approx(256.5f));
}

TEST_CASE("HealthCheck - FPS is calculated after two endFrame calls", "[mitiru][validate][health]")
{
	mitiru::validate::HealthCheck health;

	/// First frame establishes baseline
	health.beginFrame();
	health.endFrame();

	/// Small delay to ensure measurable interval
	std::this_thread::sleep_for(std::chrono::milliseconds(10));

	/// Second frame should produce FPS
	health.beginFrame();
	health.endFrame();

	const auto& m = health.metrics();
	/// FPS should be positive (could be very high on fast machines)
	CHECK(m.fps > 0.0f);
}

TEST_CASE("HealthCheck - toJson produces valid output", "[mitiru][validate][health]")
{
	mitiru::validate::HealthCheck health;
	health.recordEntityCount(100);
	health.recordDrawCalls(50);
	health.recordMemoryUsage(128.0f);

	const auto json = health.toJson();
	CHECK(json.find("\"fps\"") != std::string::npos);
	CHECK(json.find("\"updateMs\"") != std::string::npos);
	CHECK(json.find("\"drawMs\"") != std::string::npos);
	CHECK(json.find("\"entityCount\":100") != std::string::npos);
	CHECK(json.find("\"drawCallCount\":50") != std::string::npos);
}

TEST_CASE("HealthMetrics - toJson produces complete JSON", "[mitiru][validate][health]")
{
	mitiru::validate::HealthMetrics m;
	m.fps = 60.0f;
	m.updateMs = 1.5f;
	m.drawMs = 2.5f;
	m.entityCount = 300;
	m.memoryUsageMB = 512.0f;
	m.drawCallCount = 100;

	const auto json = m.toJson();
	CHECK(json.front() == '{');
	CHECK(json.back() == '}');
	CHECK(json.find("\"entityCount\":300") != std::string::npos);
	CHECK(json.find("\"drawCallCount\":100") != std::string::npos);
}

// ============================================================
// CoverageTracker tests
// ============================================================

TEST_CASE("CoverageTracker - initially empty with 100% coverage", "[mitiru][validate][coverage]")
{
	mitiru::validate::CoverageTracker tracker;
	CHECK(tracker.coverage() == Catch::Approx(1.0f));
	CHECK(tracker.allPaths().empty());
	CHECK(tracker.visitedPaths().empty());
	CHECK(tracker.unvisitedPaths().empty());
}

TEST_CASE("CoverageTracker - registerPath adds unvisited path", "[mitiru][validate][coverage]")
{
	mitiru::validate::CoverageTracker tracker;
	tracker.registerPath("path_a");
	tracker.registerPath("path_b");

	CHECK(tracker.allPaths().size() == 2);
	CHECK(tracker.visitedPaths().empty());
	CHECK(tracker.coverage() == Catch::Approx(0.0f));
}

TEST_CASE("CoverageTracker - markVisited marks path and auto-registers", "[mitiru][validate][coverage]")
{
	mitiru::validate::CoverageTracker tracker;

	/// Mark a pre-registered path
	tracker.registerPath("path_a");
	tracker.markVisited("path_a");
	CHECK(tracker.isVisited("path_a"));

	/// Mark an unregistered path - auto-registers
	tracker.markVisited("path_b");
	CHECK(tracker.isVisited("path_b"));
	CHECK(tracker.allPaths().size() == 2);
}

TEST_CASE("CoverageTracker - coverage calculation is correct", "[mitiru][validate][coverage]")
{
	mitiru::validate::CoverageTracker tracker;
	tracker.registerPath("p1");
	tracker.registerPath("p2");
	tracker.registerPath("p3");
	tracker.registerPath("p4");

	CHECK(tracker.coverage() == Catch::Approx(0.0f));

	tracker.markVisited("p1");
	CHECK(tracker.coverage() == Catch::Approx(0.25f));

	tracker.markVisited("p2");
	CHECK(tracker.coverage() == Catch::Approx(0.5f));

	tracker.markVisited("p3");
	CHECK(tracker.coverage() == Catch::Approx(0.75f));

	tracker.markVisited("p4");
	CHECK(tracker.coverage() == Catch::Approx(1.0f));
}

TEST_CASE("CoverageTracker - isVisited returns false for unvisited", "[mitiru][validate][coverage]")
{
	mitiru::validate::CoverageTracker tracker;
	tracker.registerPath("path_a");
	CHECK_FALSE(tracker.isVisited("path_a"));
	CHECK_FALSE(tracker.isVisited("nonexistent"));
}

TEST_CASE("CoverageTracker - unvisitedPaths returns correct paths", "[mitiru][validate][coverage]")
{
	mitiru::validate::CoverageTracker tracker;
	tracker.registerPath("p1");
	tracker.registerPath("p2");
	tracker.registerPath("p3");
	tracker.markVisited("p2");

	const auto unvisited = tracker.unvisitedPaths();
	CHECK(unvisited.size() == 2);

	/// Check that p1 and p3 are in unvisited (order is unspecified for unordered_set)
	bool hasP1 = false;
	bool hasP3 = false;
	for (const auto& p : unvisited)
	{
		if (p == "p1") hasP1 = true;
		if (p == "p3") hasP3 = true;
	}
	CHECK(hasP1);
	CHECK(hasP3);
}

TEST_CASE("CoverageTracker - duplicate registerPath is idempotent", "[mitiru][validate][coverage]")
{
	mitiru::validate::CoverageTracker tracker;
	tracker.registerPath("dup");
	tracker.registerPath("dup");
	tracker.registerPath("dup");

	/// unordered_set deduplicates
	CHECK(tracker.allPaths().size() == 1);
}

TEST_CASE("CoverageTracker - duplicate markVisited is idempotent", "[mitiru][validate][coverage]")
{
	mitiru::validate::CoverageTracker tracker;
	tracker.registerPath("p1");
	tracker.registerPath("p2");

	tracker.markVisited("p1");
	tracker.markVisited("p1");
	tracker.markVisited("p1");

	CHECK(tracker.visitedPaths().size() == 1);
	CHECK(tracker.coverage() == Catch::Approx(0.5f));
}

TEST_CASE("CoverageTracker - reset clears all data", "[mitiru][validate][coverage]")
{
	mitiru::validate::CoverageTracker tracker;
	tracker.registerPath("p1");
	tracker.markVisited("p1");

	tracker.reset();
	CHECK(tracker.allPaths().empty());
	CHECK(tracker.visitedPaths().empty());
	CHECK(tracker.coverage() == Catch::Approx(1.0f));
}

TEST_CASE("CoverageTracker - resetVisited keeps paths but clears visits", "[mitiru][validate][coverage]")
{
	mitiru::validate::CoverageTracker tracker;
	tracker.registerPath("p1");
	tracker.registerPath("p2");
	tracker.markVisited("p1");

	tracker.resetVisited();
	CHECK(tracker.allPaths().size() == 2);
	CHECK(tracker.visitedPaths().empty());
	CHECK(tracker.coverage() == Catch::Approx(0.0f));
}

TEST_CASE("CoverageTracker - toJson produces valid output", "[mitiru][validate][coverage]")
{
	mitiru::validate::CoverageTracker tracker;
	tracker.registerPath("p1");
	tracker.registerPath("p2");
	tracker.markVisited("p1");

	const auto json = tracker.toJson();
	CHECK(json.front() == '{');
	CHECK(json.back() == '}');
	CHECK(json.find("\"totalPaths\":2") != std::string::npos);
	CHECK(json.find("\"visitedCount\":1") != std::string::npos);
	CHECK(json.find("\"coverage\"") != std::string::npos);
	CHECK(json.find("\"unvisited\"") != std::string::npos);
}

TEST_CASE("CoverageTracker - toJson shows unvisited paths", "[mitiru][validate][coverage]")
{
	mitiru::validate::CoverageTracker tracker;
	tracker.registerPath("visited_path");
	tracker.registerPath("unvisited_path");
	tracker.markVisited("visited_path");

	const auto json = tracker.toJson();
	CHECK(json.find("\"unvisited_path\"") != std::string::npos);
	/// visited_path should NOT be in the unvisited list
	/// (it could appear in other parts of JSON but not in the unvisited array)
}

TEST_CASE("CoverageTracker - MITIRU_COVERAGE_MARK macro works", "[mitiru][validate][coverage]")
{
	mitiru::validate::CoverageTracker tracker;
	tracker.registerPath("macro_path");

	MITIRU_COVERAGE_MARK(tracker, "macro_path");
	CHECK(tracker.isVisited("macro_path"));
}

// ============================================================
// TestHarness tests
// ============================================================

TEST_CASE("TestHarness - initially empty", "[mitiru][validate][harness]")
{
	mitiru::validate::TestHarness harness;
	CHECK(harness.size() == 0);
}

TEST_CASE("TestHarness - addTest increases size", "[mitiru][validate][harness]")
{
	mitiru::validate::TestHarness harness;
	harness.addTest(mitiru::validate::GameTest{
		"test_1",
		[](mitiru::Engine&) { return true; }
	});
	CHECK(harness.size() == 1);

	harness.addTest(mitiru::validate::GameTest{
		"test_2",
		[](mitiru::Engine&) { return false; }
	});
	CHECK(harness.size() == 2);
}

TEST_CASE("TestHarness - clear removes all tests", "[mitiru][validate][harness]")
{
	mitiru::validate::TestHarness harness;
	harness.addTest(mitiru::validate::GameTest{
		"test_1",
		[](mitiru::Engine&) { return true; }
	});
	harness.addTest(mitiru::validate::GameTest{
		"test_2",
		[](mitiru::Engine&) { return true; }
	});
	CHECK(harness.size() == 2);

	harness.clear();
	CHECK(harness.size() == 0);
}

TEST_CASE("TestResult - toJson produces correct output", "[mitiru][validate][harness]")
{
	mitiru::validate::TestResult result;
	result.name = "my_test";
	result.passed = true;
	result.message = "OK";

	const auto json = result.toJson();
	CHECK(json.find("\"name\":\"my_test\"") != std::string::npos);
	CHECK(json.find("\"passed\":true") != std::string::npos);
	CHECK(json.find("\"message\":\"OK\"") != std::string::npos);
}

TEST_CASE("TestResult - toJson for failed test", "[mitiru][validate][harness]")
{
	mitiru::validate::TestResult result;
	result.name = "failing_test";
	result.passed = false;
	result.message = "FAILED";

	const auto json = result.toJson();
	CHECK(json.find("\"passed\":false") != std::string::npos);
	CHECK(json.find("\"message\":\"FAILED\"") != std::string::npos);
}
