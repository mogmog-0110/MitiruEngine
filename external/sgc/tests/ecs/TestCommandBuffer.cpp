#include <catch2/catch_test_macros.hpp>

#include "sgc/ecs/CommandBuffer.hpp"
#include "sgc/ecs/World.hpp"

namespace
{

struct Position { float x{0}; float y{0}; };
struct Health { int hp{100}; };

} // anonymous namespace

TEST_CASE("CommandBuffer createEntity", "[ecs][commandbuffer]")
{
	sgc::ecs::World world;
	sgc::ecs::CommandBuffer cmd;

	cmd.createEntity([](sgc::ecs::World& w, sgc::ecs::Entity e) {
		w.addComponent(e, Position{10.0f, 20.0f});
	});

	REQUIRE(world.entityCount() == 0);
	cmd.execute(world);
	REQUIRE(world.entityCount() == 1);
}

TEST_CASE("CommandBuffer destroyEntity", "[ecs][commandbuffer]")
{
	sgc::ecs::World world;
	auto e = world.createEntity();
	world.addComponent(e, Position{0.0f, 0.0f});

	sgc::ecs::CommandBuffer cmd;
	cmd.destroyEntity(e);
	REQUIRE(world.entityCount() == 1);

	cmd.execute(world);
	REQUIRE(world.entityCount() == 0);
}

TEST_CASE("CommandBuffer addComponent", "[ecs][commandbuffer]")
{
	sgc::ecs::World world;
	auto e = world.createEntity();

	sgc::ecs::CommandBuffer cmd;
	cmd.addComponent(e, Health{50});
	REQUIRE_FALSE(world.hasComponent<Health>(e));

	cmd.execute(world);
	REQUIRE(world.hasComponent<Health>(e));
	REQUIRE(world.getComponent<Health>(e)->hp == 50);
}

TEST_CASE("CommandBuffer removeComponent", "[ecs][commandbuffer]")
{
	sgc::ecs::World world;
	auto e = world.createEntity();
	world.addComponent(e, Position{1.0f, 2.0f});

	sgc::ecs::CommandBuffer cmd;
	cmd.removeComponent<Position>(e);
	REQUIRE(world.hasComponent<Position>(e));

	cmd.execute(world);
	REQUIRE_FALSE(world.hasComponent<Position>(e));
}

TEST_CASE("CommandBuffer multiple commands", "[ecs][commandbuffer]")
{
	sgc::ecs::World world;
	sgc::ecs::CommandBuffer cmd;

	cmd.createEntity([](sgc::ecs::World& w, sgc::ecs::Entity e) {
		w.addComponent(e, Position{1.0f, 1.0f});
		w.addComponent(e, Health{100});
	});
	cmd.createEntity([](sgc::ecs::World& w, sgc::ecs::Entity e) {
		w.addComponent(e, Position{2.0f, 2.0f});
	});

	REQUIRE(cmd.commandCount() == 2);
	cmd.execute(world);
	REQUIRE(world.entityCount() == 2);
	REQUIRE(cmd.commandCount() == 0); // cleared after execute
}

TEST_CASE("CommandBuffer clear discards commands", "[ecs][commandbuffer]")
{
	sgc::ecs::World world;
	sgc::ecs::CommandBuffer cmd;

	cmd.createEntity([](sgc::ecs::World&, sgc::ecs::Entity) {});
	REQUIRE(cmd.commandCount() == 1);

	cmd.clear();
	REQUIRE(cmd.commandCount() == 0);

	cmd.execute(world);
	REQUIRE(world.entityCount() == 0);
}

TEST_CASE("CommandBuffer mixed create and destroy", "[ecs][commandbuffer]")
{
	sgc::ecs::World world;
	auto e1 = world.createEntity();
	world.addComponent(e1, Position{0.0f, 0.0f});

	sgc::ecs::CommandBuffer cmd;
	cmd.destroyEntity(e1);
	cmd.createEntity([](sgc::ecs::World& w, sgc::ecs::Entity e) {
		w.addComponent(e, Position{99.0f, 99.0f});
	});

	cmd.execute(world);
	REQUIRE(world.entityCount() == 1);
}
