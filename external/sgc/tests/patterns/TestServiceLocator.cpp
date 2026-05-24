/// @file TestServiceLocator.cpp
/// @brief ServiceLocator.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/patterns/ServiceLocator.hpp"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace
{
struct IAudio
{
	virtual ~IAudio() = default;
	virtual std::string name() const = 0;
};

struct MockAudio : IAudio
{
	std::string name() const override { return "MockAudio"; }
};
} // namespace

TEST_CASE("ServiceLocator provide and get", "[patterns][service]")
{
	sgc::ServiceLocator locator;
	locator.provide<IAudio>(std::make_shared<MockAudio>());

	REQUIRE(locator.has<IAudio>());
	REQUIRE(locator.get<IAudio>().name() == "MockAudio");
}

TEST_CASE("ServiceLocator has returns false for unregistered", "[patterns][service]")
{
	sgc::ServiceLocator locator;
	REQUIRE_FALSE(locator.has<IAudio>());
}

TEST_CASE("ServiceLocator get throws for unregistered", "[patterns][service]")
{
	sgc::ServiceLocator locator;
	REQUIRE_THROWS_AS(locator.get<IAudio>(), std::runtime_error);
}

TEST_CASE("ServiceLocator remove unregisters service", "[patterns][service]")
{
	sgc::ServiceLocator locator;
	locator.provide<IAudio>(std::make_shared<MockAudio>());
	locator.remove<IAudio>();

	REQUIRE_FALSE(locator.has<IAudio>());
}

TEST_CASE("ServiceLocator clear removes all", "[patterns][service]")
{
	sgc::ServiceLocator locator;
	locator.provide<IAudio>(std::make_shared<MockAudio>());
	locator.clear();

	REQUIRE_FALSE(locator.has<IAudio>());
}

TEST_CASE("ServiceLocator concurrent access is safe", "[patterns][service]")
{
	sgc::ServiceLocator locator;
	locator.provide<IAudio>(std::make_shared<MockAudio>());

	std::atomic<int> failCount{0};
	std::vector<std::thread> threads;
	for (int i = 0; i < 8; ++i)
	{
		threads.emplace_back([&locator, &failCount]()
		{
			for (int j = 0; j < 100; ++j)
			{
				if (!locator.has<IAudio>()) failCount.fetch_add(1);
				if (locator.get<IAudio>().name() != "MockAudio") failCount.fetch_add(1);
			}
		});
	}
	for (auto& t : threads) t.join();
	REQUIRE(failCount.load() == 0);
}
