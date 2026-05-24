/// @file TestFpsCounter.cpp
/// @brief FpsCounter.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/debug/FpsCounter.hpp"

using Catch::Approx;

TEST_CASE("FpsCounter initial values are zero", "[debug][fps]")
{
	sgc::debug::FpsCounter fps;
	REQUIRE(fps.fps() == 0.0f);
	REQUIRE(fps.frameTime() == 0.0f);
	REQUIRE(fps.frameTimeMs() == 0.0f);
}

TEST_CASE("FpsCounter calculates FPS after interval", "[debug][fps]")
{
	sgc::debug::FpsCounter fps(0.5f);

	// 60FPS: 各フレーム約16.67ms
	constexpr float dt = 1.0f / 60.0f;
	float elapsed = 0.0f;

	while (elapsed < 0.5f)
	{
		fps.update(dt);
		elapsed += dt;
	}

	// 0.5秒間に約30フレーム → FPS ≈ 60
	REQUIRE(fps.fps() == Approx(60.0f).margin(2.0f));
}

TEST_CASE("FpsCounter frameTime calculation", "[debug][fps]")
{
	sgc::debug::FpsCounter fps(0.5f);

	// 10フレームを0.5秒で → 1フレーム = 0.05秒
	for (int i = 0; i < 10; ++i)
	{
		fps.update(0.05f);
	}

	REQUIRE(fps.frameTime() == Approx(0.05f).margin(0.001f));
	REQUIRE(fps.frameTimeMs() == Approx(50.0f).margin(1.0f));
}

TEST_CASE("FpsCounter does not update before interval", "[debug][fps]")
{
	sgc::debug::FpsCounter fps(1.0f);  // 1秒間隔

	fps.update(0.016f);
	// まだ1秒未満 → FPSは更新されない
	REQUIRE(fps.fps() == 0.0f);
}

TEST_CASE("FpsCounter reset clears values", "[debug][fps]")
{
	sgc::debug::FpsCounter fps(0.1f);

	// FPSを計算させる
	for (int i = 0; i < 10; ++i)
	{
		fps.update(0.02f);
	}
	REQUIRE(fps.fps() > 0.0f);

	fps.reset();
	REQUIRE(fps.fps() == 0.0f);
	REQUIRE(fps.frameTime() == 0.0f);
	REQUIRE(fps.frameTimeMs() == 0.0f);
}

TEST_CASE("FpsCounter custom update interval", "[debug][fps]")
{
	sgc::debug::FpsCounter fps(0.25f);  // 0.25秒間隔

	// 0.25秒で5フレーム → 20FPS
	for (int i = 0; i < 5; ++i)
	{
		fps.update(0.05f);
	}

	REQUIRE(fps.fps() == Approx(20.0f).margin(0.5f));
}
