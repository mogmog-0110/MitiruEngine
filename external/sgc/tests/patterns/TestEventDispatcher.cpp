/// @file TestEventDispatcher.cpp
/// @brief EventDispatcher.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/patterns/EventDispatcher.hpp"

#include <atomic>
#include <thread>
#include <vector>

namespace
{
struct DamageEvent { int amount; };
struct HealEvent { int amount; };
} // namespace

TEST_CASE("EventDispatcher emits to listener", "[patterns][event]")
{
	sgc::EventDispatcher dispatcher;
	int received = 0;

	dispatcher.on<DamageEvent>([&](const DamageEvent& e) { received = e.amount; });
	dispatcher.emit(DamageEvent{42});

	REQUIRE(received == 42);
}

TEST_CASE("EventDispatcher supports multiple listeners", "[patterns][event]")
{
	sgc::EventDispatcher dispatcher;
	int count = 0;

	dispatcher.on<DamageEvent>([&](const DamageEvent&) { ++count; });
	dispatcher.on<DamageEvent>([&](const DamageEvent&) { ++count; });
	dispatcher.emit(DamageEvent{10});

	REQUIRE(count == 2);
}

TEST_CASE("EventDispatcher different event types are independent", "[patterns][event]")
{
	sgc::EventDispatcher dispatcher;
	int damageCount = 0;
	int healCount = 0;

	dispatcher.on<DamageEvent>([&](const DamageEvent&) { ++damageCount; });
	dispatcher.on<HealEvent>([&](const HealEvent&) { ++healCount; });

	dispatcher.emit(DamageEvent{10});

	REQUIRE(damageCount == 1);
	REQUIRE(healCount == 0);
}

TEST_CASE("EventDispatcher off removes listener", "[patterns][event]")
{
	sgc::EventDispatcher dispatcher;
	int count = 0;

	auto id = dispatcher.on<DamageEvent>([&](const DamageEvent&) { ++count; });
	dispatcher.emit(DamageEvent{10});
	REQUIRE(count == 1);

	dispatcher.off(id);
	dispatcher.emit(DamageEvent{10});
	REQUIRE(count == 1);
}

TEST_CASE("EventDispatcher clearListeners removes all for type", "[patterns][event]")
{
	sgc::EventDispatcher dispatcher;
	int count = 0;

	dispatcher.on<DamageEvent>([&](const DamageEvent&) { ++count; });
	dispatcher.on<DamageEvent>([&](const DamageEvent&) { ++count; });
	dispatcher.clearListeners<DamageEvent>();
	dispatcher.emit(DamageEvent{10});

	REQUIRE(count == 0);
}

TEST_CASE("EventDispatcher clearAll removes all event types", "[patterns][event]")
{
	sgc::EventDispatcher dispatcher;
	int damageCount = 0;
	int healCount = 0;

	dispatcher.on<DamageEvent>([&](const DamageEvent&) { ++damageCount; });
	dispatcher.on<HealEvent>([&](const HealEvent&) { ++healCount; });
	dispatcher.clearAll();

	dispatcher.emit(DamageEvent{10});
	dispatcher.emit(HealEvent{20});

	REQUIRE(damageCount == 0);
	REQUIRE(healCount == 0);
}

TEST_CASE("EventDispatcher emit with no listeners is safe", "[patterns][event]")
{
	sgc::EventDispatcher dispatcher;
	// リスナーなしでemitしてもクラッシュしない
	dispatcher.emit(DamageEvent{42});
	REQUIRE(true);
}

TEST_CASE("EventDispatcher off during emit is safe (reentry)", "[patterns][event]")
{
	sgc::EventDispatcher dispatcher;
	int callCount = 0;
	sgc::ListenerId idToRemove = 0;

	// リスナー1: 自分自身をoff()で解除する
	idToRemove = dispatcher.on<DamageEvent>([&](const DamageEvent&) {
		++callCount;
		dispatcher.off(idToRemove);
	});

	// リスナー2: 通常のリスナー
	dispatcher.on<DamageEvent>([&](const DamageEvent&) {
		++callCount;
	});

	// emit中にoff()が呼ばれてもクラッシュしない
	dispatcher.emit(DamageEvent{10});
	REQUIRE(callCount == 2);

	// リスナー1は解除済み、リスナー2のみ呼ばれる
	callCount = 0;
	dispatcher.emit(DamageEvent{20});
	REQUIRE(callCount == 1);
}

// ── priority ────────────────────────────────────────────

TEST_CASE("EventDispatcher high priority listeners fire first", "[patterns][event]")
{
	sgc::EventDispatcher dispatcher;
	std::vector<std::string> order;

	dispatcher.on<DamageEvent>([&](const DamageEvent&) { order.push_back("low"); }, sgc::Priority::Low);
	dispatcher.on<DamageEvent>([&](const DamageEvent&) { order.push_back("high"); }, sgc::Priority::High);
	dispatcher.on<DamageEvent>([&](const DamageEvent&) { order.push_back("normal"); }, sgc::Priority::Normal);

	dispatcher.emit(DamageEvent{10});

	REQUIRE(order.size() == 3);
	REQUIRE(order[0] == "high");
	REQUIRE(order[1] == "normal");
	REQUIRE(order[2] == "low");
}

// ── once ────────────────────────────────────────────────

TEST_CASE("EventDispatcher once fires only once", "[patterns][event]")
{
	sgc::EventDispatcher dispatcher;
	int count = 0;

	dispatcher.once<DamageEvent>([&](const DamageEvent&) { ++count; });
	dispatcher.emit(DamageEvent{10});
	dispatcher.emit(DamageEvent{20});

	REQUIRE(count == 1);
}

TEST_CASE("EventDispatcher once with priority", "[patterns][event]")
{
	sgc::EventDispatcher dispatcher;
	std::vector<std::string> order;

	dispatcher.on<DamageEvent>([&](const DamageEvent&) { order.push_back("normal"); });
	dispatcher.once<DamageEvent>([&](const DamageEvent&) { order.push_back("once-high"); }, sgc::Priority::High);

	dispatcher.emit(DamageEvent{10});
	REQUIRE(order[0] == "once-high");
	REQUIRE(order[1] == "normal");

	order.clear();
	dispatcher.emit(DamageEvent{20});
	REQUIRE(order.size() == 1);
	REQUIRE(order[0] == "normal");
}

// ── default priority backward compat ────────────────────

TEST_CASE("EventDispatcher default priority is Normal", "[patterns][event]")
{
	sgc::EventDispatcher dispatcher;
	int count = 0;
	dispatcher.on<DamageEvent>([&](const DamageEvent&) { ++count; });
	dispatcher.emit(DamageEvent{10});
	REQUIRE(count == 1);
}

TEST_CASE("EventDispatcher concurrent emit is safe", "[patterns][event]")
{
	sgc::EventDispatcher dispatcher;
	std::atomic<int> totalCount{0};

	dispatcher.on<DamageEvent>([&](const DamageEvent& e) {
		totalCount.fetch_add(e.amount);
	});

	std::vector<std::thread> threads;
	for (int i = 0; i < 8; ++i)
	{
		threads.emplace_back([&dispatcher]()
		{
			for (int j = 0; j < 100; ++j)
			{
				dispatcher.emit(DamageEvent{1});
			}
		});
	}
	for (auto& t : threads) t.join();

	REQUIRE(totalCount.load() == 800);
}
