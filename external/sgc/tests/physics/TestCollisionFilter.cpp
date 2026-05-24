#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/physics/CollisionFilter.hpp"
#include "sgc/physics/PhysicsWorld2D.hpp"

using namespace sgc::physics;
using Approx = Catch::Approx;

TEST_CASE("CollisionFilter - shouldCollide returns true for default filters", "[physics][CollisionFilter]")
{
	const CollisionFilter a;
	const CollisionFilter b;
	REQUIRE(shouldCollide(a, b));
}

TEST_CASE("CollisionFilter - shouldCollide returns false when masks dont overlap", "[physics][CollisionFilter]")
{
	CollisionFilter a;
	a.layer = 0x01;
	a.mask  = 0x01;

	CollisionFilter b;
	b.layer = 0x02;
	b.mask  = 0x02;

	REQUIRE_FALSE(shouldCollide(a, b));
}

TEST_CASE("CollisionFilter - shouldCollide is symmetric", "[physics][CollisionFilter]")
{
	CollisionFilter a;
	a.layer = 0x01;
	a.mask  = 0x02;

	CollisionFilter b;
	b.layer = 0x02;
	b.mask  = 0x01;

	REQUIRE(shouldCollide(a, b));
	REQUIRE(shouldCollide(b, a));
}

TEST_CASE("CollisionFilter - shouldCollide with specific layer bits", "[physics][CollisionFilter]")
{
	CollisionFilter a;
	a.layer = 0x04;
	a.mask  = 0x08;

	CollisionFilter b;
	b.layer = 0x08;
	b.mask  = 0x04;

	REQUIRE(shouldCollide(a, b));

	// 片方のマスクを変更して不一致にする
	b.mask = 0x08;
	REQUIRE_FALSE(shouldCollide(a, b));
}

TEST_CASE("CollisionFilter - zero mask collides with nothing", "[physics][CollisionFilter]")
{
	CollisionFilter a;
	a.layer = 0xFFFFFFFF;
	a.mask  = 0x00000000;

	const CollisionFilter b;  // デフォルト（全ビット）

	REQUIRE_FALSE(shouldCollide(a, b));
}

TEST_CASE("CollisionFilter - single layer bit filtering", "[physics][CollisionFilter]")
{
	// プレイヤー（レイヤー1）は敵（レイヤー2）とだけ衝突
	CollisionFilter player;
	player.layer = 0x01;
	player.mask  = 0x02;

	// 敵（レイヤー2）はプレイヤー（レイヤー1）とだけ衝突
	CollisionFilter enemy;
	enemy.layer = 0x02;
	enemy.mask  = 0x01;

	// 弾（レイヤー4）は敵（レイヤー2）とだけ衝突
	CollisionFilter bullet;
	bullet.layer = 0x04;
	bullet.mask  = 0x02;

	REQUIRE(shouldCollide(player, enemy));
	REQUIRE_FALSE(shouldCollide(player, bullet));  // プレイヤーは弾レイヤーを対象にしていない
	REQUIRE_FALSE(shouldCollide(enemy, bullet));   // 敵はbulletレイヤーを対象にしていない
}

TEST_CASE("CollisionFilter - PhysicsWorld2D respects collision filter", "[physics][CollisionFilter][integration]")
{
	PhysicsWorld2Df world;

	// 2つの剛体を同じ位置付近に配置（重なるように）
	RigidBody2Df bodyA;
	bodyA.position = {50.0f, 50.0f};
	bodyA.mass = 1.0f;
	bodyA.restitution = 0.0f;
	bodyA.filter.layer = 0x01;
	bodyA.filter.mask  = 0x01;  // レイヤー1とだけ衝突

	RigidBody2Df bodyB;
	bodyB.position = {55.0f, 50.0f};  // Aと重なる位置
	bodyB.mass = 1.0f;
	bodyB.restitution = 0.0f;
	bodyB.filter.layer = 0x02;
	bodyB.filter.mask  = 0x02;  // レイヤー2とだけ衝突

	world.addBody(&bodyA, {10.0f, 10.0f});
	world.addBody(&bodyB, {10.0f, 10.0f});

	// 位置を記録
	const float posAx = bodyA.position.x;
	const float posBx = bodyB.position.x;

	// 物理ステップ実行（重力なし）
	world.step(1.0f / 60.0f);

	// フィルタが不一致なので衝突応答は発生しない → 位置は重力分のみ変化
	// 速度は0、重力も0なので、位置はほぼ変わらないはず
	REQUIRE(bodyA.position.x == Approx(posAx).margin(0.01f));
	REQUIRE(bodyB.position.x == Approx(posBx).margin(0.01f));
}
