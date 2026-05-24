/// @file TestECS.cpp
/// @brief ECS (Entity, ComponentStorage, World) のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/ecs/World.hpp"

// ── テスト用コンポーネント ────────────────────────────────────

namespace
{

struct Position
{
	float x{0.0f};
	float y{0.0f};
};

struct Velocity
{
	float dx{0.0f};
	float dy{0.0f};
};

struct Health
{
	int hp{100};
};

} // namespace

// ── Entity ──────────────────────────────────────────────────────

TEST_CASE("Entity default is invalid", "[ecs][entity]")
{
	sgc::ecs::Entity e;
	REQUIRE_FALSE(e.isValid());
	REQUIRE(e.id == sgc::ecs::INVALID_ENTITY_ID);
}

TEST_CASE("Entity with valid id is valid", "[ecs][entity]")
{
	sgc::ecs::Entity e{0, 0};
	REQUIRE(e.isValid());
}

TEST_CASE("Entity equality comparison", "[ecs][entity]")
{
	sgc::ecs::Entity a{1, 0};
	sgc::ecs::Entity b{1, 0};
	sgc::ecs::Entity c{1, 1};
	REQUIRE(a == b);
	REQUIRE_FALSE(a == c);
}

// ── ComponentStorage ────────────────────────────────────────────

TEST_CASE("ComponentStorage add and get", "[ecs][storage]")
{
	sgc::ecs::ComponentStorage<Position> storage;
	storage.add(0, Position{10.0f, 20.0f});

	REQUIRE(storage.has(0));
	REQUIRE(storage.size() == 1);

	auto* p = storage.get(0);
	REQUIRE(p != nullptr);
	REQUIRE(p->x == 10.0f);
	REQUIRE(p->y == 20.0f);
}

TEST_CASE("ComponentStorage remove with swap-and-pop", "[ecs][storage]")
{
	sgc::ecs::ComponentStorage<Position> storage;
	storage.add(0, Position{1.0f, 1.0f});
	storage.add(1, Position{2.0f, 2.0f});
	storage.add(2, Position{3.0f, 3.0f});

	storage.remove(0); // swap entity 2 into slot 0

	REQUIRE_FALSE(storage.has(0));
	REQUIRE(storage.has(1));
	REQUIRE(storage.has(2));
	REQUIRE(storage.size() == 2);

	// entity 2 should still be retrievable
	auto* p2 = storage.get(2);
	REQUIRE(p2 != nullptr);
	REQUIRE(p2->x == 3.0f);
}

TEST_CASE("ComponentStorage has returns false for missing", "[ecs][storage]")
{
	sgc::ecs::ComponentStorage<Position> storage;
	REQUIRE_FALSE(storage.has(99));
	REQUIRE(storage.get(99) == nullptr);
}

TEST_CASE("ComponentStorage entities and components parallel", "[ecs][storage]")
{
	sgc::ecs::ComponentStorage<int> storage;
	storage.add(5, 50);
	storage.add(10, 100);

	const auto& entities = storage.entities();
	const auto& components = storage.components();

	REQUIRE(entities.size() == 2);
	REQUIRE(components.size() == 2);
	// entities[i] corresponds to components[i]
	for (std::size_t i = 0; i < entities.size(); ++i)
	{
		REQUIRE(*storage.get(entities[i]) == components[i]);
	}
}

// ── World ───────────────────────────────────────────────────────

TEST_CASE("World createEntity returns valid entity", "[ecs][world]")
{
	sgc::ecs::World world;
	auto e = world.createEntity();
	REQUIRE(e.isValid());
	REQUIRE(world.isAlive(e));
	REQUIRE(world.entityCount() == 1);
}

TEST_CASE("World destroyEntity marks entity as dead", "[ecs][world]")
{
	sgc::ecs::World world;
	auto e = world.createEntity();
	world.destroyEntity(e);

	REQUIRE_FALSE(world.isAlive(e));
	REQUIRE(world.entityCount() == 0);
}

TEST_CASE("World generation detects stale entity", "[ecs][world]")
{
	sgc::ecs::World world;
	auto e1 = world.createEntity();
	world.destroyEntity(e1);

	// 同じIDが再利用されるが、世代が違う
	auto e2 = world.createEntity();
	REQUIRE(e1.id == e2.id);
	REQUIRE(e1.generation != e2.generation);
	REQUIRE_FALSE(world.isAlive(e1));
	REQUIRE(world.isAlive(e2));
}

TEST_CASE("World addComponent and getComponent", "[ecs][world]")
{
	sgc::ecs::World world;
	auto e = world.createEntity();

	world.addComponent(e, Position{5.0f, 10.0f});

	auto* p = world.getComponent<Position>(e);
	REQUIRE(p != nullptr);
	REQUIRE(p->x == 5.0f);
	REQUIRE(p->y == 10.0f);
}

TEST_CASE("World removeComponent", "[ecs][world]")
{
	sgc::ecs::World world;
	auto e = world.createEntity();
	world.addComponent(e, Position{1.0f, 2.0f});

	world.removeComponent<Position>(e);
	REQUIRE_FALSE(world.hasComponent<Position>(e));
	REQUIRE(world.getComponent<Position>(e) == nullptr);
}

TEST_CASE("World hasComponent", "[ecs][world]")
{
	sgc::ecs::World world;
	auto e = world.createEntity();

	REQUIRE_FALSE(world.hasComponent<Position>(e));
	world.addComponent(e, Position{0.0f, 0.0f});
	REQUIRE(world.hasComponent<Position>(e));
}

TEST_CASE("World forEach single component", "[ecs][world]")
{
	sgc::ecs::World world;

	auto e1 = world.createEntity();
	auto e2 = world.createEntity();
	world.addComponent(e1, Position{1.0f, 0.0f});
	world.addComponent(e2, Position{2.0f, 0.0f});

	float sum = 0.0f;
	world.forEach<Position>([&sum](Position& p)
	{
		sum += p.x;
	});

	REQUIRE(sum == 3.0f);
}

TEST_CASE("World forEach multiple components", "[ecs][world]")
{
	sgc::ecs::World world;

	auto e1 = world.createEntity();
	auto e2 = world.createEntity();
	auto e3 = world.createEntity();

	world.addComponent(e1, Position{0.0f, 0.0f});
	world.addComponent(e1, Velocity{1.0f, 2.0f});

	world.addComponent(e2, Position{10.0f, 10.0f});
	world.addComponent(e2, Velocity{-1.0f, -1.0f});

	world.addComponent(e3, Position{99.0f, 99.0f});
	// e3 has no Velocity → should be skipped

	world.forEach<Position, Velocity>([](Position& pos, Velocity& vel)
	{
		pos.x += vel.dx;
		pos.y += vel.dy;
	});

	REQUIRE(world.getComponent<Position>(e1)->x == 1.0f);
	REQUIRE(world.getComponent<Position>(e1)->y == 2.0f);
	REQUIRE(world.getComponent<Position>(e2)->x == 9.0f);
	// e3 should be unchanged
	REQUIRE(world.getComponent<Position>(e3)->x == 99.0f);
}

TEST_CASE("World forEach skips entities missing components", "[ecs][world]")
{
	sgc::ecs::World world;

	auto e = world.createEntity();
	world.addComponent(e, Position{1.0f, 1.0f});
	// No Velocity

	int count = 0;
	world.forEach<Position, Velocity>([&count](Position&, Velocity&)
	{
		++count;
	});

	REQUIRE(count == 0);
}

TEST_CASE("World entity reuse after destroy", "[ecs][world]")
{
	sgc::ecs::World world;
	auto e1 = world.createEntity();
	world.addComponent(e1, Health{50});
	world.destroyEntity(e1);

	auto e2 = world.createEntity();
	// e2 should reuse e1's ID but with new generation
	REQUIRE(e2.id == e1.id);
	REQUIRE_FALSE(world.hasComponent<Health>(e2));
}

TEST_CASE("World large entity count", "[ecs][world]")
{
	sgc::ecs::World world;
	std::vector<sgc::ecs::Entity> entities;

	for (int i = 0; i < 10000; ++i)
	{
		auto e = world.createEntity();
		world.addComponent(e, Position{static_cast<float>(i), 0.0f});
		entities.push_back(e);
	}

	REQUIRE(world.entityCount() == 10000);

	int count = 0;
	world.forEach<Position>([&count](Position&)
	{
		++count;
	});

	REQUIRE(count == 10000);
}

TEST_CASE("World destroyEntity cleans up components", "[ecs][world]")
{
	sgc::ecs::World world;
	auto e = world.createEntity();
	world.addComponent(e, Position{1.0f, 2.0f});
	world.addComponent(e, Velocity{3.0f, 4.0f});

	world.destroyEntity(e);

	// getComponent on dead entity returns nullptr
	REQUIRE(world.getComponent<Position>(e) == nullptr);
	REQUIRE(world.getComponent<Velocity>(e) == nullptr);
}

// ── forEachEntity ───────────────────────────────────────────────

TEST_CASE("World forEachEntity single component", "[ecs][world]")
{
	sgc::ecs::World world;
	auto e1 = world.createEntity();
	auto e2 = world.createEntity();
	world.addComponent(e1, Position{1.0f, 0.0f});
	world.addComponent(e2, Position{2.0f, 0.0f});

	std::vector<sgc::ecs::Entity> collected;
	world.forEachEntity<Position>([&](sgc::ecs::Entity entity, Position&)
	{
		collected.push_back(entity);
	});

	REQUIRE(collected.size() == 2);
	REQUIRE(world.isAlive(collected[0]));
	REQUIRE(world.isAlive(collected[1]));
}

TEST_CASE("World forEachEntity multiple components", "[ecs][world]")
{
	sgc::ecs::World world;
	auto e1 = world.createEntity();
	auto e2 = world.createEntity();
	auto e3 = world.createEntity();

	world.addComponent(e1, Position{1.0f, 0.0f});
	world.addComponent(e1, Velocity{10.0f, 0.0f});

	world.addComponent(e2, Position{2.0f, 0.0f});
	world.addComponent(e2, Velocity{20.0f, 0.0f});

	world.addComponent(e3, Position{3.0f, 0.0f});
	// e3 has no Velocity

	int count = 0;
	world.forEachEntity<Position, Velocity>(
		[&](sgc::ecs::Entity, Position&, Velocity&) { ++count; });

	REQUIRE(count == 2);
}

TEST_CASE("World forEachEntity skips missing components", "[ecs][world]")
{
	sgc::ecs::World world;
	auto e = world.createEntity();
	world.addComponent(e, Position{1.0f, 0.0f});

	int count = 0;
	world.forEachEntity<Position, Velocity>(
		[&](sgc::ecs::Entity, Position&, Velocity&) { ++count; });

	REQUIRE(count == 0);
}

TEST_CASE("World forEachEntity provides correct entity for getComponent", "[ecs][world]")
{
	sgc::ecs::World world;
	auto e1 = world.createEntity();
	auto e2 = world.createEntity();
	world.addComponent(e1, Position{1.0f, 0.0f});
	world.addComponent(e1, Velocity{10.0f, 0.0f});
	world.addComponent(e1, Health{50});
	world.addComponent(e2, Position{2.0f, 0.0f});
	world.addComponent(e2, Velocity{20.0f, 0.0f});

	// forEachEntity でEntityを使ってHealthを取得
	int healthSum = 0;
	world.forEachEntity<Position, Velocity>(
		[&](sgc::ecs::Entity entity, Position&, Velocity&)
		{
			auto* h = world.getComponent<Health>(entity);
			if (h) healthSum += h->hp;
		});

	REQUIRE(healthSum == 50);
}

// ── View ────────────────────────────────────────────────────────

TEST_CASE("View basic usage with single component", "[ecs][view]")
{
	sgc::ecs::World world;
	auto e1 = world.createEntity();
	auto e2 = world.createEntity();
	world.addComponent(e1, Position{1.0f, 0.0f});
	world.addComponent(e2, Position{2.0f, 0.0f});

	auto view = world.view<Position>();
	float sum = 0.0f;
	view.each([&sum](Position& p) { sum += p.x; });

	REQUIRE(sum == 3.0f);
}

TEST_CASE("View with multiple components", "[ecs][view]")
{
	sgc::ecs::World world;
	auto e1 = world.createEntity();
	auto e2 = world.createEntity();
	auto e3 = world.createEntity();

	world.addComponent(e1, Position{0.0f, 0.0f});
	world.addComponent(e1, Velocity{1.0f, 2.0f});

	world.addComponent(e2, Position{10.0f, 10.0f});
	world.addComponent(e2, Velocity{-1.0f, -1.0f});

	world.addComponent(e3, Position{99.0f, 99.0f});
	// e3にはVelocityなし

	auto view = world.view<Position, Velocity>();
	view.each([](Position& pos, Velocity& vel) {
		pos.x += vel.dx;
		pos.y += vel.dy;
	});

	REQUIRE(world.getComponent<Position>(e1)->x == 1.0f);
	REQUIRE(world.getComponent<Position>(e2)->x == 9.0f);
	REQUIRE(world.getComponent<Position>(e3)->x == 99.0f);
}

TEST_CASE("View matches forEach results", "[ecs][view]")
{
	sgc::ecs::World world;
	for (int i = 0; i < 100; ++i)
	{
		auto e = world.createEntity();
		world.addComponent(e, Position{static_cast<float>(i), 0.0f});
		if (i % 2 == 0)
		{
			world.addComponent(e, Velocity{1.0f, 0.0f});
		}
	}

	// forEachでカウント
	int forEachCount = 0;
	world.forEach<Position, Velocity>([&forEachCount](Position&, Velocity&) {
		++forEachCount;
	});

	// Viewでカウント
	auto view = world.view<Position, Velocity>();
	int viewCount = 0;
	view.each([&viewCount](Position&, Velocity&) {
		++viewCount;
	});

	REQUIRE(forEachCount == viewCount);
	REQUIRE(forEachCount == 50);
}

TEST_CASE("View eachEntity returns correct entity", "[ecs][view]")
{
	sgc::ecs::World world;
	auto e1 = world.createEntity();
	auto e2 = world.createEntity();
	world.addComponent(e1, Position{1.0f, 0.0f});
	world.addComponent(e1, Velocity{10.0f, 0.0f});
	world.addComponent(e2, Position{2.0f, 0.0f});
	world.addComponent(e2, Velocity{20.0f, 0.0f});

	auto view = world.view<Position, Velocity>();
	std::vector<sgc::ecs::Entity> entities;
	view.eachEntity([&](sgc::ecs::Entity entity, Position&, Velocity&)
	{
		entities.push_back(entity);
	});

	REQUIRE(entities.size() == 2);
	REQUIRE(world.isAlive(entities[0]));
	REQUIRE(world.isAlive(entities[1]));
}

TEST_CASE("View eachEntity matches forEachEntity results", "[ecs][view]")
{
	sgc::ecs::World world;
	for (int i = 0; i < 50; ++i)
	{
		auto e = world.createEntity();
		world.addComponent(e, Position{static_cast<float>(i), 0.0f});
		if (i % 3 == 0) world.addComponent(e, Velocity{1.0f, 0.0f});
	}

	std::vector<sgc::ecs::Entity> forEachResult;
	world.forEachEntity<Position, Velocity>(
		[&](sgc::ecs::Entity entity, Position&, Velocity&)
		{
			forEachResult.push_back(entity);
		});

	auto view = world.view<Position, Velocity>();
	std::vector<sgc::ecs::Entity> viewResult;
	view.eachEntity([&](sgc::ecs::Entity entity, Position&, Velocity&)
	{
		viewResult.push_back(entity);
	});

	REQUIRE(forEachResult.size() == viewResult.size());
	for (std::size_t i = 0; i < forEachResult.size(); ++i)
	{
		REQUIRE(forEachResult[i] == viewResult[i]);
	}
}

TEST_CASE("View isValid returns false when storage missing", "[ecs][view]")
{
	sgc::ecs::World world;
	auto view = world.view<Position, Health>();
	REQUIRE_FALSE(view.isValid());

	// each should be a no-op
	int count = 0;
	view.each([&count](Position&, Health&) { ++count; });
	REQUIRE(count == 0);
}

// ── Tags ────────────────────────────────────────────────────────

TEST_CASE("World setTag and getTag", "[ecs][tag]")
{
	sgc::ecs::World world;
	auto e = world.createEntity();
	REQUIRE(world.getTag(e) == 0);

	world.setTag(e, 42);
	REQUIRE(world.getTag(e) == 42);
}

TEST_CASE("World findByTag returns matching entities", "[ecs][tag]")
{
	sgc::ecs::World world;
	auto e1 = world.createEntity();
	auto e2 = world.createEntity();
	auto e3 = world.createEntity();

	world.setTag(e1, 100);
	world.setTag(e2, 200);
	world.setTag(e3, 100);

	auto found = world.findByTag(100);
	REQUIRE(found.size() == 2);

	auto found200 = world.findByTag(200);
	REQUIRE(found200.size() == 1);
	REQUIRE(found200[0] == e2);
}

TEST_CASE("World destroyEntity removes tag", "[ecs][tag]")
{
	sgc::ecs::World world;
	auto e = world.createEntity();
	world.setTag(e, 42);
	world.destroyEntity(e);

	auto found = world.findByTag(42);
	REQUIRE(found.empty());
}

TEST_CASE("World getTag returns 0 for dead entity", "[ecs][tag]")
{
	sgc::ecs::World world;
	auto e = world.createEntity();
	world.setTag(e, 42);
	world.destroyEntity(e);
	REQUIRE(world.getTag(e) == 0);
}

// ── EventDispatcher連携 ─────────────────────────────────────────

TEST_CASE("World fires ComponentAdded event", "[ecs][event]")
{
	sgc::EventDispatcher dispatcher;
	sgc::ecs::World world;
	world.setEventDispatcher(&dispatcher);

	int addedCount = 0;
	dispatcher.on<sgc::ecs::ComponentAdded<Position>>(
		[&](const sgc::ecs::ComponentAdded<Position>&) { ++addedCount; });

	auto e = world.createEntity();
	world.addComponent(e, Position{1.0f, 2.0f});

	REQUIRE(addedCount == 1);
}

TEST_CASE("World fires ComponentRemoved event", "[ecs][event]")
{
	sgc::EventDispatcher dispatcher;
	sgc::ecs::World world;
	world.setEventDispatcher(&dispatcher);

	int removedCount = 0;
	dispatcher.on<sgc::ecs::ComponentRemoved<Position>>(
		[&](const sgc::ecs::ComponentRemoved<Position>&) { ++removedCount; });

	auto e = world.createEntity();
	world.addComponent(e, Position{1.0f, 2.0f});
	world.removeComponent<Position>(e);

	REQUIRE(removedCount == 1);
}

TEST_CASE("World no event without dispatcher", "[ecs][event]")
{
	sgc::ecs::World world;
	auto e = world.createEntity();
	// イベントディスパッチャーなしでもクラッシュしない
	world.addComponent(e, Position{1.0f, 2.0f});
	world.removeComponent<Position>(e);
	REQUIRE_FALSE(world.hasComponent<Position>(e));
}
