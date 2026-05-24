/// @file TestSystemTraits.cpp
/// @brief ECS SystemTraits tests

#include <catch2/catch_test_macros.hpp>

#include "sgc/ecs/SystemTraits.hpp"
#include "sgc/ecs/World.hpp"
#include "sgc/ecs/System.hpp"
#include "sgc/types/TypeList.hpp"

using namespace sgc;
using namespace sgc::ecs;

// ── テスト用コンポーネント ──────────────────────────────

struct Position
{
	float x = 0.0f;
	float y = 0.0f;
};

struct Velocity
{
	float dx = 0.0f;
	float dy = 0.0f;
};

struct Health
{
	int hp = 100;
};

// ── テスト用システム ────────────────────────────────────

/// requires_components を持つシステム
struct MovementSystem
{
	using requires_components = TypeList<Position, Velocity>;

	void update(World& world, float dt)
	{
		world.forEach<Position, Velocity>(
			[dt](Position& pos, Velocity& vel)
			{
				pos.x += vel.dx * dt;
				pos.y += vel.dy * dt;
			});
	}
};

/// requires_components を持たないシステム
struct SimpleSystem
{
	void update(World& /*world*/, float /*dt*/)
	{
	}
};

/// 空の requires_components を持つシステム
struct EmptyRequirementsSystem
{
	using requires_components = TypeList<>;

	void update(World& /*world*/, float /*dt*/)
	{
	}
};

/// 3つ以上のコンポーネントを要求するシステム
struct ComplexSystem
{
	using requires_components = TypeList<Position, Velocity, Health>;

	void update(World& /*world*/, float /*dt*/)
	{
	}
};

// ── HasRequiredComponents コンセプト ─────────────────────

TEST_CASE("HasRequiredComponents detects annotated systems", "[ecs][SystemTraits]")
{
	STATIC_CHECK(HasRequiredComponents<MovementSystem>);
	STATIC_CHECK(HasRequiredComponents<EmptyRequirementsSystem>);
	STATIC_CHECK(HasRequiredComponents<ComplexSystem>);
}

TEST_CASE("HasRequiredComponents rejects unannotated systems", "[ecs][SystemTraits]")
{
	STATIC_CHECK_FALSE(HasRequiredComponents<SimpleSystem>);
	STATIC_CHECK_FALSE(HasRequiredComponents<int>);
}

// ── RequiredComponentsOfT ───────────────────────────────

TEST_CASE("RequiredComponentsOfT returns TypeList for annotated system", "[ecs][SystemTraits]")
{
	using Reqs = RequiredComponentsOfT<MovementSystem>;
	STATIC_CHECK(std::is_same_v<Reqs, TypeList<Position, Velocity>>);
}

TEST_CASE("RequiredComponentsOfT returns empty TypeList for unannotated system", "[ecs][SystemTraits]")
{
	using Reqs = RequiredComponentsOfT<SimpleSystem>;
	STATIC_CHECK(std::is_same_v<Reqs, TypeList<>>);
}

TEST_CASE("RequiredComponentsOfT returns empty TypeList for EmptyRequirementsSystem", "[ecs][SystemTraits]")
{
	using Reqs = RequiredComponentsOfT<EmptyRequirementsSystem>;
	STATIC_CHECK(std::is_same_v<Reqs, TypeList<>>);
}

TEST_CASE("RequiredComponentsOfT TypeList size matches component count", "[ecs][SystemTraits]")
{
	using Reqs = RequiredComponentsOfT<MovementSystem>;
	STATIC_CHECK(TYPE_LIST_SIZE<Reqs> == 2);

	using ComplexReqs = RequiredComponentsOfT<ComplexSystem>;
	STATIC_CHECK(TYPE_LIST_SIZE<ComplexReqs> == 3);
}

TEST_CASE("RequiredComponentsOfT TypeList Contains check", "[ecs][SystemTraits]")
{
	using Reqs = RequiredComponentsOfT<MovementSystem>;
	STATIC_CHECK(TYPE_LIST_CONTAINS<Position, Reqs>);
	STATIC_CHECK(TYPE_LIST_CONTAINS<Velocity, Reqs>);
	STATIC_CHECK_FALSE(TYPE_LIST_CONTAINS<Health, Reqs>);
}

// ── validateSystemRequirements ──────────────────────────

TEST_CASE("validateSystemRequirements returns true when all storages exist", "[ecs][SystemTraits]")
{
	World world;
	auto e = world.createEntity();
	world.addComponent(e, Position{1.0f, 2.0f});
	world.addComponent(e, Velocity{3.0f, 4.0f});

	CHECK(validateSystemRequirements<MovementSystem>(world));
}

TEST_CASE("validateSystemRequirements returns false when storage missing", "[ecs][SystemTraits]")
{
	World world;
	auto e = world.createEntity();
	world.addComponent(e, Position{1.0f, 2.0f});
	// Velocity storage not created

	CHECK_FALSE(validateSystemRequirements<MovementSystem>(world));
}

TEST_CASE("validateSystemRequirements returns true for unannotated system", "[ecs][SystemTraits]")
{
	World world;
	// No storages created
	CHECK(validateSystemRequirements<SimpleSystem>(world));
}

TEST_CASE("validateSystemRequirements returns true for empty requirements", "[ecs][SystemTraits]")
{
	World world;
	CHECK(validateSystemRequirements<EmptyRequirementsSystem>(world));
}

// ── World::hasStorage ──────────────────────────────────

TEST_CASE("World hasStorage returns false for unregistered type", "[ecs][SystemTraits]")
{
	World world;
	CHECK_FALSE(world.hasStorage<Position>());
}

TEST_CASE("World hasStorage returns true after adding component", "[ecs][SystemTraits]")
{
	World world;
	auto e = world.createEntity();
	world.addComponent(e, Position{1.0f, 2.0f});
	CHECK(world.hasStorage<Position>());
}

// ── addSystem with annotated system ────────────────────

TEST_CASE("SystemScheduler accepts annotated system", "[ecs][SystemTraits]")
{
	SystemScheduler scheduler;
	scheduler.addSystem(MovementSystem{});
	CHECK(scheduler.systemCount() == 1);
}

TEST_CASE("SystemScheduler accepts unannotated system", "[ecs][SystemTraits]")
{
	SystemScheduler scheduler;
	scheduler.addSystem(SimpleSystem{});
	CHECK(scheduler.systemCount() == 1);
}

TEST_CASE("validateSystemRequirements with 3+ components", "[ecs][SystemTraits]")
{
	World world;
	auto e = world.createEntity();
	world.addComponent(e, Position{});
	world.addComponent(e, Velocity{});
	// Health not added

	CHECK_FALSE(validateSystemRequirements<ComplexSystem>(world));

	world.addComponent(e, Health{100});
	CHECK(validateSystemRequirements<ComplexSystem>(world));
}
