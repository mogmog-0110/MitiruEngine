#include <catch2/catch_test_macros.hpp>

#include <mitiru/debug/GpuProfiler.hpp>

TEST_CASE("NullGpuTimer begin/end does not crash", "[mitiru][debug]")
{
	mitiru::debug::NullGpuTimer timer;
	timer.begin("ShadowPass");
	timer.end("ShadowPass");
}

TEST_CASE("NullGpuTimer begin records section name", "[mitiru][debug]")
{
	mitiru::debug::NullGpuTimer timer;
	timer.begin("ForwardPass");
	REQUIRE(timer.sectionCount() == 1);

	timer.begin("PostProcess");
	REQUIRE(timer.sectionCount() == 2);
}

TEST_CASE("NullGpuTimer getResults returns zero durations", "[mitiru][debug]")
{
	mitiru::debug::NullGpuTimer timer;
	timer.begin("ShadowPass");
	timer.end("ShadowPass");
	timer.begin("ForwardPass");
	timer.end("ForwardPass");

	auto results = timer.getResults();
	REQUIRE(results.size() == 2);
	REQUIRE(results.at("ShadowPass") == 0.0f);
	REQUIRE(results.at("ForwardPass") == 0.0f);
}

TEST_CASE("NullGpuTimer reset clears sections", "[mitiru][debug]")
{
	mitiru::debug::NullGpuTimer timer;
	timer.begin("Pass1");
	timer.begin("Pass2");
	REQUIRE(timer.sectionCount() == 2);

	timer.reset();
	REQUIRE(timer.sectionCount() == 0);
	REQUIRE(timer.getResults().empty());
}

TEST_CASE("GpuProfileScope RAII calls begin and end", "[mitiru][debug]")
{
	mitiru::debug::NullGpuTimer timer;

	{
		mitiru::debug::GpuProfileScope scope(&timer, "TestScope");
		REQUIRE(timer.sectionCount() == 1);
	}

	/// end()はno-opだがクラッシュしないことを確認
	auto results = timer.getResults();
	REQUIRE(results.count("TestScope") == 1);
}

TEST_CASE("GpuProfileScope with null timer does not crash", "[mitiru][debug]")
{
	{
		mitiru::debug::GpuProfileScope scope(nullptr, "NullScope");
	}
}

TEST_CASE("GpuProfileScope nested scopes", "[mitiru][debug]")
{
	mitiru::debug::NullGpuTimer timer;

	{
		mitiru::debug::GpuProfileScope outer(&timer, "Outer");
		{
			mitiru::debug::GpuProfileScope inner(&timer, "Inner");
			REQUIRE(timer.sectionCount() == 2);
		}
	}

	auto results = timer.getResults();
	REQUIRE(results.size() == 2);
}

TEST_CASE("NullGpuTimer getResults empty initially", "[mitiru][debug]")
{
	mitiru::debug::NullGpuTimer timer;
	REQUIRE(timer.getResults().empty());
	REQUIRE(timer.sectionCount() == 0);
}

TEST_CASE("NullGpuTimer duplicate section name overwrites", "[mitiru][debug]")
{
	mitiru::debug::NullGpuTimer timer;
	timer.begin("Pass");
	timer.begin("Pass");
	REQUIRE(timer.sectionCount() == 1);
}
