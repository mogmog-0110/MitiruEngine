#include <catch2/catch_test_macros.hpp>

#include <thread>
#include <vector>

#include "sgc/core/Profiler.hpp"

TEST_CASE("Profiler beginSection and endSection", "[core][profiler]")
{
	auto& profiler = sgc::Profiler::instance();
	profiler.reset();

	profiler.beginSection("test");
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	profiler.endSection("test");

	auto s = profiler.stats("test");
	REQUIRE(s.callCount == 1);
	REQUIRE(s.totalMs > 0.0);
	REQUIRE(s.averageMs > 0.0);
	profiler.reset();
}

TEST_CASE("Profiler multiple calls accumulate", "[core][profiler]")
{
	auto& profiler = sgc::Profiler::instance();
	profiler.reset();

	for (int i = 0; i < 3; ++i)
	{
		profiler.beginSection("loop");
		profiler.endSection("loop");
	}

	auto s = profiler.stats("loop");
	REQUIRE(s.callCount == 3);
	profiler.reset();
}

TEST_CASE("Profiler reset clears all data", "[core][profiler]")
{
	auto& profiler = sgc::Profiler::instance();
	profiler.beginSection("reset_test");
	profiler.endSection("reset_test");
	profiler.reset();

	auto s = profiler.stats("reset_test");
	REQUIRE(s.callCount == 0);
}

TEST_CASE("Profiler allStats returns all sections", "[core][profiler]")
{
	auto& profiler = sgc::Profiler::instance();
	profiler.reset();

	profiler.beginSection("alpha");
	profiler.endSection("alpha");
	profiler.beginSection("beta");
	profiler.endSection("beta");

	auto all = profiler.allStats();
	REQUIRE(all.size() == 2);
	profiler.reset();
}

TEST_CASE("Profiler min max tracking", "[core][profiler]")
{
	auto& profiler = sgc::Profiler::instance();
	profiler.reset();

	profiler.beginSection("minmax");
	profiler.endSection("minmax");
	profiler.beginSection("minmax");
	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	profiler.endSection("minmax");

	auto s = profiler.stats("minmax");
	REQUIRE(s.callCount == 2);
	REQUIRE(s.maxMs >= s.minMs);
	profiler.reset();
}

TEST_CASE("ScopedProfile measures scope", "[core][profiler]")
{
	auto& profiler = sgc::Profiler::instance();
	profiler.reset();

	{
		sgc::ScopedProfile sp("scoped_test");
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	auto s = profiler.stats("scoped_test");
	REQUIRE(s.callCount == 1);
	REQUIRE(s.totalMs > 0.0);
	profiler.reset();
}

TEST_CASE("Profiler stats for unknown section returns defaults", "[core][profiler]")
{
	auto& profiler = sgc::Profiler::instance();
	profiler.reset();

	auto s = profiler.stats("nonexistent");
	REQUIRE(s.callCount == 0);
	REQUIRE(s.totalMs == 0.0);
}

TEST_CASE("Profiler endSection without begin is safe", "[core][profiler]")
{
	auto& profiler = sgc::Profiler::instance();
	profiler.reset();

	// endSection without begin should be a no-op
	profiler.endSection("never_started");
	auto s = profiler.stats("never_started");
	REQUIRE(s.callCount == 0);
}

TEST_CASE("SGC_PROFILE_SCOPE macro works", "[core][profiler]")
{
	auto& profiler = sgc::Profiler::instance();
	profiler.reset();

	{
		SGC_PROFILE_SCOPE("macro_test");
	}

	auto s = profiler.stats("macro_test");
	REQUIRE(s.callCount == 1);
	profiler.reset();
}

TEST_CASE("Profiler concurrent access is safe", "[core][profiler]")
{
	auto& profiler = sgc::Profiler::instance();
	profiler.reset();

	std::vector<std::thread> threads;
	for (int i = 0; i < 4; ++i)
	{
		threads.emplace_back([&profiler, i]()
		{
			const std::string name = "concurrent_" + std::to_string(i);
			for (int j = 0; j < 50; ++j)
			{
				profiler.beginSection(name);
				profiler.endSection(name);
			}
		});
	}
	for (auto& t : threads) t.join();

	// スレッド合流後にアサート（Catch2マクロはスレッド外でのみ使用）
	for (int i = 0; i < 4; ++i)
	{
		const std::string name = "concurrent_" + std::to_string(i);
		auto s = profiler.stats(name);
		REQUIRE(s.callCount == 50);
	}
	profiler.reset();
}
