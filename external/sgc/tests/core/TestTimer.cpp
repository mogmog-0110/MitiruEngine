/// @file TestTimer.cpp
/// @brief Timer.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/core/Timer.hpp"

#include <thread>

using Catch::Approx;

TEST_CASE("Stopwatch measures elapsed time", "[core][timer]")
{
	sgc::Stopwatch sw;
	sw.start();
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	sw.stop();

	REQUIRE(sw.elapsedMilliseconds() > 5.0);
	REQUIRE(sw.elapsedSeconds() > 0.005);
}

TEST_CASE("Stopwatch restart resets and starts", "[core][timer]")
{
	sgc::Stopwatch sw;
	sw.start();
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	sw.restart();

	REQUIRE(sw.isRunning());
	REQUIRE(sw.elapsedMilliseconds() < 10.0);
}

TEST_CASE("DeltaClock returns positive delta", "[core][timer]")
{
	sgc::DeltaClock clock;
	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	float dt = clock.tick();

	REQUIRE(dt > 0.0f);
	REQUIRE(dt < 1.0f);
}

TEST_CASE("Stopwatch reset clears elapsed and stops", "[core][timer]")
{
	sgc::Stopwatch sw;
	sw.start();
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	sw.reset();

	REQUIRE_FALSE(sw.isRunning());
	REQUIRE(sw.elapsedMilliseconds() < 1.0);
}

TEST_CASE("DeltaClock reset makes next tick small", "[core][timer]")
{
	sgc::DeltaClock clock;
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	(void)clock.tick(); // 消費

	clock.reset();
	float dt = clock.tick();
	// reset直後のtickは非常に小さいデルタを返す
	REQUIRE(dt < 0.05f);
}

TEST_CASE("Stopwatch elapsedSecondsF returns float", "[core][timer]")
{
	sgc::Stopwatch sw;
	sw.start();
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	sw.stop();

	float elapsed = sw.elapsedSecondsF();
	REQUIRE(elapsed > 0.005f);
	REQUIRE(elapsed < 1.0f);
}

// ── DeltaClock TimeScale ────────────────────────────────

TEST_CASE("DeltaClock timeScale defaults to 1", "[core][timer]")
{
	sgc::DeltaClock clock;
	REQUIRE(clock.getTimeScale() == 1.0f);
}

TEST_CASE("DeltaClock timeScale scales delta", "[core][timer]")
{
	sgc::DeltaClock clock;
	clock.setTimeScale(2.0f);
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	float dt = clock.tick();
	// 2xスケールなので実際の2倍程度
	REQUIRE(dt > 0.01f);
}

TEST_CASE("DeltaClock getFPS returns inverse of dt", "[core][timer]")
{
	sgc::DeltaClock clock;
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	(void)clock.tick();
	float fps = clock.getFPS();
	REQUIRE(fps > 0.0f);
	REQUIRE(fps < 10000.0f);
}

// ── CountdownTimer ──────────────────────────────────────

TEST_CASE("CountdownTimer starts not expired", "[core][timer]")
{
	sgc::CountdownTimer timer(3.0f);
	REQUIRE_FALSE(timer.isExpired());
	REQUIRE(timer.remaining() == 3.0f);
	REQUIRE(timer.progress() == Catch::Approx(0.0f));
}

TEST_CASE("CountdownTimer expires after enough updates", "[core][timer]")
{
	sgc::CountdownTimer timer(1.0f);
	timer.update(0.5f);
	REQUIRE_FALSE(timer.isExpired());
	REQUIRE(timer.remaining() == Catch::Approx(0.5f));

	timer.update(0.6f);
	REQUIRE(timer.isExpired());
	REQUIRE(timer.remaining() == 0.0f);
	REQUIRE(timer.progress() == Catch::Approx(1.0f));
}

TEST_CASE("CountdownTimer restart resets", "[core][timer]")
{
	sgc::CountdownTimer timer(1.0f);
	timer.update(1.5f);
	REQUIRE(timer.isExpired());
	timer.restart();
	REQUIRE_FALSE(timer.isExpired());
	REQUIRE(timer.remaining() == 1.0f);
}

TEST_CASE("CountdownTimer progress at midpoint", "[core][timer]")
{
	sgc::CountdownTimer timer(2.0f);
	timer.update(1.0f);
	REQUIRE(timer.progress() == Catch::Approx(0.5f));
}

// ── IntervalTimer ───────────────────────────────────────

TEST_CASE("IntervalTimer fires after interval", "[core][timer]")
{
	sgc::IntervalTimer timer(0.5f);
	timer.update(0.3f);
	REQUIRE_FALSE(timer.consume());

	timer.update(0.3f);
	REQUIRE(timer.consume());
	// 消費後はもう1回分は溜まっていない
	REQUIRE_FALSE(timer.consume());
}

TEST_CASE("IntervalTimer fires multiple times", "[core][timer]")
{
	sgc::IntervalTimer timer(0.1f);
	timer.update(0.35f);

	int fires = 0;
	while (timer.consume()) ++fires;
	REQUIRE(fires == 3);
}

TEST_CASE("IntervalTimer setInterval changes interval", "[core][timer]")
{
	sgc::IntervalTimer timer(1.0f);
	REQUIRE(timer.interval() == 1.0f);
	timer.setInterval(0.5f);
	REQUIRE(timer.interval() == 0.5f);
}
