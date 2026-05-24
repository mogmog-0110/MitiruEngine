/// @file TestCoroutine.cpp
/// @brief Coroutine.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/core/Coroutine.hpp"

// ── テスト用コルーチン ──────────────────────────────────────────

static sgc::Task simpleCoroutine(int& counter)
{
	++counter;
	co_await sgc::WaitForNextFrame{};
	++counter;
	co_await sgc::WaitForNextFrame{};
	++counter;
	co_return;
}

static sgc::Task timedCoroutine(int& counter)
{
	++counter;
	co_await sgc::WaitForSeconds{0.5f};
	++counter;
	co_return;
}

static sgc::Task conditionCoroutine(int& counter, bool& flag)
{
	++counter;
	co_await sgc::WaitUntil{[&flag]() { return flag; }};
	++counter;
	co_return;
}

static sgc::Generator<int> rangeGenerator(int start, int end)
{
	for (int i = start; i < end; ++i)
	{
		co_yield i;
	}
}

static sgc::Generator<int> fibGenerator()
{
	int a = 0, b = 1;
	while (true)
	{
		co_yield a;
		int next = a + b;
		a = b;
		b = next;
	}
}

// ── テスト ────────────────────────────────────────────────

TEST_CASE("Task basic resume and done", "[core][coroutine]")
{
	int counter = 0;
	auto task = simpleCoroutine(counter);
	REQUIRE(counter == 0);      // initial_suspendで停止中
	REQUIRE_FALSE(task.done());

	task.resume();  // initial_suspendから再開 → 最初のco_awaitまで実行
	REQUIRE(counter == 1);

	task.resume();
	REQUIRE(counter == 2);

	task.resume();
	REQUIRE(counter == 3);
	REQUIRE(task.done());
}

TEST_CASE("Task cancel", "[core][coroutine]")
{
	int counter = 0;
	auto task = simpleCoroutine(counter);
	task.resume();
	REQUIRE(counter == 1);

	task.cancel();
	REQUIRE(task.done());
}

TEST_CASE("Task move semantics", "[core][coroutine]")
{
	int counter = 0;
	auto task1 = simpleCoroutine(counter);
	task1.resume();
	REQUIRE(counter == 1);

	auto task2 = std::move(task1);
	REQUIRE(task1.done());  // ムーブ後はnull
	REQUIRE_FALSE(task2.done());

	task2.resume();
	REQUIRE(counter == 2);
}

TEST_CASE("Generator produces values", "[core][coroutine]")
{
	auto gen = rangeGenerator(0, 5);
	std::vector<int> values;

	for (int v : gen)
	{
		values.push_back(v);
	}

	REQUIRE(values.size() == 5);
	REQUIRE(values[0] == 0);
	REQUIRE(values[4] == 4);
}

TEST_CASE("Generator fibonacci", "[core][coroutine]")
{
	auto gen = fibGenerator();
	std::vector<int> values;

	int count = 0;
	for (int v : gen)
	{
		values.push_back(v);
		if (++count >= 8) break;
	}

	REQUIRE(values.size() == 8);
	REQUIRE(values[0] == 0);
	REQUIRE(values[1] == 1);
	REQUIRE(values[2] == 1);
	REQUIRE(values[3] == 2);
	REQUIRE(values[4] == 3);
	REQUIRE(values[5] == 5);
	REQUIRE(values[6] == 8);
	REQUIRE(values[7] == 13);
}

TEST_CASE("CoroutineScheduler basic lifecycle", "[core][coroutine]")
{
	sgc::CoroutineScheduler scheduler;
	int counter = 0;

	scheduler.start(simpleCoroutine(counter));
	REQUIRE(scheduler.activeCount() == 1);
	REQUIRE(counter == 1);

	scheduler.tick(0.016f);  // 1フレーム進める
	REQUIRE(counter == 2);

	scheduler.tick(0.016f);  // もう1フレーム
	REQUIRE(counter == 3);

	scheduler.tick(0.016f);  // 完了後のtick
	REQUIRE(scheduler.activeCount() == 0);
}

TEST_CASE("CoroutineScheduler WaitForSeconds", "[core][coroutine]")
{
	sgc::CoroutineScheduler scheduler;
	int counter = 0;

	scheduler.start(timedCoroutine(counter));
	REQUIRE(counter == 1);

	scheduler.tick(0.2f);  // 0.2秒 → まだ待機中
	REQUIRE(counter == 1);

	scheduler.tick(0.2f);  // 0.4秒 → まだ待機中
	REQUIRE(counter == 1);

	scheduler.tick(0.2f);  // 0.6秒 → 0.5秒超過、再開
	REQUIRE(counter == 2);
}

TEST_CASE("CoroutineScheduler WaitUntil", "[core][coroutine]")
{
	sgc::CoroutineScheduler scheduler;
	int counter = 0;
	bool flag = false;

	scheduler.start(conditionCoroutine(counter, flag));
	REQUIRE(counter == 1);

	scheduler.tick(0.016f);  // 条件未達
	REQUIRE(counter == 1);

	flag = true;
	scheduler.tick(0.016f);  // 条件達成
	REQUIRE(counter == 2);
}

TEST_CASE("CoroutineScheduler cancel", "[core][coroutine]")
{
	sgc::CoroutineScheduler scheduler;
	int counter = 0;

	auto id = scheduler.start(simpleCoroutine(counter));
	REQUIRE(scheduler.activeCount() == 1);

	scheduler.cancel(id);
	REQUIRE(scheduler.activeCount() == 0);
}

TEST_CASE("CoroutineScheduler cancelAll", "[core][coroutine]")
{
	sgc::CoroutineScheduler scheduler;
	int c1 = 0, c2 = 0;

	scheduler.start(simpleCoroutine(c1));
	scheduler.start(simpleCoroutine(c2));
	REQUIRE(scheduler.activeCount() == 2);

	scheduler.cancelAll();
	REQUIRE(scheduler.activeCount() == 0);
}

TEST_CASE("CoroutineScheduler multiple tasks", "[core][coroutine]")
{
	sgc::CoroutineScheduler scheduler;
	int c1 = 0, c2 = 0;

	scheduler.start(simpleCoroutine(c1));
	scheduler.start(simpleCoroutine(c2));
	REQUIRE(c1 == 1);
	REQUIRE(c2 == 1);

	scheduler.tick(0.016f);
	REQUIRE(c1 == 2);
	REQUIRE(c2 == 2);
}
