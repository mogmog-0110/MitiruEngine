#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/ecs/System.hpp"
#include "sgc/ecs/World.hpp"

namespace
{

struct Position { float x{0}; float y{0}; };
struct Velocity { float dx{0}; float dy{0}; };

struct MovementSystem
{
	void update(sgc::ecs::World& world, float dt)
	{
		world.forEach<Position, Velocity>([dt](Position& pos, Velocity& vel) {
			pos.x += vel.dx * dt;
			pos.y += vel.dy * dt;
		});
	}
};

struct CounterSystem
{
	int callCount{0};
	void update(sgc::ecs::World&, float)
	{
		++callCount;
	}
};

} // anonymous namespace

TEST_CASE("SystemScheduler addSystem and update", "[ecs][system]")
{
	sgc::ecs::World world;
	sgc::ecs::SystemScheduler scheduler;

	auto e = world.createEntity();
	world.addComponent(e, Position{0.0f, 0.0f});
	world.addComponent(e, Velocity{10.0f, 5.0f});

	scheduler.addSystem(MovementSystem{});
	scheduler.update(world, 1.0f);

	auto* pos = world.getComponent<Position>(e);
	REQUIRE(pos != nullptr);
	REQUIRE(pos->x == 10.0f);
	REQUIRE(pos->y == 5.0f);
}

TEST_CASE("SystemScheduler runs systems in order", "[ecs][system]")
{
	sgc::ecs::World world;
	sgc::ecs::SystemScheduler scheduler;

	std::vector<int> order;
	struct SysA
	{
		std::vector<int>* out;
		void update(sgc::ecs::World&, float) { out->push_back(1); }
	};
	struct SysB
	{
		std::vector<int>* out;
		void update(sgc::ecs::World&, float) { out->push_back(2); }
	};

	scheduler.addSystem(SysA{&order});
	scheduler.addSystem(SysB{&order});
	scheduler.update(world, 0.016f);

	REQUIRE(order.size() == 2);
	REQUIRE(order[0] == 1);
	REQUIRE(order[1] == 2);
}

TEST_CASE("SystemScheduler systemCount", "[ecs][system]")
{
	sgc::ecs::SystemScheduler scheduler;
	REQUIRE(scheduler.systemCount() == 0);

	scheduler.addSystem(MovementSystem{});
	REQUIRE(scheduler.systemCount() == 1);

	scheduler.addSystem(CounterSystem{});
	REQUIRE(scheduler.systemCount() == 2);
}

TEST_CASE("SystemScheduler removeSystem", "[ecs][system]")
{
	sgc::ecs::SystemScheduler scheduler;
	scheduler.addSystem(MovementSystem{});
	scheduler.addSystem(CounterSystem{});

	scheduler.removeSystem(0);
	REQUIRE(scheduler.systemCount() == 1);
}

TEST_CASE("SystemScheduler clear", "[ecs][system]")
{
	sgc::ecs::SystemScheduler scheduler;
	scheduler.addSystem(MovementSystem{});
	scheduler.addSystem(CounterSystem{});

	scheduler.clear();
	REQUIRE(scheduler.systemCount() == 0);
}

TEST_CASE("SystemScheduler update with delta time", "[ecs][system]")
{
	sgc::ecs::World world;
	sgc::ecs::SystemScheduler scheduler;

	auto e = world.createEntity();
	world.addComponent(e, Position{100.0f, 200.0f});
	world.addComponent(e, Velocity{-5.0f, 3.0f});

	scheduler.addSystem(MovementSystem{});

	// 0.5秒更新
	scheduler.update(world, 0.5f);
	auto* pos = world.getComponent<Position>(e);
	REQUIRE(pos->x == Catch::Approx(97.5f));
	REQUIRE(pos->y == Catch::Approx(201.5f));
}

// ── Phase & Priority ─────────────────────────────────────────

TEST_CASE("SystemScheduler priority sorts within same phase", "[ecs][system]")
{
	sgc::ecs::World world;
	sgc::ecs::SystemScheduler scheduler;

	std::vector<int> order;
	struct SysA
	{
		std::vector<int>* out;
		void update(sgc::ecs::World&, float) { out->push_back(1); }
	};
	struct SysB
	{
		std::vector<int>* out;
		void update(sgc::ecs::World&, float) { out->push_back(2); }
	};

	// SysB with lower priority (0) added first, SysA with higher priority (10) added second
	scheduler.addSystem(SysB{&order}, sgc::ecs::Phase::Update, 10);
	scheduler.addSystem(SysA{&order}, sgc::ecs::Phase::Update, 0);

	scheduler.update(world, 0.016f);
	REQUIRE(order.size() == 2);
	REQUIRE(order[0] == 1);  // priority 0 first
	REQUIRE(order[1] == 2);  // priority 10 second
}

TEST_CASE("SystemScheduler phase ordering", "[ecs][system]")
{
	sgc::ecs::World world;
	sgc::ecs::SystemScheduler scheduler;

	std::vector<std::string> order;
	struct RenderSys
	{
		std::vector<std::string>* out;
		void update(sgc::ecs::World&, float) { out->push_back("render"); }
	};
	struct UpdateSys
	{
		std::vector<std::string>* out;
		void update(sgc::ecs::World&, float) { out->push_back("update"); }
	};
	struct LateSys
	{
		std::vector<std::string>* out;
		void update(sgc::ecs::World&, float) { out->push_back("late"); }
	};

	// 逆順に追加
	scheduler.addSystem(RenderSys{&order}, sgc::ecs::Phase::Render);
	scheduler.addSystem(LateSys{&order}, sgc::ecs::Phase::LateUpdate);
	scheduler.addSystem(UpdateSys{&order}, sgc::ecs::Phase::Update);

	scheduler.update(world, 0.016f);
	REQUIRE(order.size() == 3);
	REQUIRE(order[0] == "update");
	REQUIRE(order[1] == "late");
	REQUIRE(order[2] == "render");
}

TEST_CASE("SystemScheduler updatePhase runs only target phase", "[ecs][system]")
{
	sgc::ecs::World world;
	sgc::ecs::SystemScheduler scheduler;

	int updateCount = 0;
	int renderCount = 0;
	struct UpdateSys
	{
		int* count;
		void update(sgc::ecs::World&, float) { ++(*count); }
	};
	struct RenderSys
	{
		int* count;
		void update(sgc::ecs::World&, float) { ++(*count); }
	};

	scheduler.addSystem(UpdateSys{&updateCount}, sgc::ecs::Phase::Update);
	scheduler.addSystem(RenderSys{&renderCount}, sgc::ecs::Phase::Render);

	scheduler.updatePhase(sgc::ecs::Phase::Update, world, 0.016f);
	REQUIRE(updateCount == 1);
	REQUIRE(renderCount == 0);
}

TEST_CASE("SystemScheduler default phase is Update", "[ecs][system]")
{
	sgc::ecs::World world;
	sgc::ecs::SystemScheduler scheduler;

	int count = 0;
	struct SimpleSys
	{
		int* count;
		void update(sgc::ecs::World&, float) { ++(*count); }
	};

	// フェーズ未指定 → Updateフェーズ
	scheduler.addSystem(SimpleSys{&count});

	scheduler.updatePhase(sgc::ecs::Phase::Update, world, 0.016f);
	REQUIRE(count == 1);

	scheduler.updatePhase(sgc::ecs::Phase::Render, world, 0.016f);
	REQUIRE(count == 1);  // Renderフェーズでは実行されない
}
