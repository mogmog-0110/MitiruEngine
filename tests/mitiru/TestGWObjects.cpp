#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "mitiru/graphwalker/GameObject.hpp"
#include "mitiru/graphwalker/Player.hpp"
#include "mitiru/graphwalker/Platforms.hpp"
#include "mitiru/graphwalker/Hazards.hpp"
#include "mitiru/graphwalker/Interactables.hpp"
#include "mitiru/graphwalker/ObjectFactory.hpp"
#include "mitiru/graphwalker/PhysicsIntegration.hpp"

using namespace mitiru::gw;
using Catch::Approx;

// ── Player tests ────────────────────────────────────────────

TEST_CASE("Player creation with defaults", "[gw][player]")
{
	Player player({100.0f, 200.0f});

	REQUIRE(player.getPosition().x == Approx(100.0f));
	REQUIRE(player.getPosition().y == Approx(200.0f));
	REQUIRE(player.getRadius() == Approx(Player::DEFAULT_RADIUS));
	REQUIRE(player.getLives() == Player::DEFAULT_LIVES);
	REQUIRE(player.isActive());
	REQUIRE_FALSE(player.isGrounded());
	REQUIRE_FALSE(player.isJumping());
	REQUIRE_FALSE(player.isDead());
	REQUIRE(player.getType() == ObjectType::Player);
}

TEST_CASE("Player jump only when grounded", "[gw][player]")
{
	Player player({100.0f, 200.0f});

	// ジャンプ不可（非接地）
	player.jump();
	REQUIRE_FALSE(player.isJumping());
	REQUIRE(player.getVelocity().y == Approx(0.0f));

	// 接地設定してジャンプ
	player.setGrounded(true);
	player.jump();
	REQUIRE(player.isJumping());
	REQUIRE(player.getVelocity().y < 0.0f);
	REQUIRE_FALSE(player.isGrounded());
}

TEST_CASE("Player takeDamage reduces lives", "[gw][player]")
{
	Player player({0.0f, 0.0f});

	REQUIRE(player.getLives() == 3);
	player.takeDamage();
	REQUIRE(player.getLives() == 2);
	player.takeDamage();
	REQUIRE(player.getLives() == 1);
	player.takeDamage();
	REQUIRE(player.getLives() == 0);
	REQUIRE(player.isDead());

	// ライフ0以下にはならない
	player.takeDamage();
	REQUIRE(player.getLives() == 0);
}

TEST_CASE("Player respawn resets state", "[gw][player]")
{
	Player player({100.0f, 200.0f});

	// 状態を変更
	player.setGrounded(true);
	player.jump();
	player.update(1.0f / 60.0f);

	// リスポーン
	player.respawn({50.0f, 50.0f});
	REQUIRE(player.getPosition().x == Approx(50.0f));
	REQUIRE(player.getPosition().y == Approx(50.0f));
	REQUIRE(player.getVelocity().x == Approx(0.0f));
	REQUIRE(player.getVelocity().y == Approx(0.0f));
	REQUIRE_FALSE(player.isGrounded());
	REQUIRE_FALSE(player.isJumping());
}

TEST_CASE("Player update applies gravity", "[gw][player]")
{
	Player player({100.0f, 100.0f});
	const float dt = 1.0f / 60.0f;

	player.update(dt);

	// 重力で下方向に加速
	REQUIRE(player.getVelocity().y > 0.0f);
	// 位置も下方に移動
	REQUIRE(player.getPosition().y > 100.0f);
}

// ── Platform tests ──────────────────────────────────────────

TEST_CASE("StaticPlatform bounds correct", "[gw][platform]")
{
	StaticPlatform plat({100.0f, 400.0f}, {200.0f, 20.0f});

	const auto bounds = plat.getBounds();
	REQUIRE(bounds.x() == Approx(100.0f));
	REQUIRE(bounds.y() == Approx(400.0f));
	REQUIRE(bounds.width() == Approx(200.0f));
	REQUIRE(bounds.height() == Approx(20.0f));
	REQUIRE(plat.getType() == ObjectType::StaticPlatform);
}

TEST_CASE("MovingPlatform moves between points", "[gw][platform]")
{
	MovingPlatform mp({0.0f, 300.0f}, {400.0f, 300.0f}, {100.0f, 20.0f}, 1.0f);

	REQUIRE(mp.getCurrentT() == Approx(0.0f));
	REQUIRE(mp.getPosition().x == Approx(0.0f));

	// 0.5秒進めると中間点付近
	mp.update(0.5f);
	REQUIRE(mp.getCurrentT() == Approx(0.5f));
	REQUIRE(mp.getPosition().x == Approx(200.0f));

	// 1.0秒進めると終点
	mp.update(0.5f);
	REQUIRE(mp.getCurrentT() == Approx(1.0f));
	REQUIRE(mp.getPosition().x == Approx(400.0f));

	// さらに進めると折り返し
	mp.update(0.25f);
	REQUIRE(mp.getCurrentT() == Approx(0.75f));
}

TEST_CASE("CrumblingPlatform crumbles after timer", "[gw][platform]")
{
	CrumblingPlatform cp({100.0f, 300.0f}, {80.0f, 20.0f}, 0.5f);

	REQUIRE_FALSE(cp.isCrumbling());
	REQUIRE_FALSE(cp.isDestroyed());
	REQUIRE(cp.isActive());

	// 崩壊開始
	cp.startCrumbling();
	REQUIRE(cp.isCrumbling());

	// まだ崩壊していない
	cp.update(0.3f);
	REQUIRE_FALSE(cp.isDestroyed());
	REQUIRE(cp.isActive());

	// 崩壊完了
	cp.update(0.3f);
	REQUIRE(cp.isDestroyed());
	REQUIRE_FALSE(cp.isActive());
}

TEST_CASE("SpringPlatform has bounce force", "[gw][platform]")
{
	SpringPlatform sp({100.0f, 400.0f}, {80.0f, 20.0f}, 1200.0f);

	REQUIRE(sp.getBounceForce() == Approx(1200.0f));
	REQUIRE(sp.getType() == ObjectType::SpringPlatform);
}

// ── Hazard tests ────────────────────────────────────────────

TEST_CASE("LaserBarrier toggles on/off", "[gw][hazard]")
{
	LaserBarrier laser({200.0f, 100.0f}, 150.0f, 1.0f, 0.5f);

	REQUIRE(laser.isOn());
	REQUIRE(laser.isActive());

	// オン状態が1秒続く
	laser.update(0.8f);
	REQUIRE(laser.isOn());

	// オフに切り替わる
	laser.update(0.3f);
	REQUIRE_FALSE(laser.isOn());
	REQUIRE_FALSE(laser.isActive());

	// オフ状態が0.5秒続く
	laser.update(0.4f);
	REQUIRE_FALSE(laser.isOn());

	// 再びオンに
	laser.update(0.2f);
	REQUIRE(laser.isOn());
}

TEST_CASE("PatrolEnemy patrols between points", "[gw][hazard]")
{
	PatrolEnemy enemy({0.0f, 100.0f}, {200.0f, 100.0f}, {20.0f, 20.0f}, 100.0f);

	REQUIRE(enemy.getPosition().x == Approx(0.0f));
	REQUIRE(enemy.getType() == ObjectType::PatrolEnemy);

	// 1秒で100ピクセル進む（全長200なのでt=0.5）
	enemy.update(1.0f);
	REQUIRE(enemy.getPosition().x == Approx(100.0f));

	// さらに1秒で終点到達
	enemy.update(1.0f);
	REQUIRE(enemy.getPosition().x == Approx(200.0f));

	// 折り返し: rate=100/200=0.5/sec, 0.5s backward → t=1.0-0.25=0.75 → x=150
	enemy.update(0.5f);
	REQUIRE(enemy.getPosition().x == Approx(150.0f));
}

// ── Interactable tests ──────────────────────────────────────

TEST_CASE("CollectibleBlock can be collected", "[gw][interactable]")
{
	CollectibleBlock block({100.0f, 200.0f}, {30.0f, 30.0f}, "btn_A");

	REQUIRE_FALSE(block.isCollected());
	REQUIRE(block.isActive());
	REQUIRE(block.getButtonId() == "btn_A");

	block.collect();
	REQUIRE(block.isCollected());
	REQUIRE_FALSE(block.isActive());
}

TEST_CASE("CheckpointObj can be activated", "[gw][interactable]")
{
	CheckpointObj cp({300.0f, 200.0f}, "cp_01");

	REQUIRE_FALSE(cp.isActivated());
	REQUIRE(cp.getCheckpointId() == "cp_01");

	cp.activate();
	REQUIRE(cp.isActivated());
}

TEST_CASE("GateObj opens with correct buttons", "[gw][interactable]")
{
	GateObj gate({400.0f, 100.0f}, {20.0f, 80.0f}, {"btn_A", "btn_B"});

	REQUIRE_FALSE(gate.isOpen());
	REQUIRE(gate.isActive());

	// ボタンA だけでは開かない
	gate.tryOpen({"btn_A"});
	REQUIRE_FALSE(gate.isOpen());
	REQUIRE(gate.isActive());

	// 両方揃えば開く
	gate.tryOpen({"btn_A", "btn_B"});
	REQUIRE(gate.isOpen());
	REQUIRE_FALSE(gate.isActive());
}

TEST_CASE("NpcObj interaction range check", "[gw][interactable]")
{
	NpcObj npc({100.0f, 100.0f}, "npc_01", "Hello!", 50.0f);

	// NPC中心は(115, 125) → 50px以内
	REQUIRE(npc.isInRange({115.0f, 125.0f}));
	// 遠すぎる
	REQUIRE_FALSE(npc.isInRange({300.0f, 300.0f}));
}

// ── Factory tests ───────────────────────────────────────────

TEST_CASE("ObjectFactory creates all types", "[gw][factory]")
{
	auto player = ObjectFactory::createPlayer({0, 0});
	REQUIRE(player != nullptr);
	REQUIRE(player->getType() == ObjectType::Player);

	auto sp = ObjectFactory::createStaticPlatform({0, 0}, {100, 20});
	REQUIRE(sp != nullptr);
	REQUIRE(sp->getType() == ObjectType::StaticPlatform);

	auto mp = ObjectFactory::createMovingPlatform({0, 0}, {100, 0}, {80, 20}, 1.0f);
	REQUIRE(mp != nullptr);
	REQUIRE(mp->getType() == ObjectType::MovingPlatform);

	auto crumb = ObjectFactory::createCrumblingPlatform({0, 0}, {80, 20});
	REQUIRE(crumb != nullptr);
	REQUIRE(crumb->getType() == ObjectType::CrumblingPlatform);

	auto spring = ObjectFactory::createSpringPlatform({0, 0}, {80, 20});
	REQUIRE(spring != nullptr);
	REQUIRE(spring->getType() == ObjectType::SpringPlatform);

	auto spike = ObjectFactory::createSpikeHazard({0, 0}, {100, 10});
	REQUIRE(spike != nullptr);
	REQUIRE(spike->getType() == ObjectType::SpikeHazard);

	auto laser = ObjectFactory::createLaserBarrier({0, 0}, 100.0f, 1.0f, 0.5f);
	REQUIRE(laser != nullptr);
	REQUIRE(laser->getType() == ObjectType::LaserBarrier);

	auto enemy = ObjectFactory::createPatrolEnemy({0, 0}, {100, 0}, {20, 20}, 50.0f);
	REQUIRE(enemy != nullptr);
	REQUIRE(enemy->getType() == ObjectType::PatrolEnemy);

	auto coll = ObjectFactory::createCollectible({0, 0}, {30, 30}, "btn_X");
	REQUIRE(coll != nullptr);
	REQUIRE(coll->getType() == ObjectType::CollectibleBlock);

	auto checkpoint = ObjectFactory::createCheckpoint({0, 0}, "cp_1");
	REQUIRE(checkpoint != nullptr);
	REQUIRE(checkpoint->getType() == ObjectType::Checkpoint);

	auto npc = ObjectFactory::createNpc({0, 0}, "npc_1", "Hi");
	REQUIRE(npc != nullptr);
	REQUIRE(npc->getType() == ObjectType::NPC);

	auto gate = ObjectFactory::createGate({0, 0}, {20, 80}, {"a", "b"});
	REQUIRE(gate != nullptr);
	REQUIRE(gate->getType() == ObjectType::Gate);

	auto goal = ObjectFactory::createGoal({0, 0});
	REQUIRE(goal != nullptr);
	REQUIRE(goal->getType() == ObjectType::Goal);
}

// ── Physics tests ───────────────────────────────────────────

TEST_CASE("GWPhysicsWorld collision detection", "[gw][physics]")
{
	// 重なるAABB
	sgc::Rectf a{0.0f, 0.0f, 50.0f, 50.0f};
	sgc::Rectf b{30.0f, 30.0f, 50.0f, 50.0f};
	REQUIRE(GWPhysicsWorld::checkCollision(a, b));

	// 離れたAABB
	sgc::Rectf c{200.0f, 200.0f, 50.0f, 50.0f};
	REQUIRE_FALSE(GWPhysicsWorld::checkCollision(a, c));
}

TEST_CASE("GWPhysicsWorld resolve collision result", "[gw][physics]")
{
	// プレイヤー中心(50,55) 半径15 → bounds(35,40,30,30) → bottom=70
	// プラットフォーム(35,60,100,20) → top=60
	// overlapY = 70-60 = 10 > 0 → 衝突
	Player player({50.0f, 55.0f}, 15.0f);
	StaticPlatform plat({35.0f, 60.0f}, {100.0f, 20.0f});

	// プレイヤーがプラットフォームと重なる位置に
	player.setVelocity({0.0f, 100.0f}); // 下方向に移動中
	const auto result = GWPhysicsWorld::resolveCollision(player, plat);

	REQUIRE(result.collided);
	REQUIRE(result.hitType == ObjectType::StaticPlatform);
	REQUIRE(result.penetration > 0.0f);
}

TEST_CASE("GWPhysicsWorld one-way collision skips upward", "[gw][physics]")
{
	Player player({50.0f, 55.0f}, 15.0f);
	StaticPlatform plat({35.0f, 60.0f}, {100.0f, 20.0f});

	// 上方向に移動中 → ワンウェイでスキップ
	player.setVelocity({0.0f, -200.0f});
	const auto result = GWPhysicsWorld::resolveCollision(player, plat);

	REQUIRE_FALSE(result.collided);
}

TEST_CASE("GWPhysicsWorld ground check", "[gw][physics]")
{
	GWPhysicsWorld physics;
	Player player({100.0f, 80.0f}, 15.0f);
	StaticPlatform plat({50.0f, 95.0f}, {200.0f, 20.0f});

	physics.addObject(&plat);

	// プレイヤーの足元(y=95)がプラットフォーム上面(y=95)に接触
	REQUIRE(physics.isGrounded(player));

	// プレイヤーを遠くに移動 → 接地なし
	player.setPosition({100.0f, 0.0f});
	REQUIRE_FALSE(physics.isGrounded(player));
}

TEST_CASE("GWPhysicsWorld add/remove objects", "[gw][physics]")
{
	GWPhysicsWorld physics;
	StaticPlatform plat1({0, 0}, {100, 20});
	StaticPlatform plat2({200, 0}, {100, 20});

	physics.addObject(&plat1);
	physics.addObject(&plat2);
	REQUIRE(physics.getObjectCount() == 2);

	physics.removeObject(&plat1);
	REQUIRE(physics.getObjectCount() == 1);
}
