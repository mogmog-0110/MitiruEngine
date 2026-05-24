#include <catch2/catch_test_macros.hpp>

#include "sgc/ecs/EntityTemplate.hpp"

using namespace sgc::ecs;

namespace
{

struct Position
{
	float x = 0;
	float y = 0;
};

struct Velocity
{
	float dx = 0;
	float dy = 0;
};

struct Health
{
	int hp = 100;
};

} // namespace

TEST_CASE("EntityTemplate componentCount", "[ecs][template]")
{
	EntityTemplate tmpl;
	CHECK(tmpl.componentCount() == 0);

	tmpl.add(Position{1, 2});
	CHECK(tmpl.componentCount() == 1);

	tmpl.add<Velocity>();
	CHECK(tmpl.componentCount() == 2);
}

TEST_CASE("EntityTemplate instantiate creates entity with components", "[ecs][template]")
{
	World world;
	EntityTemplate tmpl;
	tmpl.add(Position{10, 20})
	    .add(Velocity{1, -1});

	auto entity = tmpl.instantiate(world);
	CHECK(world.isAlive(entity));
	CHECK(world.hasComponent<Position>(entity));
	CHECK(world.hasComponent<Velocity>(entity));

	const auto* pos = world.getComponent<Position>(entity);
	REQUIRE(pos != nullptr);
	CHECK(pos->x == 10);
	CHECK(pos->y == 20);
}

TEST_CASE("EntityTemplate instantiate multiple entities", "[ecs][template]")
{
	World world;
	EntityTemplate tmpl;
	tmpl.add(Health{50});

	auto e1 = tmpl.instantiate(world);
	auto e2 = tmpl.instantiate(world);

	CHECK(e1.id != e2.id);
	CHECK(world.hasComponent<Health>(e1));
	CHECK(world.hasComponent<Health>(e2));
}

TEST_CASE("EntityTemplate default component", "[ecs][template]")
{
	World world;
	EntityTemplate tmpl;
	tmpl.add<Health>();

	auto entity = tmpl.instantiate(world);
	const auto* hp = world.getComponent<Health>(entity);
	REQUIRE(hp != nullptr);
	CHECK(hp->hp == 100);
}
