#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <vector>

#include "sgc/core/ThreadPool.hpp"

TEST_CASE("ThreadPool submit returns future", "[core][threadpool]")
{
	sgc::ThreadPool pool(2);
	auto future = pool.submit([] { return 42; });
	REQUIRE(future.get() == 42);
}

TEST_CASE("ThreadPool multiple tasks", "[core][threadpool]")
{
	sgc::ThreadPool pool(2);
	std::vector<std::future<int>> futures;
	for (int i = 0; i < 10; ++i)
	{
		futures.push_back(pool.submit([i] { return i * i; }));
	}

	for (int i = 0; i < 10; ++i)
	{
		REQUIRE(futures[i].get() == i * i);
	}
}

TEST_CASE("ThreadPool parallelFor", "[core][threadpool]")
{
	sgc::ThreadPool pool(4);
	std::atomic<int> sum{0};

	pool.parallelFor(0, 100, [&sum](std::size_t i) {
		sum += static_cast<int>(i);
	});

	// sum of 0..99 = 4950
	REQUIRE(sum.load() == 4950);
}

TEST_CASE("ThreadPool parallelFor empty range", "[core][threadpool]")
{
	sgc::ThreadPool pool(2);
	std::atomic<int> count{0};

	pool.parallelFor(5, 5, [&count](std::size_t) {
		++count;
	});
	REQUIRE(count.load() == 0);

	pool.parallelFor(10, 5, [&count](std::size_t) {
		++count;
	});
	REQUIRE(count.load() == 0);
}

TEST_CASE("ThreadPool threadCount", "[core][threadpool]")
{
	sgc::ThreadPool pool(3);
	REQUIRE(pool.threadCount() == 3);
}

TEST_CASE("ThreadPool submit with arguments", "[core][threadpool]")
{
	sgc::ThreadPool pool(2);
	auto future = pool.submit([](int a, int b) { return a + b; }, 10, 20);
	REQUIRE(future.get() == 30);
}

TEST_CASE("ThreadPool void task", "[core][threadpool]")
{
	sgc::ThreadPool pool(2);
	std::atomic<bool> done{false};

	auto future = pool.submit([&done] { done = true; });
	future.get();
	REQUIRE(done.load());
}

TEST_CASE("ThreadPool handles large workload", "[core][threadpool]")
{
	sgc::ThreadPool pool(4);
	std::atomic<int> count{0};

	std::vector<std::future<void>> futures;
	for (int i = 0; i < 1000; ++i)
	{
		futures.push_back(pool.submit([&count] { ++count; }));
	}
	for (auto& f : futures)
	{
		f.get();
	}
	REQUIRE(count.load() == 1000);
}
