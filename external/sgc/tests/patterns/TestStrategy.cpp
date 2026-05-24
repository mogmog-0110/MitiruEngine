/// @file TestStrategy.cpp
/// @brief Strategy.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <string>

#include "sgc/patterns/Strategy.hpp"

TEST_CASE("Strategy hasStrategy false by default", "[patterns][strategy]")
{
	sgc::Strategy<int(int, int)> strategy;
	REQUIRE_FALSE(strategy.hasStrategy());
}

TEST_CASE("Strategy execute with simple function", "[patterns][strategy]")
{
	sgc::Strategy<int(int, int)> strategy;
	strategy.setStrategy([](int a, int b) { return a + b; });

	REQUIRE(strategy.hasStrategy());
	REQUIRE(strategy.execute(3, 4) == 7);
}

TEST_CASE("Strategy swap algorithms at runtime", "[patterns][strategy]")
{
	sgc::Strategy<int(int, int)> strategy;

	strategy.setStrategy([](int a, int b) { return a + b; });
	REQUIRE(strategy.execute(10, 5) == 15);

	strategy.setStrategy([](int a, int b) { return a * b; });
	REQUIRE(strategy.execute(10, 5) == 50);
}

TEST_CASE("Strategy clear removes strategy", "[patterns][strategy]")
{
	sgc::Strategy<int()> strategy;
	strategy.setStrategy([]() { return 42; });
	REQUIRE(strategy.hasStrategy());

	strategy.clear();
	REQUIRE_FALSE(strategy.hasStrategy());
}

TEST_CASE("Strategy throws when no strategy set", "[patterns][strategy]")
{
	sgc::Strategy<int()> strategy;
	REQUIRE_THROWS_AS(strategy.execute(), std::runtime_error);
}

TEST_CASE("Strategy with void return", "[patterns][strategy]")
{
	int counter = 0;
	sgc::Strategy<void(int)> strategy;
	strategy.setStrategy([&counter](int n) { counter += n; });

	strategy.execute(5);
	REQUIRE(counter == 5);

	strategy.execute(3);
	REQUIRE(counter == 8);
}

TEST_CASE("Strategy with string return", "[patterns][strategy]")
{
	sgc::Strategy<std::string(const std::string&)> strategy;
	strategy.setStrategy([](const std::string& s) { return "Hello, " + s; });

	REQUIRE(strategy.execute("World") == "Hello, World");
}

TEST_CASE("Strategy with no arguments", "[patterns][strategy]")
{
	sgc::Strategy<int()> strategy;
	strategy.setStrategy([]() { return 42; });
	REQUIRE(strategy.execute() == 42);
}
