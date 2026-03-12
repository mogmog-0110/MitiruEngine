#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <mitiru/debug/ProfilerOverlay.hpp>

TEST_CASE("ProfilerOverlay default state", "[mitiru][debug]")
{
	mitiru::debug::ProfilerOverlay profiler;
	REQUIRE(profiler.fpsHistory().empty());
	REQUIRE(profiler.averageFps() == 0.0f);
	REQUIRE(profiler.minFps() == 0.0f);
	REQUIRE(profiler.maxFps() == 0.0f);
	REQUIRE(profiler.latestFrame().samples.empty());
	REQUIRE(profiler.systemTimings().empty());
	REQUIRE(profiler.maxHistory() == 120);
}

TEST_CASE("ProfilerOverlay custom history size", "[mitiru][debug]")
{
	mitiru::debug::ProfilerOverlay profiler(60);
	REQUIRE(profiler.maxHistory() == 60);
}

TEST_CASE("ProfilerOverlay pushFrame updates FPS history", "[mitiru][debug]")
{
	mitiru::debug::ProfilerOverlay profiler(5);

	mitiru::debug::FrameProfile frame;
	frame.totalFrameMs = 16.6f;
	profiler.pushFrame(frame);

	REQUIRE(profiler.fpsHistory().size() == 1);
	REQUIRE(profiler.fpsHistory()[0] == Catch::Approx(1000.0f / 16.6f));
}

TEST_CASE("ProfilerOverlay FPS history circular buffer", "[mitiru][debug]")
{
	mitiru::debug::ProfilerOverlay profiler(3);

	for (int i = 0; i < 5; ++i)
	{
		mitiru::debug::FrameProfile frame;
		frame.totalFrameMs = static_cast<float>(10 + i);
		profiler.pushFrame(frame);
	}

	/// 最大3フレーム分しか保持しない
	REQUIRE(profiler.fpsHistory().size() == 3);
}

TEST_CASE("ProfilerOverlay averageFps calculation", "[mitiru][debug]")
{
	mitiru::debug::ProfilerOverlay profiler;

	/// 16.6ms = 60.24 FPS, 33.3ms = 30.03 FPS
	mitiru::debug::FrameProfile f1;
	f1.totalFrameMs = 16.6f;
	profiler.pushFrame(f1);

	mitiru::debug::FrameProfile f2;
	f2.totalFrameMs = 33.3f;
	profiler.pushFrame(f2);

	float expected = (1000.0f / 16.6f + 1000.0f / 33.3f) / 2.0f;
	REQUIRE(profiler.averageFps() == Catch::Approx(expected).margin(0.1f));
}

TEST_CASE("ProfilerOverlay minFps and maxFps", "[mitiru][debug]")
{
	mitiru::debug::ProfilerOverlay profiler;

	mitiru::debug::FrameProfile fast;
	fast.totalFrameMs = 10.0f;
	profiler.pushFrame(fast);

	mitiru::debug::FrameProfile slow;
	slow.totalFrameMs = 50.0f;
	profiler.pushFrame(slow);

	REQUIRE(profiler.maxFps() == Catch::Approx(100.0f));
	REQUIRE(profiler.minFps() == Catch::Approx(20.0f));
}

TEST_CASE("ProfilerOverlay getFlameGraphData sorted", "[mitiru][debug]")
{
	mitiru::debug::ProfilerOverlay profiler;

	mitiru::debug::FrameProfile frame;
	frame.totalFrameMs = 16.6f;
	frame.samples.push_back({"Physics", 5.0f, 3.0f, 0, 0xFF0000FF});
	frame.samples.push_back({"Collide", 5.0f, 1.5f, 1, 0xFF00FF00});
	frame.samples.push_back({"Update", 0.0f, 5.0f, 0, 0xFFFF0000});
	profiler.pushFrame(frame);

	auto data = profiler.getFlameGraphData();
	REQUIRE(data.size() == 3);
	/// depth 0 が先に来る
	REQUIRE(data[0].depth == 0);
	REQUIRE(data[1].depth == 0);
	REQUIRE(data[2].depth == 1);
}

TEST_CASE("ProfilerOverlay system timings from depth-0 samples", "[mitiru][debug]")
{
	mitiru::debug::ProfilerOverlay profiler;

	mitiru::debug::FrameProfile frame;
	frame.totalFrameMs = 16.6f;
	frame.samples.push_back({"ECS", 0.0f, 3.0f, 0, 0});
	frame.samples.push_back({"SubSystem", 0.0f, 1.0f, 1, 0});
	frame.samples.push_back({"Render", 3.0f, 8.0f, 0, 0});
	profiler.pushFrame(frame);

	auto timings = profiler.systemTimings();
	REQUIRE(timings.size() == 2);
	REQUIRE(timings[0].name == "ECS");
	REQUIRE(timings[1].name == "Render");
}

TEST_CASE("ProfilerOverlay memory tracking allocation", "[mitiru][debug]")
{
	mitiru::debug::ProfilerOverlay profiler;

	profiler.recordAllocation(1024);
	REQUIRE(profiler.memoryStats().allocated == 1024);
	REQUIRE(profiler.memoryStats().current == 1024);
	REQUIRE(profiler.memoryStats().peak == 1024);
	REQUIRE(profiler.memoryStats().freed == 0);
}

TEST_CASE("ProfilerOverlay memory tracking free", "[mitiru][debug]")
{
	mitiru::debug::ProfilerOverlay profiler;

	profiler.recordAllocation(1024);
	profiler.recordFree(512);

	REQUIRE(profiler.memoryStats().current == 512);
	REQUIRE(profiler.memoryStats().freed == 512);
	REQUIRE(profiler.memoryStats().peak == 1024);
}

TEST_CASE("ProfilerOverlay memory peak tracks maximum", "[mitiru][debug]")
{
	mitiru::debug::ProfilerOverlay profiler;

	profiler.recordAllocation(1000);
	profiler.recordAllocation(500);
	REQUIRE(profiler.memoryStats().peak == 1500);

	profiler.recordFree(800);
	REQUIRE(profiler.memoryStats().peak == 1500);
	REQUIRE(profiler.memoryStats().current == 700);
}

TEST_CASE("ProfilerOverlay memory free more than current", "[mitiru][debug]")
{
	mitiru::debug::ProfilerOverlay profiler;
	profiler.recordAllocation(100);
	profiler.recordFree(200);
	REQUIRE(profiler.memoryStats().current == 0);
}

TEST_CASE("ProfilerOverlay zero frame time is skipped for FPS", "[mitiru][debug]")
{
	mitiru::debug::ProfilerOverlay profiler;

	mitiru::debug::FrameProfile frame;
	frame.totalFrameMs = 0.0f;
	profiler.pushFrame(frame);

	REQUIRE(profiler.fpsHistory().empty());
}

TEST_CASE("MemoryStats default values", "[mitiru][debug]")
{
	mitiru::debug::MemoryStats stats;
	REQUIRE(stats.allocated == 0);
	REQUIRE(stats.freed == 0);
	REQUIRE(stats.peak == 0);
	REQUIRE(stats.current == 0);
}
