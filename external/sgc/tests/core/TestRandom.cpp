/// @file TestRandom.cpp
/// @brief Random.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/core/Random.hpp"

#include <vector>

TEST_CASE("Random range returns values in range", "[core][random]")
{
	sgc::Random rng(42);

	for (int i = 0; i < 100; ++i)
	{
		int val = rng.range(0, 10);
		REQUIRE(val >= 0);
		REQUIRE(val <= 10);
	}
}

TEST_CASE("Random rangeF returns values in range", "[core][random]")
{
	sgc::Random rng(42);

	for (int i = 0; i < 100; ++i)
	{
		float val = rng.rangeF(0.0f, 1.0f);
		REQUIRE(val >= 0.0f);
		REQUIRE(val <= 1.0f);
	}
}

TEST_CASE("Random same seed produces same sequence", "[core][random]")
{
	sgc::Random rng1(123);
	sgc::Random rng2(123);

	for (int i = 0; i < 10; ++i)
	{
		REQUIRE(rng1.range(0, 1000) == rng2.range(0, 1000));
	}
}

TEST_CASE("Random chance returns bool", "[core][random]")
{
	sgc::Random rng(42);

	int trueCount = 0;
	for (int i = 0; i < 1000; ++i)
	{
		if (rng.chance(0.5f)) ++trueCount;
	}

	REQUIRE(trueCount > 350);
	REQUIRE(trueCount < 650);
}

TEST_CASE("Random normalized returns [0, 1)", "[core][random]")
{
	sgc::Random rng(42);

	for (int i = 0; i < 100; ++i)
	{
		float val = rng.normalized();
		REQUIRE(val >= 0.0f);
		REQUIRE(val < 1.0f);
	}
}

TEST_CASE("Random reseed reproduces same sequence", "[core][random]")
{
	sgc::Random rng(99);

	// 生成した列を記録
	std::vector<int> first;
	for (int i = 0; i < 10; ++i)
		first.push_back(rng.range(0, 1000));

	// reseedで同じシードに戻す
	rng.reseed(99);
	for (int i = 0; i < 10; ++i)
		REQUIRE(rng.range(0, 1000) == first[static_cast<size_t>(i)]);
}

TEST_CASE("Random chance(0) always false", "[core][random]")
{
	sgc::Random rng(42);
	for (int i = 0; i < 100; ++i)
		REQUIRE_FALSE(rng.chance(0.0f));
}

TEST_CASE("Random chance(1) always true", "[core][random]")
{
	sgc::Random rng(42);
	for (int i = 0; i < 100; ++i)
		REQUIRE(rng.chance(1.0f));
}

TEST_CASE("Random normalized double version", "[core][random]")
{
	sgc::Random rng(42);
	for (int i = 0; i < 100; ++i)
	{
		double val = rng.normalized<double>();
		REQUIRE(val >= 0.0);
		REQUIRE(val < 1.0);
	}
}
