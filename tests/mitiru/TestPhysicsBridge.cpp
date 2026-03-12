/// @file TestPhysicsBridge.cpp
/// @brief mitiru::physics モジュールのテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "mitiru/physics/PhysicsBridge.hpp"
#include "mitiru/physics/CollisionBridge.hpp"
#include "mitiru/physics/PhysicsSystem.hpp"
#include "mitiru/scene/GameWorld.hpp"
#include "mitiru/scene/SystemRunner.hpp"

using namespace mitiru::physics;
using namespace mitiru::scene;
using Catch::Approx;

// ── PhysicsConfig ─────────────────────────────────────────

TEST_CASE("PhysicsConfig has sensible defaults", "[physics][config]")
{
	PhysicsConfig config;

	REQUIRE(config.gravity.x == Approx(0.0f));
	REQUIRE(config.gravity.y == Approx(-9.81f));
	REQUIRE(config.gravity.z == Approx(0.0f));
	REQUIRE(config.fixedTimeStep == Approx(1.0f / 60.0f));
	REQUIRE(config.maxSubSteps == 8);
	REQUIRE(config.broadphaseType == BroadphaseType::BruteForce);
}

// ── RigidBodyComponent ────────────────────────────────────

TEST_CASE("RigidBodyComponent has sensible defaults", "[physics][rigidbody]")
{
	RigidBodyComponent rb;

	REQUIRE(rb.mass == Approx(1.0f));
	REQUIRE(rb.restitution == Approx(0.3f));
	REQUIRE(rb.friction == Approx(0.5f));
	REQUIRE(rb.linearDamping == Approx(0.01f));
	REQUIRE(rb.angularDamping == Approx(0.05f));
	REQUIRE(rb.shape.type == ColliderShapeType::Sphere);
	REQUIRE(rb.isKinematic == false);
	REQUIRE(rb.isTrigger == false);
}

// ── PhysicsBridge init / shutdown ─────────────────────────

TEST_CASE("PhysicsBridge init and shutdown", "[physics][bridge]")
{
	PhysicsBridge bridge;

	REQUIRE_FALSE(bridge.isInitialized());

	bridge.init(PhysicsConfig{});
	REQUIRE(bridge.isInitialized());
	REQUIRE(bridge.bodyCount() == 0);

	bridge.shutdown();
	REQUIRE_FALSE(bridge.isInitialized());
}

// ── stepSimulation applies gravity ────────────────────────

TEST_CASE("PhysicsBridge stepSimulation applies gravity", "[physics][bridge][gravity]")
{
	PhysicsBridge bridge;
	PhysicsConfig config;
	config.gravity = {0.0f, -10.0f, 0.0f};
	config.fixedTimeStep = 1.0f / 60.0f;
	bridge.init(config);

	GameWorld world;
	auto id = world.createEntity("FallingBox");
	world.addComponent(id, TransformComponent{{0.0f, 100.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f}});
	world.addComponent(id, RigidBodyComponent{});

	bridge.syncToPhysics(world);

	// 十分なステップを実行して重力効果を確認
	for (int i = 0; i < 60; ++i)
	{
		bridge.stepSimulation(1.0f / 60.0f);
	}

	bridge.syncFromPhysics(world);

	auto* transform = world.getComponent<TransformComponent>(id);
	REQUIRE(transform != nullptr);
	// 約1秒間の自由落下: y = 100 + 0.5 * (-10) * 1^2 = ~95 (減衰あり)
	REQUIRE(transform->position.y < 100.0f);
}

// ── syncToPhysics / syncFromPhysics roundtrip ─────────────

TEST_CASE("PhysicsBridge sync roundtrip preserves position for static body", "[physics][bridge][sync]")
{
	PhysicsBridge bridge;
	bridge.init(PhysicsConfig{});

	GameWorld world;
	auto id = world.createEntity("StaticPlatform");
	world.addComponent(id, TransformComponent{{5.0f, 10.0f, 3.0f}, {}, {1.0f, 1.0f, 1.0f}});

	RigidBodyComponent rb;
	rb.mass = 0.0f;  // 静的ボディ（質量0）
	world.addComponent(id, rb);

	bridge.syncToPhysics(world);
	bridge.stepSimulation(1.0f / 60.0f);
	bridge.syncFromPhysics(world);

	auto* transform = world.getComponent<TransformComponent>(id);
	REQUIRE(transform != nullptr);
	REQUIRE(transform->position.x == Approx(5.0f));
	REQUIRE(transform->position.y == Approx(10.0f));
	REQUIRE(transform->position.z == Approx(3.0f));
}

// ── Raycast hit ───────────────────────────────────────────

TEST_CASE("PhysicsBridge raycast hits a sphere", "[physics][bridge][raycast]")
{
	PhysicsBridge bridge;
	bridge.init(PhysicsConfig{});

	GameWorld world;
	auto id = world.createEntity("Target");
	world.addComponent(id, TransformComponent{{0.0f, 0.0f, 10.0f}, {}, {1.0f, 1.0f, 1.0f}});

	RigidBodyComponent rb;
	rb.shape.type = ColliderShapeType::Sphere;
	rb.shape.data = SphereData{1.0f};
	rb.mass = 0.0f;
	world.addComponent(id, rb);

	bridge.syncToPhysics(world);

	// Z軸正方向にレイを飛ばす
	auto hit = bridge.raycast({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 100.0f);

	REQUIRE(hit.has_value());
	REQUIRE(hit->entityId == id);
	REQUIRE(hit->distance == Approx(9.0f).margin(0.01f));
	REQUIRE(hit->normal.z == Approx(-1.0f).margin(0.01f));
}

// ── Raycast miss ──────────────────────────────────────────

TEST_CASE("PhysicsBridge raycast misses when no target", "[physics][bridge][raycast]")
{
	PhysicsBridge bridge;
	bridge.init(PhysicsConfig{});

	GameWorld world;
	auto id = world.createEntity("OffTarget");
	world.addComponent(id, TransformComponent{{100.0f, 0.0f, 10.0f}, {}, {1.0f, 1.0f, 1.0f}});

	RigidBodyComponent rb;
	rb.shape.type = ColliderShapeType::Sphere;
	rb.shape.data = SphereData{1.0f};
	rb.mass = 0.0f;
	world.addComponent(id, rb);

	bridge.syncToPhysics(world);

	// Z軸正方向にレイを飛ばすが、球はX=100にあるのでミス
	auto hit = bridge.raycast({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 100.0f);

	REQUIRE_FALSE(hit.has_value());
}

// ── CollisionBridge sphere-sphere detection ───────────────

TEST_CASE("CollisionBridge detects sphere-sphere collision", "[physics][collision][sphere]")
{
	GameWorld world;

	auto idA = world.createEntity("SphereA");
	world.addComponent(idA, TransformComponent{{0.0f, 0.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f}});
	RigidBodyComponent rbA;
	rbA.shape.type = ColliderShapeType::Sphere;
	rbA.shape.data = SphereData{1.0f};
	world.addComponent(idA, rbA);

	auto idB = world.createEntity("SphereB");
	world.addComponent(idB, TransformComponent{{1.5f, 0.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f}});
	RigidBodyComponent rbB;
	rbB.shape.type = ColliderShapeType::Sphere;
	rbB.shape.data = SphereData{1.0f};
	world.addComponent(idB, rbB);

	CollisionBridge collision;

	int callbackCount = 0;
	collision.registerCallback([&callbackCount](const CollisionPair&)
	{
		++callbackCount;
	});

	auto pairs = collision.detectCollisions(world);

	REQUIRE(pairs.size() == 1);
	REQUIRE(callbackCount == 1);
	REQUIRE(pairs[0].penetration > 0.0f);
	REQUIRE(pairs[0].penetration == Approx(0.5f).margin(0.01f));
}

TEST_CASE("CollisionBridge no collision when spheres are apart", "[physics][collision][sphere]")
{
	GameWorld world;

	auto idA = world.createEntity("SphereA");
	world.addComponent(idA, TransformComponent{{0.0f, 0.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f}});
	RigidBodyComponent rbA;
	rbA.shape.type = ColliderShapeType::Sphere;
	rbA.shape.data = SphereData{1.0f};
	world.addComponent(idA, rbA);

	auto idB = world.createEntity("SphereB");
	world.addComponent(idB, TransformComponent{{5.0f, 0.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f}});
	RigidBodyComponent rbB;
	rbB.shape.type = ColliderShapeType::Sphere;
	rbB.shape.data = SphereData{1.0f};
	world.addComponent(idB, rbB);

	CollisionBridge collision;
	auto pairs = collision.detectCollisions(world);

	REQUIRE(pairs.empty());
}

// ── PhysicsSystem update cycle ────────────────────────────

TEST_CASE("PhysicsSystem update cycle runs full pipeline", "[physics][system]")
{
	PhysicsBridge bridge;
	PhysicsConfig config;
	config.gravity = {0.0f, -10.0f, 0.0f};
	bridge.init(config);

	CollisionBridge collision;

	GameWorld world;
	auto id = world.createEntity("DynamicBody");
	world.addComponent(id, TransformComponent{{0.0f, 50.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f}});
	world.addComponent(id, RigidBodyComponent{});

	PhysicsSystem system(bridge, collision);
	REQUIRE(system.name() == "PhysicsSystem");

	// 複数フレーム更新
	for (int i = 0; i < 30; ++i)
	{
		system.update(world, 1.0f / 60.0f);
	}

	auto* transform = world.getComponent<TransformComponent>(id);
	REQUIRE(transform != nullptr);
	// 重力で下に落ちているはず
	REQUIRE(transform->position.y < 50.0f);
}

// ── CollisionBridge AABB detection ────────────────────────

TEST_CASE("CollisionBridge detects AABB-AABB collision", "[physics][collision][aabb]")
{
	GameWorld world;

	auto idA = world.createEntity("BoxA");
	world.addComponent(idA, TransformComponent{{0.0f, 0.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f}});
	RigidBodyComponent rbA;
	rbA.shape.type = ColliderShapeType::Box;
	rbA.shape.data = BoxData{{1.0f, 1.0f, 1.0f}};
	world.addComponent(idA, rbA);

	auto idB = world.createEntity("BoxB");
	world.addComponent(idB, TransformComponent{{1.5f, 0.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f}});
	RigidBodyComponent rbB;
	rbB.shape.type = ColliderShapeType::Box;
	rbB.shape.data = BoxData{{1.0f, 1.0f, 1.0f}};
	world.addComponent(idB, rbB);

	CollisionBridge collision;
	auto pairs = collision.detectCollisions(world);

	REQUIRE(pairs.size() == 1);
	REQUIRE(pairs[0].penetration == Approx(0.5f).margin(0.01f));
	// 最小重なり軸はX軸
	REQUIRE(std::abs(pairs[0].normal.x) == Approx(1.0f));
}

// ── PhysicsSystem integrates with SystemRunner ────────────

TEST_CASE("PhysicsSystem works with SystemRunner", "[physics][system][runner]")
{
	PhysicsBridge bridge;
	bridge.init(PhysicsConfig{});

	CollisionBridge collision;

	GameWorld world;
	auto id = world.createEntity("TestEntity");
	world.addComponent(id, TransformComponent{{0.0f, 0.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f}});

	RigidBodyComponent rb;
	rb.mass = 0.0f;  // 静的ボディ
	world.addComponent(id, rb);

	SystemRunner runner;
	runner.addSystem(std::make_unique<PhysicsSystem>(bridge, collision), 100);

	REQUIRE(runner.systemCount() == 1);
	REQUIRE(runner.isEnabled("PhysicsSystem"));

	// 実行してもクラッシュしないことを確認
	runner.updateAll(world, 1.0f / 60.0f);

	auto* transform = world.getComponent<TransformComponent>(id);
	REQUIRE(transform != nullptr);
	// 静的ボディなので位置は変わらない
	REQUIRE(transform->position.x == Approx(0.0f));
	REQUIRE(transform->position.y == Approx(0.0f));
	REQUIRE(transform->position.z == Approx(0.0f));
}
