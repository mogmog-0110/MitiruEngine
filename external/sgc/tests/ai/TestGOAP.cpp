/// @file TestGOAP.cpp
/// @brief GOAP.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/ai/GOAP.hpp"

// テスト用プロパティインデックス
enum Props
{
	HasAxe = 0,
	HasWood = 1,
	AtTree = 2,
	AtHome = 3,
	HasFire = 4,
};

TEST_CASE("WorldState set and get", "[ai][goap]")
{
	sgc::ai::WorldState ws;
	ws.set(HasAxe, true);
	ws.set(HasWood, false);

	REQUIRE(ws.get(HasAxe) == true);
	REQUIRE(ws.get(HasWood) == false);
	REQUIRE(ws.hasMask(HasAxe));
	REQUIRE(ws.hasMask(HasWood));
	REQUIRE_FALSE(ws.hasMask(AtTree));
}

TEST_CASE("WorldState satisfies checks masked properties only", "[ai][goap]")
{
	sgc::ai::WorldState current;
	current.set(HasAxe, true);
	current.set(HasWood, false);
	current.set(AtTree, true);

	sgc::ai::WorldState goal;
	goal.set(HasAxe, true);

	// ゴールはHasAxeのみ要求 → 満たす
	REQUIRE(current.satisfies(goal));

	goal.set(HasWood, true);
	// HasWoodも要求 → 満たさない
	REQUIRE_FALSE(current.satisfies(goal));
}

TEST_CASE("WorldState distanceTo counts mismatches", "[ai][goap]")
{
	sgc::ai::WorldState current;
	current.set(HasAxe, false);
	current.set(HasWood, false);
	current.set(AtTree, true);

	sgc::ai::WorldState goal;
	goal.set(HasAxe, true);
	goal.set(HasWood, true);

	REQUIRE(current.distanceTo(goal) == 2);
}

TEST_CASE("WorldState applyEffects modifies correctly", "[ai][goap]")
{
	sgc::ai::WorldState state;
	state.set(HasAxe, false);
	state.set(AtTree, true);

	sgc::ai::WorldState effects;
	effects.set(HasAxe, true);

	const auto result = state.applyEffects(effects);
	REQUIRE(result.get(HasAxe) == true);
	REQUIRE(result.get(AtTree) == true);
}

TEST_CASE("GOAPAction precondition check", "[ai][goap]")
{
	sgc::ai::GOAPAction chop("ChopTree", 2.0f);
	chop.setPrecondition(HasAxe, true);
	chop.setPrecondition(AtTree, true);

	sgc::ai::WorldState state;
	state.set(HasAxe, true);
	state.set(AtTree, true);

	REQUIRE(chop.isPreconditionMet(state));

	sgc::ai::WorldState noAxe;
	noAxe.set(HasAxe, false);
	noAxe.set(AtTree, true);

	REQUIRE_FALSE(chop.isPreconditionMet(noAxe));
}

TEST_CASE("GOAPAction apply effects", "[ai][goap]")
{
	sgc::ai::GOAPAction chop("ChopTree", 2.0f);
	chop.setEffect(HasWood, true);

	sgc::ai::WorldState state;
	state.set(HasAxe, true);
	state.set(AtTree, true);

	const auto result = chop.applyTo(state);
	REQUIRE(result.get(HasWood) == true);
	REQUIRE(result.get(HasAxe) == true);
}

TEST_CASE("GOAPPlanner finds single-step plan", "[ai][goap]")
{
	sgc::ai::GOAPPlanner planner;

	sgc::ai::GOAPAction getAxe("GetAxe", 1.0f);
	getAxe.setEffect(HasAxe, true);
	planner.addAction(std::move(getAxe));

	sgc::ai::WorldState start;
	start.set(HasAxe, false);

	sgc::ai::WorldState goal;
	goal.set(HasAxe, true);

	const auto plan = planner.plan(start, goal);
	REQUIRE(plan.has_value());
	REQUIRE(plan->actions.size() == 1);
	REQUIRE(plan->actions[0]->name() == "GetAxe");
	REQUIRE(plan->totalCost == Catch::Approx(1.0f));
}

TEST_CASE("GOAPPlanner finds multi-step plan", "[ai][goap]")
{
	sgc::ai::GOAPPlanner planner;

	sgc::ai::GOAPAction getAxe("GetAxe", 1.0f);
	getAxe.setEffect(HasAxe, true);
	planner.addAction(std::move(getAxe));

	sgc::ai::GOAPAction goToTree("GoToTree", 2.0f);
	goToTree.setEffect(AtTree, true);
	planner.addAction(std::move(goToTree));

	sgc::ai::GOAPAction chopTree("ChopTree", 3.0f);
	chopTree.setPrecondition(HasAxe, true);
	chopTree.setPrecondition(AtTree, true);
	chopTree.setEffect(HasWood, true);
	planner.addAction(std::move(chopTree));

	sgc::ai::WorldState start;
	start.set(HasAxe, false);
	start.set(AtTree, false);
	start.set(HasWood, false);

	sgc::ai::WorldState goal;
	goal.set(HasWood, true);

	const auto plan = planner.plan(start, goal);
	REQUIRE(plan.has_value());
	REQUIRE(plan->actions.size() == 3);

	// GetAxe + GoToTree + ChopTree = 6.0
	REQUIRE(plan->totalCost == Catch::Approx(6.0f));

	// 最後のアクションはChopTree
	REQUIRE(plan->actions.back()->name() == "ChopTree");
}

TEST_CASE("GOAPPlanner returns nullopt for impossible goal", "[ai][goap]")
{
	sgc::ai::GOAPPlanner planner;

	// HasFireを達成するアクションがない
	sgc::ai::GOAPAction getAxe("GetAxe", 1.0f);
	getAxe.setEffect(HasAxe, true);
	planner.addAction(std::move(getAxe));

	sgc::ai::WorldState start;
	sgc::ai::WorldState goal;
	goal.set(HasFire, true);

	const auto plan = planner.plan(start, goal, 100);
	REQUIRE_FALSE(plan.has_value());
}

TEST_CASE("GOAPPlanner returns empty plan when goal already satisfied", "[ai][goap]")
{
	sgc::ai::GOAPPlanner planner;

	sgc::ai::WorldState start;
	start.set(HasAxe, true);

	sgc::ai::WorldState goal;
	goal.set(HasAxe, true);

	const auto plan = planner.plan(start, goal);
	REQUIRE(plan.has_value());
	REQUIRE(plan->actions.empty());
	REQUIRE(plan->totalCost == Catch::Approx(0.0f));
}

TEST_CASE("GOAPPlanner prefers cheaper plan", "[ai][goap]")
{
	sgc::ai::GOAPPlanner planner;

	// 高コスト直接ルート
	sgc::ai::GOAPAction expensiveWood("ExpensiveWood", 10.0f);
	expensiveWood.setEffect(HasWood, true);
	planner.addAction(std::move(expensiveWood));

	// 低コスト2ステップルート
	sgc::ai::GOAPAction getAxe("GetAxe", 1.0f);
	getAxe.setEffect(HasAxe, true);
	planner.addAction(std::move(getAxe));

	sgc::ai::GOAPAction chopTree("ChopTree", 2.0f);
	chopTree.setPrecondition(HasAxe, true);
	chopTree.setEffect(HasWood, true);
	planner.addAction(std::move(chopTree));

	sgc::ai::WorldState start;
	start.set(HasAxe, false);
	start.set(HasWood, false);

	sgc::ai::WorldState goal;
	goal.set(HasWood, true);

	const auto plan = planner.plan(start, goal);
	REQUIRE(plan.has_value());
	// 安い方（GetAxe + ChopTree = 3.0）が選ばれる
	REQUIRE(plan->totalCost == Catch::Approx(3.0f));
}
