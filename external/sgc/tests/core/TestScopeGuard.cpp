/// @file TestScopeGuard.cpp
/// @brief ScopeGuard.hpp のユニットテスト — スコープガードの動作を検証

#include <catch2/catch_test_macros.hpp>

#include "sgc/core/ScopeGuard.hpp"

#include <vector>

TEST_CASE("ScopeGuard executes on scope exit", "[core][scopeguard]")
{
	int counter = 0;
	{
		auto guard = sgc::ScopeGuard([&counter]() { ++counter; });
		REQUIRE(counter == 0);
	}
	REQUIRE(counter == 1);
}

TEST_CASE("ScopeGuard can be dismissed", "[core][scopeguard]")
{
	int counter = 0;
	{
		auto guard = sgc::ScopeGuard([&counter]() { ++counter; });
		guard.dismiss();
		REQUIRE_FALSE(guard.isActive());
	}
	REQUIRE(counter == 0);
}

TEST_CASE("ScopeGuard move transfers ownership", "[core][scopeguard]")
{
	int counter = 0;
	{
		auto guard1 = sgc::ScopeGuard([&counter]() { ++counter; });
		auto guard2 = std::move(guard1);
		REQUIRE_FALSE(guard1.isActive());  // NOLINT: ムーブ後の状態をテスト
		REQUIRE(guard2.isActive());
	}
	REQUIRE(counter == 1);
}

TEST_CASE("makeScopeGuard factory works", "[core][scopeguard]")
{
	int counter = 0;
	{
		auto guard = sgc::makeScopeGuard([&counter]() { counter += 10; });
		REQUIRE(guard.isActive());
	}
	REQUIRE(counter == 10);
}

TEST_CASE("SGC_SCOPE_EXIT macro works", "[core][scopeguard]")
{
	int counter = 0;
	{
		SGC_SCOPE_EXIT([&counter]() { counter += 5; });
		REQUIRE(counter == 0);
	}
	REQUIRE(counter == 5);
}

TEST_CASE("Multiple ScopeGuards execute in reverse order", "[core][scopeguard]")
{
	std::vector<int> order;
	{
		auto guard1 = sgc::ScopeGuard([&order]() { order.push_back(1); });
		auto guard2 = sgc::ScopeGuard([&order]() { order.push_back(2); });
		auto guard3 = sgc::ScopeGuard([&order]() { order.push_back(3); });
	}
	// デストラクタは構築の逆順に実行される
	REQUIRE(order == std::vector<int>{3, 2, 1});
}
