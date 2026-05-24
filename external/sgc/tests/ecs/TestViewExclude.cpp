#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/ecs/World.hpp"

namespace
{

struct Position { float x{0}; float y{0}; };
struct Velocity { float dx{0}; float dy{0}; };
struct Dead {};
struct Frozen {};

} // anonymous namespace

TEST_CASE("ExcludeView filters out excluded components", "[ecs][view][exclude]")
{
	sgc::ecs::World world;

	auto e1 = world.createEntity();
	world.addComponent(e1, Position{1.0f, 0.0f});
	world.addComponent(e1, Velocity{1.0f, 0.0f});

	auto e2 = world.createEntity();
	world.addComponent(e2, Position{2.0f, 0.0f});
	world.addComponent(e2, Velocity{1.0f, 0.0f});
	world.addComponent(e2, Dead{});

	auto e3 = world.createEntity();
	world.addComponent(e3, Position{3.0f, 0.0f});
	world.addComponent(e3, Velocity{1.0f, 0.0f});

	int count = 0;
	float sumX = 0.0f;
	auto view = world.viewExclude<Position, Velocity>(sgc::ecs::Exclude<Dead>{});
	view.each([&](Position& pos, Velocity&) {
		sumX += pos.x;
		++count;
	});

	REQUIRE(count == 2);
	REQUIRE(sumX == Catch::Approx(4.0f)); // e1(1) + e3(3)
}

TEST_CASE("ExcludeView with no excluded entities", "[ecs][view][exclude]")
{
	sgc::ecs::World world;

	auto e1 = world.createEntity();
	world.addComponent(e1, Position{1.0f, 0.0f});

	auto e2 = world.createEntity();
	world.addComponent(e2, Position{2.0f, 0.0f});

	int count = 0;
	auto view = world.viewExclude<Position>(sgc::ecs::Exclude<Dead>{});
	view.each([&](Position&) { ++count; });

	REQUIRE(count == 2);
}

TEST_CASE("ExcludeView with all entities excluded", "[ecs][view][exclude]")
{
	sgc::ecs::World world;

	auto e1 = world.createEntity();
	world.addComponent(e1, Position{1.0f, 0.0f});
	world.addComponent(e1, Dead{});

	auto e2 = world.createEntity();
	world.addComponent(e2, Position{2.0f, 0.0f});
	world.addComponent(e2, Dead{});

	int count = 0;
	auto view = world.viewExclude<Position>(sgc::ecs::Exclude<Dead>{});
	view.each([&](Position&) { ++count; });

	REQUIRE(count == 0);
}

TEST_CASE("ExcludeView with multiple exclude types", "[ecs][view][exclude]")
{
	sgc::ecs::World world;

	auto e1 = world.createEntity();
	world.addComponent(e1, Position{1.0f, 0.0f});
	world.addComponent(e1, Velocity{1.0f, 0.0f});

	auto e2 = world.createEntity();
	world.addComponent(e2, Position{2.0f, 0.0f});
	world.addComponent(e2, Velocity{1.0f, 0.0f});
	world.addComponent(e2, Dead{});

	auto e3 = world.createEntity();
	world.addComponent(e3, Position{3.0f, 0.0f});
	world.addComponent(e3, Velocity{1.0f, 0.0f});
	world.addComponent(e3, Frozen{});

	auto e4 = world.createEntity();
	world.addComponent(e4, Position{4.0f, 0.0f});
	world.addComponent(e4, Velocity{1.0f, 0.0f});

	int count = 0;
	auto view = world.viewExclude<Position, Velocity>(
		sgc::ecs::Exclude<Dead, Frozen>{});
	view.each([&](Position&, Velocity&) { ++count; });

	REQUIRE(count == 2); // e1 and e4
}

TEST_CASE("ExcludeView isValid returns true with storages", "[ecs][view][exclude]")
{
	sgc::ecs::World world;
	auto e = world.createEntity();
	world.addComponent(e, Position{0.0f, 0.0f});

	auto view = world.viewExclude<Position>(sgc::ecs::Exclude<Dead>{});
	REQUIRE(view.isValid());
}

TEST_CASE("ExcludeView isValid returns false without storages", "[ecs][view][exclude]")
{
	sgc::ecs::World world;
	// Position ストレージが存在しない
	auto view = world.viewExclude<Position>(sgc::ecs::Exclude<Dead>{});
	REQUIRE_FALSE(view.isValid());
}

TEST_CASE("ExcludeView eachEntity filters excluded and returns entity", "[ecs][view][exclude]")
{
	sgc::ecs::World world;

	auto e1 = world.createEntity();
	world.addComponent(e1, Position{1.0f, 0.0f});
	world.addComponent(e1, Velocity{1.0f, 0.0f});

	auto e2 = world.createEntity();
	world.addComponent(e2, Position{2.0f, 0.0f});
	world.addComponent(e2, Velocity{1.0f, 0.0f});
	world.addComponent(e2, Dead{});

	auto e3 = world.createEntity();
	world.addComponent(e3, Position{3.0f, 0.0f});
	world.addComponent(e3, Velocity{1.0f, 0.0f});

	std::vector<sgc::ecs::Entity> collected;
	auto view = world.viewExclude<Position, Velocity>(sgc::ecs::Exclude<Dead>{});
	view.eachEntity([&](sgc::ecs::Entity entity, Position&, Velocity&)
	{
		collected.push_back(entity);
	});

	REQUIRE(collected.size() == 2);
	// e2 (Dead) should be excluded
	for (const auto& ent : collected)
	{
		REQUIRE(world.isAlive(ent));
		REQUIRE_FALSE(world.hasComponent<Dead>(ent));
	}
}

TEST_CASE("Regular View still works after exclude modification", "[ecs][view]")
{
	sgc::ecs::World world;

	auto e1 = world.createEntity();
	world.addComponent(e1, Position{1.0f, 0.0f});

	auto e2 = world.createEntity();
	world.addComponent(e2, Position{2.0f, 0.0f});
	world.addComponent(e2, Dead{});

	// 通常のViewは Dead を無視しない
	int count = 0;
	auto view = world.view<Position>();
	view.each([&](Position&) { ++count; });

	REQUIRE(count == 2);
}
