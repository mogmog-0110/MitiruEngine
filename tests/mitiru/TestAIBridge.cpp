#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "mitiru/ai/BehaviorBridge.hpp"
#include "mitiru/ai/PathfindingBridge.hpp"
#include "mitiru/ai/AISystem.hpp"
#include "mitiru/scene/GameWorld.hpp"
#include "mitiru/scene/SystemRunner.hpp"

using namespace mitiru::ai;
using namespace mitiru::scene;

// ── BehaviorTreeComponent ─────────────────────────

TEST_CASE("BehaviorTreeComponent has correct defaults", "[ai][behavior]")
{
	BehaviorTreeComponent btc;
	REQUIRE(btc.treeId.empty());
	REQUIRE(btc.blackboard.empty());
	REQUIRE(btc.tickRate == Catch::Approx(0.1f));
	REQUIRE(btc.lastTickTime == Catch::Approx(0.0f));
}

// ── FSMComponent ──────────────────────────────────

TEST_CASE("FSMComponent has correct defaults", "[ai][fsm]")
{
	FSMComponent fsm;
	REQUIRE(fsm.fsmId.empty());
	REQUIRE(fsm.currentState.empty());
	REQUIRE(fsm.stateData.empty());
}

// ── BehaviorBridge register and tick ──────────────

TEST_CASE("BehaviorBridge registers trees and FSMs", "[ai][bridge]")
{
	BehaviorBridge bridge;
	REQUIRE(bridge.treeCount() == 0);
	REQUIRE(bridge.fsmCount() == 0);

	bridge.registerBehaviorTree("patrol", R"({"type":"sequence"})");
	bridge.registerFSM("enemy_fsm", R"({"states":["idle","chase"]})");

	REQUIRE(bridge.treeCount() == 1);
	REQUIRE(bridge.fsmCount() == 1);
}

TEST_CASE("BehaviorBridge ticks behavior trees", "[ai][bridge]")
{
	BehaviorBridge bridge;
	bridge.registerBehaviorTree("patrol", R"({"type":"sequence"})");

	GameWorld world;
	auto eid = world.createEntity("Agent");
	BehaviorTreeComponent btc;
	btc.treeId = "patrol";
	btc.tickRate = 0.05f;
	world.addComponent(eid, btc);

	REQUIRE(bridge.btTickCount() == 0);

	// tickRateを超えるdtで呼び出し → ツリーが実行される
	bridge.tickBehaviorTrees(world, 0.1f);
	REQUIRE(bridge.btTickCount() == 1);

	// ブラックボードに _lastTick が書き込まれたか確認
	const auto* updated = world.getComponent<BehaviorTreeComponent>(eid);
	REQUIRE(updated != nullptr);
	auto it = updated->blackboard.find("_lastTick");
	REQUIRE(it != updated->blackboard.end());
	REQUIRE(it->second == Catch::Approx(1.0f));
}

TEST_CASE("BehaviorBridge ticks FSMs", "[ai][bridge]")
{
	BehaviorBridge bridge;
	bridge.registerFSM("enemy_fsm", R"({"states":["idle"]})");

	GameWorld world;
	auto eid = world.createEntity("Enemy");
	FSMComponent fsm;
	fsm.fsmId = "enemy_fsm";
	fsm.currentState = "idle";
	world.addComponent(eid, fsm);

	bridge.tickFSMs(world, 0.016f);
	REQUIRE(bridge.fsmTickCount() == 1);

	const auto* updated = world.getComponent<FSMComponent>(eid);
	REQUIRE(updated != nullptr);
	auto it = updated->stateData.find("_lastTick");
	REQUIRE(it != updated->stateData.end());
	REQUIRE(it->second == "1");
}

// ── Blackboard set/get roundtrip ──────────────────

TEST_CASE("BehaviorBridge blackboard set/get roundtrip", "[ai][bridge]")
{
	BehaviorBridge bridge;
	GameWorld world;
	auto eid = world.createEntity("Agent");
	world.addComponent(eid, BehaviorTreeComponent{"tree1"});

	bridge.setBlackboardValue(world, eid, "health", 100.0f);
	bridge.setBlackboardValue(world, eid, "ammo", 30.0f);

	auto health = bridge.getBlackboardValue(world, eid, "health");
	REQUIRE(health.has_value());
	REQUIRE(health.value() == Catch::Approx(100.0f));

	auto ammo = bridge.getBlackboardValue(world, eid, "ammo");
	REQUIRE(ammo.has_value());
	REQUIRE(ammo.value() == Catch::Approx(30.0f));

	// 存在しないキー
	auto missing = bridge.getBlackboardValue(world, eid, "nonexistent");
	REQUIRE_FALSE(missing.has_value());

	// 存在しないエンティティ
	auto invalid = bridge.getBlackboardValue(world, 9999, "health");
	REQUIRE_FALSE(invalid.has_value());
}

// ── NavGrid creation and walkability ──────────────

TEST_CASE("NavGrid creation and walkability check", "[ai][pathfinding]")
{
	NavGrid grid;
	grid.width = 5;
	grid.height = 5;
	grid.walkable.assign(25, true);
	grid.cellSize = 1.0f;

	PathfindingBridge pathfinder;
	pathfinder.setNavGrid(grid);

	REQUIRE(pathfinder.isWalkable(0, 0));
	REQUIRE(pathfinder.isWalkable(4, 4));
	REQUIRE_FALSE(pathfinder.isWalkable(-1, 0));
	REQUIRE_FALSE(pathfinder.isWalkable(5, 0));

	pathfinder.setWalkable(2, 2, false);
	REQUIRE_FALSE(pathfinder.isWalkable(2, 2));
}

// ── A* pathfinding simple case ────────────────────

TEST_CASE("A* pathfinding finds simple path", "[ai][pathfinding]")
{
	NavGrid grid;
	grid.width = 5;
	grid.height = 5;
	grid.walkable.assign(25, true);
	grid.cellSize = 1.0f;

	PathfindingBridge pathfinder;
	pathfinder.setNavGrid(grid);

	auto result = pathfinder.findPath({0, 0, 4, 4});
	REQUIRE(result.found);
	REQUIRE_FALSE(result.path.empty());

	// 始点と終点が正しい
	REQUIRE(result.path.front() == std::make_pair(0, 0));
	REQUIRE(result.path.back() == std::make_pair(4, 4));

	// マンハッタン距離 = 8
	REQUIRE(result.cost == Catch::Approx(8.0f));
}

// ── A* pathfinding with obstacles ─────────────────

TEST_CASE("A* pathfinding avoids obstacles", "[ai][pathfinding]")
{
	// 5x5グリッド、中央列を壁にする（y=1..3, x=2）
	NavGrid grid;
	grid.width = 5;
	grid.height = 5;
	grid.walkable.assign(25, true);
	grid.cellSize = 1.0f;

	PathfindingBridge pathfinder;
	pathfinder.setNavGrid(grid);
	pathfinder.setWalkable(2, 1, false);
	pathfinder.setWalkable(2, 2, false);
	pathfinder.setWalkable(2, 3, false);

	auto result = pathfinder.findPath({0, 2, 4, 2});
	REQUIRE(result.found);

	// 経路がx=2のセルを通らないことを確認
	for (const auto& [px, py] : result.path)
	{
		if (py >= 1 && py <= 3)
		{
			REQUIRE(px != 2);
		}
	}
}

// ── A* pathfinding no path ────────────────────────

TEST_CASE("A* pathfinding returns not found when blocked", "[ai][pathfinding]")
{
	// 3x3グリッド、中央行を完全に壁にして上下を分断
	NavGrid grid;
	grid.width = 3;
	grid.height = 3;
	grid.walkable.assign(9, true);
	grid.cellSize = 1.0f;

	PathfindingBridge pathfinder;
	pathfinder.setNavGrid(grid);
	pathfinder.setWalkable(0, 1, false);
	pathfinder.setWalkable(1, 1, false);
	pathfinder.setWalkable(2, 1, false);

	auto result = pathfinder.findPath({1, 0, 1, 2});
	REQUIRE_FALSE(result.found);
	REQUIRE(result.path.empty());
}

// ── Path smoothing ────────────────────────────────

TEST_CASE("PathResult smoothing reduces waypoints", "[ai][pathfinding]")
{
	// 直線経路: (0,0) -> (1,0) -> (2,0) -> (3,0) -> (4,0)
	// 平滑化後: (0,0) -> (4,0) の2点のみ
	NavGrid grid;
	grid.width = 5;
	grid.height = 1;
	grid.walkable.assign(5, true);
	grid.cellSize = 1.0f;

	PathfindingBridge pathfinder;
	pathfinder.setNavGrid(grid);

	PathResult input;
	input.found = true;
	input.cost = 4.0f;
	input.path = {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}};

	auto smoothed = pathfinder.smoothPath(input);
	REQUIRE(smoothed.found);
	REQUIRE(smoothed.path.size() <= input.path.size());
	REQUIRE(smoothed.path.front() == std::make_pair(0, 0));
	REQUIRE(smoothed.path.back() == std::make_pair(4, 0));

	// 直線なので2点に削減される
	REQUIRE(smoothed.path.size() == 2);
}

// ── AISystem update cycle ─────────────────────────

TEST_CASE("AISystem update drives BehaviorBridge", "[ai][system]")
{
	BehaviorBridge bridge;
	bridge.registerBehaviorTree("simple", R"({"type":"leaf"})");
	bridge.registerFSM("simple_fsm", R"({"states":["idle"]})");

	GameWorld world;
	auto eid = world.createEntity("AI_Agent");

	BehaviorTreeComponent btc;
	btc.treeId = "simple";
	btc.tickRate = 0.01f;
	world.addComponent(eid, btc);

	FSMComponent fsm;
	fsm.fsmId = "simple_fsm";
	fsm.currentState = "idle";
	world.addComponent(eid, fsm);

	auto aiSystem = std::make_unique<AISystem>(bridge);
	REQUIRE(aiSystem->name() == "AISystem");

	SystemRunner runner;
	runner.addSystem(std::move(aiSystem), 0);

	// 1回のupdateAllでBTとFSMが両方ティックされる
	runner.updateAll(world, 0.1f);

	REQUIRE(bridge.btTickCount() == 1);
	REQUIRE(bridge.fsmTickCount() == 1);
}

// ── A* pathfinding same start and goal ────────────

TEST_CASE("A* pathfinding handles same start and goal", "[ai][pathfinding]")
{
	NavGrid grid;
	grid.width = 3;
	grid.height = 3;
	grid.walkable.assign(9, true);
	grid.cellSize = 1.0f;

	PathfindingBridge pathfinder;
	pathfinder.setNavGrid(grid);

	auto result = pathfinder.findPath({1, 1, 1, 1});
	REQUIRE(result.found);
	REQUIRE(result.path.size() == 1);
	REQUIRE(result.cost == Catch::Approx(0.0f));
}
