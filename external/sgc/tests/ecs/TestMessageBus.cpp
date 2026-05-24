#include <catch2/catch_test_macros.hpp>

#include "sgc/ecs/SystemMessage.hpp"

using namespace sgc::ecs;

namespace
{

struct DamageEvent
{
	int entityId = 0;
	float amount = 0.0f;
};

struct HealEvent
{
	int entityId = 0;
	float amount = 0.0f;
};

} // namespace

TEST_CASE("MessageBus subscribe and publish", "[ecs][messagebus]")
{
	MessageBus bus;
	int received = 0;
	float lastAmount = 0.0f;

	bus.subscribe<DamageEvent>([&](const DamageEvent& e)
	{
		++received;
		lastAmount = e.amount;
	});

	bus.publish(DamageEvent{1, 25.0f});
	CHECK(received == 1);
	CHECK(lastAmount == 25.0f);

	bus.publish(DamageEvent{2, 50.0f});
	CHECK(received == 2);
	CHECK(lastAmount == 50.0f);
}

TEST_CASE("MessageBus multiple subscribers", "[ecs][messagebus]")
{
	MessageBus bus;
	int countA = 0;
	int countB = 0;

	bus.subscribe<DamageEvent>([&](const DamageEvent&) { ++countA; });
	bus.subscribe<DamageEvent>([&](const DamageEvent&) { ++countB; });

	bus.publish(DamageEvent{1, 10.0f});
	CHECK(countA == 1);
	CHECK(countB == 1);
}

TEST_CASE("MessageBus different message types are isolated", "[ecs][messagebus]")
{
	MessageBus bus;
	int damageCount = 0;
	int healCount = 0;

	bus.subscribe<DamageEvent>([&](const DamageEvent&) { ++damageCount; });
	bus.subscribe<HealEvent>([&](const HealEvent&) { ++healCount; });

	bus.publish(DamageEvent{1, 10.0f});
	CHECK(damageCount == 1);
	CHECK(healCount == 0);

	bus.publish(HealEvent{2, 20.0f});
	CHECK(damageCount == 1);
	CHECK(healCount == 1);
}

TEST_CASE("MessageBus clear removes all handlers", "[ecs][messagebus]")
{
	MessageBus bus;
	int count = 0;

	bus.subscribe<DamageEvent>([&](const DamageEvent&) { ++count; });
	bus.publish(DamageEvent{1, 10.0f});
	CHECK(count == 1);

	bus.clear();
	bus.publish(DamageEvent{2, 20.0f});
	CHECK(count == 1);  // ハンドラが削除されたので増えない
	CHECK(bus.typeCount() == 0);
}
