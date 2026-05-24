#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/physics/PhysicsWorld2D.hpp"

using namespace sgc::physics;
using Approx = Catch::Approx;

TEST_CASE("PhysicsWorld2D - initial state", "[physics][PhysicsWorld2D]")
{
	PhysicsWorld2Df world;
	REQUIRE(world.bodyCount() == 0);
	REQUIRE(world.gravity().x == 0.0f);
	REQUIRE(world.gravity().y == 0.0f);
}

TEST_CASE("PhysicsWorld2D - set gravity", "[physics][PhysicsWorld2D]")
{
	PhysicsWorld2Df world;
	world.setGravity({0.0f, 980.0f});
	REQUIRE(world.gravity().x == 0.0f);
	REQUIRE(world.gravity().y == 980.0f);
}

TEST_CASE("PhysicsWorld2D - construct with gravity", "[physics][PhysicsWorld2D]")
{
	PhysicsWorld2Df world({0.0f, 100.0f});
	REQUIRE(world.gravity().y == 100.0f);
}

TEST_CASE("PhysicsWorld2D - add and remove bodies", "[physics][PhysicsWorld2D]")
{
	PhysicsWorld2Df world;
	RigidBody2Df bodyA;
	RigidBody2Df bodyB;

	world.addBody(&bodyA, {8.0f, 8.0f});
	REQUIRE(world.bodyCount() == 1);

	world.addBody(&bodyB, {16.0f, 16.0f});
	REQUIRE(world.bodyCount() == 2);

	world.removeBody(&bodyA);
	REQUIRE(world.bodyCount() == 1);

	world.removeBody(&bodyB);
	REQUIRE(world.bodyCount() == 0);
}

TEST_CASE("PhysicsWorld2D - add null body is ignored", "[physics][PhysicsWorld2D]")
{
	PhysicsWorld2Df world;
	world.addBody(nullptr, {8.0f, 8.0f});
	REQUIRE(world.bodyCount() == 0);
}

TEST_CASE("PhysicsWorld2D - clear removes all bodies", "[physics][PhysicsWorld2D]")
{
	PhysicsWorld2Df world;
	RigidBody2Df a, b, c;
	world.addBody(&a, {4.0f, 4.0f});
	world.addBody(&b, {4.0f, 4.0f});
	world.addBody(&c, {4.0f, 4.0f});
	REQUIRE(world.bodyCount() == 3);

	world.clear();
	REQUIRE(world.bodyCount() == 0);
}

TEST_CASE("PhysicsWorld2D - step applies gravity", "[physics][PhysicsWorld2D]")
{
	PhysicsWorld2Df world({0.0f, 100.0f});

	RigidBody2Df body;
	body.position = {0.0f, 0.0f};
	body.mass = 1.0f;
	world.addBody(&body, {8.0f, 8.0f});

	world.step(1.0f);

	// 重力でY方向に加速するはず
	REQUIRE(body.velocity.y > 0.0f);
	REQUIRE(body.position.y > 0.0f);
}

TEST_CASE("PhysicsWorld2D - static body not affected by gravity", "[physics][PhysicsWorld2D]")
{
	PhysicsWorld2Df world({0.0f, 100.0f});

	RigidBody2Df body;
	body.position = {50.0f, 50.0f};
	body.isStatic = true;
	world.addBody(&body, {16.0f, 16.0f});

	world.step(1.0f);

	REQUIRE(body.position.x == 50.0f);
	REQUIRE(body.position.y == 50.0f);
	REQUIRE(body.velocity.x == 0.0f);
	REQUIRE(body.velocity.y == 0.0f);
}

TEST_CASE("PhysicsWorld2D - collision detection between two bodies", "[physics][PhysicsWorld2D]")
{
	PhysicsWorld2Df world;

	// 2つの重なるボディを配置
	RigidBody2Df a;
	a.position = {0.0f, 0.0f};
	a.mass = 1.0f;

	RigidBody2Df b;
	b.position = {10.0f, 0.0f};
	b.mass = 1.0f;

	// ハーフサイズ8で、距離10なら重なり=6
	world.addBody(&a, {8.0f, 8.0f});
	world.addBody(&b, {8.0f, 8.0f});

	world.step(0.0f);  // dt=0で積分は無効、衝突応答のみ

	// 衝突解消で離される
	REQUIRE(a.position.x < 0.0f);
	REQUIRE(b.position.x > 10.0f);
}

TEST_CASE("PhysicsWorld2D - dynamic vs static collision", "[physics][PhysicsWorld2D]")
{
	PhysicsWorld2Df world({0.0f, 980.0f});

	// 落下するボディ
	RigidBody2Df falling;
	falling.position = {50.0f, 90.0f};
	falling.mass = 1.0f;
	falling.restitution = 0.0f;

	// 地面（静的）
	RigidBody2Df ground;
	ground.position = {50.0f, 110.0f};
	ground.isStatic = true;

	world.addBody(&falling, {8.0f, 8.0f});
	world.addBody(&ground, {100.0f, 8.0f});

	// 重なっている状態でstep
	world.step(0.0f);

	// 落下ボディが地面の上に押し出される
	REQUIRE(falling.position.y < 95.0f);
}

TEST_CASE("PhysicsWorld2D - no collision between two static bodies", "[physics][PhysicsWorld2D]")
{
	PhysicsWorld2Df world;

	RigidBody2Df a;
	a.position = {0.0f, 0.0f};
	a.isStatic = true;

	RigidBody2Df b;
	b.position = {5.0f, 0.0f};
	b.isStatic = true;

	world.addBody(&a, {8.0f, 8.0f});
	world.addBody(&b, {8.0f, 8.0f});

	world.step(1.0f);

	// 静的同士は位置が変わらない
	REQUIRE(a.position.x == 0.0f);
	REQUIRE(b.position.x == 5.0f);
}

TEST_CASE("PhysicsWorld2D - multiple steps accumulate", "[physics][PhysicsWorld2D]")
{
	PhysicsWorld2Df world({0.0f, 100.0f});

	RigidBody2Df body;
	body.position = {0.0f, 0.0f};
	body.mass = 1.0f;
	world.addBody(&body, {4.0f, 4.0f});

	world.step(1.0f / 60.0f);
	const float y1 = body.position.y;

	world.step(1.0f / 60.0f);
	const float y2 = body.position.y;

	// 2ステップ目はさらに落下しているはず
	REQUIRE(y2 > y1);
	REQUIRE(y1 > 0.0f);
}

TEST_CASE("PhysicsWorld2D - IPhysicsWorld2D interface usage", "[physics][PhysicsWorld2D]")
{
	PhysicsWorld2Df world;
	IPhysicsWorld2Df& iface = world;

	RigidBody2Df body;
	body.mass = 1.0f;

	iface.setGravity({0.0f, 50.0f});
	iface.addBody(&body, {4.0f, 4.0f});
	REQUIRE(iface.bodyCount() == 1);
	REQUIRE(iface.gravity().y == 50.0f);

	iface.step(0.1f);
	REQUIRE(body.velocity.y > 0.0f);

	iface.clear();
	REQUIRE(iface.bodyCount() == 0);
}
