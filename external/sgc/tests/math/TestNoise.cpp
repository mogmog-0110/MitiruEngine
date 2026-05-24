/// @file TestNoise.cpp
/// @brief Noise.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/math/Noise.hpp"

using Catch::Approx;

TEST_CASE("PerlinNoise same seed produces same output", "[math][noise]")
{
	sgc::PerlinNoise a(42);
	sgc::PerlinNoise b(42);
	REQUIRE(a.noise2D(1.5f, 2.3f) == b.noise2D(1.5f, 2.3f));
}

TEST_CASE("PerlinNoise different seeds produce different output", "[math][noise]")
{
	sgc::PerlinNoise a(42);
	sgc::PerlinNoise b(123);
	REQUIRE(a.noise2D(1.5f, 2.3f) != b.noise2D(1.5f, 2.3f));
}

TEST_CASE("PerlinNoise 1D output in range [-1, 1]", "[math][noise]")
{
	sgc::PerlinNoise noise(0);
	for (int i = 0; i < 1000; ++i)
	{
		const float x = static_cast<float>(i) * 0.1f;
		const float v = noise.noise1D(x);
		REQUIRE(v >= -1.0f);
		REQUIRE(v <= 1.0f);
	}
}

TEST_CASE("PerlinNoise 2D output in range [-1, 1]", "[math][noise]")
{
	sgc::PerlinNoise noise(42);
	for (int i = 0; i < 100; ++i)
	{
		for (int j = 0; j < 100; ++j)
		{
			const float x = static_cast<float>(i) * 0.1f;
			const float y = static_cast<float>(j) * 0.1f;
			const float v = noise.noise2D(x, y);
			REQUIRE(v >= -1.0f);
			REQUIRE(v <= 1.0f);
		}
	}
}

TEST_CASE("PerlinNoise 3D output in range [-1, 1]", "[math][noise]")
{
	sgc::PerlinNoise noise(42);
	for (int i = 0; i < 20; ++i)
	{
		for (int j = 0; j < 20; ++j)
		{
			for (int k = 0; k < 20; ++k)
			{
				const float v = noise.noise3D(
					static_cast<float>(i) * 0.5f,
					static_cast<float>(j) * 0.5f,
					static_cast<float>(k) * 0.5f);
				REQUIRE(v >= -1.0f);
				REQUIRE(v <= 1.0f);
			}
		}
	}
}

TEST_CASE("PerlinNoise noise2D_01 output in [0, 1]", "[math][noise]")
{
	sgc::PerlinNoise noise(42);
	for (int i = 0; i < 100; ++i)
	{
		const float v = noise.noise2D_01(
			static_cast<float>(i) * 0.37f,
			static_cast<float>(i) * 0.53f);
		REQUIRE(v >= 0.0f);
		REQUIRE(v <= 1.0f);
	}
}

TEST_CASE("PerlinNoise octave2D output is bounded", "[math][noise]")
{
	sgc::PerlinNoise noise(42);
	for (int i = 0; i < 100; ++i)
	{
		const float v = noise.octave2D(
			static_cast<float>(i) * 0.1f,
			static_cast<float>(i) * 0.2f, 4, 0.5f);
		REQUIRE(v >= -1.0f);
		REQUIRE(v <= 1.0f);
	}
}

TEST_CASE("PerlinNoise reseed changes output", "[math][noise]")
{
	sgc::PerlinNoise noise(42);
	const float v1 = noise.noise2D(1.5f, 2.7f);
	noise.reseed(999);
	const float v2 = noise.noise2D(1.5f, 2.7f);
	REQUIRE(v1 != v2);
}

TEST_CASE("PerlinNoise is continuous (nearby values are close)", "[math][noise]")
{
	sgc::PerlinNoise noise(42);
	const float a = noise.noise2D(5.0f, 5.0f);
	const float b = noise.noise2D(5.001f, 5.0f);
	REQUIRE(std::abs(a - b) < 0.01f);
}
