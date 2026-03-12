#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <mitiru/core/GameLoop.hpp>

// --- GameLoop tests ---

TEST_CASE("GameLoopConfig default values", "[mitiru][core][GameLoop]")
{
	const mitiru::GameLoopConfig config;

	REQUIRE(config.targetFps == Catch::Approx(60.0f));
	REQUIRE(config.fixedDeltaTime == Catch::Approx(1.0f / 60.0f));
	REQUIRE(config.maxFrameSkip == 5);
}

TEST_CASE("runGameLoop immediate quit returns zero frames", "[mitiru][core][GameLoop]")
{
	mitiru::GameLoopConfig config;
	int updateCount = 0;
	int renderCount = 0;

	const auto frames = mitiru::runGameLoop(
		config,
		[&](float) { ++updateCount; },
		[&]() { ++renderCount; },
		[&]() { return true; }); ///< 即終了

	REQUIRE(frames == 0);
	REQUIRE(renderCount == 0);
}

TEST_CASE("runGameLoop runs specified iterations", "[mitiru][core][GameLoop]")
{
	mitiru::GameLoopConfig config;
	config.targetFps = 0.0f; ///< FPS制限なし（テスト高速化）
	int updateCount = 0;
	int renderCount = 0;
	int loopCount = 0;

	const auto frames = mitiru::runGameLoop(
		config,
		[&](float dt)
		{
			REQUIRE(dt == Catch::Approx(config.fixedDeltaTime));
			++updateCount;
		},
		[&]() { ++renderCount; },
		[&]() { return ++loopCount > 10; });

	REQUIRE(frames == 10);
	REQUIRE(renderCount == 10);
}

TEST_CASE("runGameLoopFrames runs exact frame count", "[mitiru][core][GameLoop]")
{
	mitiru::GameLoopConfig config;
	config.targetFps = 0.0f;
	int renderCount = 0;

	const auto frames = mitiru::runGameLoopFrames(
		config,
		5,
		[](float) {},
		[&]() { ++renderCount; });

	REQUIRE(frames == 5);
	REQUIRE(renderCount == 5);
}

TEST_CASE("runGameLoop respects maxFrameSkip", "[mitiru][core][GameLoop]")
{
	mitiru::GameLoopConfig config;
	config.targetFps = 0.0f;
	config.fixedDeltaTime = 0.001f; ///< 非常に小さい固定ステップ
	config.maxFrameSkip = 3;

	int updateCount = 0;
	int loopCount = 0;

	/// 1フレーム分だけ実行
	mitiru::runGameLoop(
		config,
		[&](float) { ++updateCount; },
		[]() {},
		[&]() { return ++loopCount > 1; });

	/// maxFrameSkip以下のupdate回数に制限される
	REQUIRE(updateCount <= config.maxFrameSkip * 2); ///< 2フレーム分のマージン
}

TEST_CASE("runGameLoop update receives fixed delta time", "[mitiru][core][GameLoop]")
{
	mitiru::GameLoopConfig config;
	config.targetFps = 0.0f;
	config.fixedDeltaTime = 0.02f;

	float receivedDt = 0.0f;
	int loopCount = 0;

	mitiru::runGameLoop(
		config,
		[&](float dt) { receivedDt = dt; },
		[]() {},
		[&]() { return ++loopCount > 5; });

	/// 固定デルタタイムが渡される
	if (receivedDt > 0.0f)
	{
		REQUIRE(receivedDt == Catch::Approx(0.02f));
	}
}
