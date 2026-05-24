#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include "sgc/ecs/World.hpp"

namespace
{

struct Position { float x{0}; float y{0}; };
struct Velocity { float dx{1}; float dy{1}; };

} // anonymous namespace

TEST_CASE("Benchmark ECS forEach", "[benchmark][ecs]")
{
	sgc::ecs::World world;

	// 1000エンティティを作成
	for (int i = 0; i < 1000; ++i)
	{
		auto e = world.createEntity();
		world.addComponent(e, Position{static_cast<float>(i), 0.0f});
		world.addComponent(e, Velocity{1.0f, 1.0f});
	}

	BENCHMARK("forEach 1000 entities with 2 components")
	{
		world.forEach<Position, Velocity>([](Position& pos, Velocity& vel) {
			pos.x += vel.dx;
			pos.y += vel.dy;
		});
	};
}

TEST_CASE("Benchmark ECS View iteration", "[benchmark][ecs]")
{
	sgc::ecs::World world;

	for (int i = 0; i < 1000; ++i)
	{
		auto e = world.createEntity();
		world.addComponent(e, Position{static_cast<float>(i), 0.0f});
		world.addComponent(e, Velocity{1.0f, 1.0f});
	}

	auto view = world.view<Position, Velocity>();

	BENCHMARK("View.each 1000 entities")
	{
		view.each([](Position& pos, Velocity& vel) {
			pos.x += vel.dx;
			pos.y += vel.dy;
		});
	};
}

TEST_CASE("Benchmark ECS entity creation", "[benchmark][ecs]")
{
	BENCHMARK("Create 100 entities")
	{
		sgc::ecs::World world;
		for (int i = 0; i < 100; ++i)
		{
			auto e = world.createEntity();
			world.addComponent(e, Position{0.0f, 0.0f});
		}
		return world.entityCount();
	};
}
