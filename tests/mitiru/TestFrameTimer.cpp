#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <mitiru/core/FrameTimer.hpp>

#include <thread>
#include <chrono>

// --- FrameTimer basic tests ---

TEST_CASE("FrameTimer initial state", "[mitiru][core][FrameTimer]")
{
	mitiru::FrameTimer timer;

	REQUIRE(timer.getFrameCount() == 0);
	REQUIRE(timer.getSmoothedDelta() == 0.0f);
	REQUIRE(timer.getAccumulator() == 0.0f);
	REQUIRE(timer.getFixedDeltaTime() == Catch::Approx(1.0f / 60.0f));
	REQUIRE(timer.getMaxDelta() == Catch::Approx(0.1f));
}

TEST_CASE("FrameTimer first tick returns zero delta", "[mitiru][core][FrameTimer]")
{
	mitiru::FrameTimer timer;

	const float dt = timer.tick();

	REQUIRE(dt == Catch::Approx(0.0f));
	REQUIRE(timer.getFrameCount() == 1);
}

TEST_CASE("FrameTimer frame count increments", "[mitiru][core][FrameTimer]")
{
	mitiru::FrameTimer timer;

	timer.tick();
	timer.tick();
	timer.tick();

	REQUIRE(timer.getFrameCount() == 3);
}

TEST_CASE("FrameTimer subsequent ticks produce positive delta", "[mitiru][core][FrameTimer]")
{
	mitiru::FrameTimer timer;

	timer.tick();
	/// 短いスリープで時間経過を作る
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	const float dt = timer.tick();

	REQUIRE(dt > 0.0f);
}

TEST_CASE("FrameTimer maxDelta caps large deltas", "[mitiru][core][FrameTimer]")
{
	mitiru::FrameTimer timer(1, 0.05f); ///< スムージングなし、上限0.05秒

	timer.tick();
	/// 60ms待機（上限50msを超える）
	std::this_thread::sleep_for(std::chrono::milliseconds(60));
	const float dt = timer.tick();

	/// スムージングフレーム数1なのでキャップ値がそのまま返る
	REQUIRE(dt <= 0.05f + 0.01f); ///< 誤差マージン
}

TEST_CASE("FrameTimer getFps returns valid value", "[mitiru][core][FrameTimer]")
{
	mitiru::FrameTimer timer;

	/// 初期状態ではFPS=0
	REQUIRE(timer.getFps() == 0.0f);

	timer.tick();
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	timer.tick();

	/// 何らかの正のFPS値が返る
	REQUIRE(timer.getFps() >= 0.0f);
}

TEST_CASE("FrameTimer fixed step accumulator", "[mitiru][core][FrameTimer]")
{
	mitiru::FrameTimer timer(1, 1.0f); ///< スムージング1、上限1秒
	timer.setFixedDeltaTime(1.0f / 60.0f);

	timer.tick();
	/// 20ms待機 → アキュムレータに蓄積
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	timer.tick();

	/// アキュムレータにデルタが蓄積されている
	REQUIRE(timer.getAccumulator() > 0.0f);
}

TEST_CASE("FrameTimer consumeFixedStep drains accumulator", "[mitiru][core][FrameTimer]")
{
	mitiru::FrameTimer timer(1, 1.0f);
	timer.setFixedDeltaTime(0.01f); ///< 10ms

	timer.tick();
	std::this_thread::sleep_for(std::chrono::milliseconds(30));
	timer.tick();

	/// アキュムレータに30ms分蓄積 → 10ms x 3回消費できるはず
	int steps = 0;
	while (timer.consumeFixedStep())
	{
		++steps;
	}
	REQUIRE(steps >= 1);
	REQUIRE(timer.getAccumulator() < timer.getFixedDeltaTime());
}

TEST_CASE("FrameTimer consumeFixedStep returns false when zero fixedDt", "[mitiru][core][FrameTimer]")
{
	mitiru::FrameTimer timer;
	timer.setFixedDeltaTime(0.0f);

	timer.tick();
	REQUIRE_FALSE(timer.consumeFixedStep());
}

TEST_CASE("FrameTimer reset clears state", "[mitiru][core][FrameTimer]")
{
	mitiru::FrameTimer timer;
	timer.tick();
	timer.tick();

	timer.reset();

	REQUIRE(timer.getFrameCount() == 0);
	REQUIRE(timer.getSmoothedDelta() == 0.0f);
	REQUIRE(timer.getAccumulator() == 0.0f);
}

TEST_CASE("FrameTimer custom smooth frames", "[mitiru][core][FrameTimer]")
{
	mitiru::FrameTimer timer(5); ///< 5フレームスムージング

	/// 複数tick してもクラッシュしない
	for (int i = 0; i < 20; ++i)
	{
		timer.tick();
	}

	REQUIRE(timer.getFrameCount() == 20);
}

TEST_CASE("FrameTimer setMaxDelta updates cap", "[mitiru][core][FrameTimer]")
{
	mitiru::FrameTimer timer;

	timer.setMaxDelta(0.2f);
	REQUIRE(timer.getMaxDelta() == Catch::Approx(0.2f));
}
