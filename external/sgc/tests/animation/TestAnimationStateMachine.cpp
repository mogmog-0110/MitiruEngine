#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/animation/AnimationStateMachine.hpp"

using namespace sgc;
using namespace sgc::animation;
using Approx = Catch::Approx;

namespace
{

/// @brief テスト用のスプライトシートとアニメーションを構築する
struct TestFixture
{
	asset::SpriteSheet sheet;
	asset::SpriteAnimation idleAnim;
	asset::SpriteAnimation runAnim;
	asset::SpriteAnimation attackAnim;

	TestFixture()
	{
		sheet.addFrame("f0", {0, 0, 32, 32});
		sheet.addFrame("f1", {32, 0, 32, 32});
		sheet.addFrame("f2", {64, 0, 32, 32});
		sheet.addFrame("f3", {96, 0, 32, 32});

		idleAnim.name = "idle";
		idleAnim.frameIndices = {0, 1};
		idleAnim.frameDuration = 0.2f;
		idleAnim.loop = true;

		runAnim.name = "run";
		runAnim.frameIndices = {2, 3};
		runAnim.frameDuration = 0.1f;
		runAnim.loop = true;

		attackAnim.name = "attack";
		attackAnim.frameIndices = {0, 1, 2, 3};
		attackAnim.frameDuration = 0.1f;
		attackAnim.loop = false;
	}
};

} // anonymous namespace

TEST_CASE("Add states and count", "[animation][statemachine]")
{
	TestFixture fix;
	AnimationStateMachine sm(fix.sheet);

	sm.addState({"idle", fix.idleAnim, true, true});
	sm.addState({"run", fix.runAnim, true, true});
	sm.addState({"attack", fix.attackAnim, false, false});

	REQUIRE(sm.stateCount() == 3);
}

TEST_CASE("Set initial state", "[animation][statemachine]")
{
	TestFixture fix;
	AnimationStateMachine sm(fix.sheet);

	sm.addState({"idle", fix.idleAnim, true, true});
	sm.setInitialState("idle");

	REQUIRE(sm.currentStateName() == "idle");
}

TEST_CASE("Update advances frames", "[animation][statemachine]")
{
	TestFixture fix;
	AnimationStateMachine sm(fix.sheet);

	sm.addState({"idle", fix.idleAnim, true, true});
	sm.setInitialState("idle");

	// フレーム0の状態を確認する
	const auto& frame0 = sm.currentFrame();
	REQUIRE(frame0.x == 0);

	// 0.2秒後（frameDuration=0.2）にフレーム1へ進む
	sm.update(0.25f);
	const auto& frame1 = sm.currentFrame();
	REQUIRE(frame1.x == 32);
}

TEST_CASE("Transition fires when condition is true", "[animation][statemachine]")
{
	TestFixture fix;
	AnimationStateMachine sm(fix.sheet);

	sm.addState({"idle", fix.idleAnim, true, true});
	sm.addState({"run", fix.runAnim, true, true});
	sm.setInitialState("idle");

	bool shouldRun = false;
	AnimationTransition trans;
	trans.fromState = "idle";
	trans.toState = "run";
	trans.condition = [&]{ return shouldRun; };
	sm.addTransition(std::move(trans));

	sm.update(0.016f);
	REQUIRE(sm.currentStateName() == "idle");

	shouldRun = true;
	sm.update(0.016f);
	REQUIRE(sm.currentStateName() == "run");
}

TEST_CASE("Transition does not fire when condition is false", "[animation][statemachine]")
{
	TestFixture fix;
	AnimationStateMachine sm(fix.sheet);

	sm.addState({"idle", fix.idleAnim, true, true});
	sm.addState({"run", fix.runAnim, true, true});
	sm.setInitialState("idle");

	AnimationTransition trans;
	trans.fromState = "idle";
	trans.toState = "run";
	trans.condition = []{ return false; };
	sm.addTransition(std::move(trans));

	sm.update(0.016f);
	sm.update(0.016f);
	sm.update(0.016f);

	REQUIRE(sm.currentStateName() == "idle");
}

TEST_CASE("Force state ignores conditions", "[animation][statemachine]")
{
	TestFixture fix;
	AnimationStateMachine sm(fix.sheet);

	sm.addState({"idle", fix.idleAnim, true, true});
	sm.addState({"run", fix.runAnim, true, true});
	sm.setInitialState("idle");

	// 遷移条件なしでも強制遷移できる
	sm.forceState("run");
	REQUIRE(sm.currentStateName() == "run");
}

TEST_CASE("Higher priority transition wins", "[animation][statemachine]")
{
	TestFixture fix;
	AnimationStateMachine sm(fix.sheet);

	sm.addState({"idle", fix.idleAnim, true, true});
	sm.addState({"run", fix.runAnim, true, true});
	sm.addState({"attack", fix.attackAnim, false, true});
	sm.setInitialState("idle");

	AnimationTransition toRun;
	toRun.fromState = "idle";
	toRun.toState = "run";
	toRun.condition = []{ return true; };
	toRun.priority = 1;
	sm.addTransition(std::move(toRun));

	AnimationTransition toAttack;
	toAttack.fromState = "idle";
	toAttack.toState = "attack";
	toAttack.condition = []{ return true; };
	toAttack.priority = 10;  // 高優先度
	sm.addTransition(std::move(toAttack));

	sm.update(0.016f);

	// 高優先度のattackが選択されるべき
	REQUIRE(sm.currentStateName() == "attack");
}

TEST_CASE("Wildcard from-state matches any", "[animation][statemachine]")
{
	TestFixture fix;
	AnimationStateMachine sm(fix.sheet);

	sm.addState({"idle", fix.idleAnim, true, true});
	sm.addState({"run", fix.runAnim, true, true});
	sm.addState({"attack", fix.attackAnim, false, true});
	sm.setInitialState("run");

	bool triggerAttack = false;
	AnimationTransition wildcard;
	wildcard.fromState = "";  // ワイルドカード
	wildcard.toState = "attack";
	wildcard.condition = [&]{ return triggerAttack; };
	sm.addTransition(std::move(wildcard));

	sm.update(0.016f);
	REQUIRE(sm.currentStateName() == "run");

	triggerAttack = true;
	sm.update(0.016f);

	// runからでもワイルドカード遷移でattackへ
	REQUIRE(sm.currentStateName() == "attack");
}

TEST_CASE("Current frame returns valid frame", "[animation][statemachine]")
{
	TestFixture fix;
	AnimationStateMachine sm(fix.sheet);

	sm.addState({"idle", fix.idleAnim, true, true});
	sm.setInitialState("idle");

	const auto& frame = sm.currentFrame();

	// 最初のフレームが返される
	REQUIRE(frame.width == 32);
	REQUIRE(frame.height == 32);
}

TEST_CASE("Non-interruptible state blocks transition", "[animation][statemachine]")
{
	TestFixture fix;
	AnimationStateMachine sm(fix.sheet);

	sm.addState({"idle", fix.idleAnim, true, true});
	// 攻撃はinterruptible=falseで、looping=false
	sm.addState({"attack", fix.attackAnim, false, false});
	sm.setInitialState("attack");

	AnimationTransition toIdle;
	toIdle.fromState = "attack";
	toIdle.toState = "idle";
	toIdle.condition = []{ return true; };
	sm.addTransition(std::move(toIdle));

	// アニメーション未完了のため遷移がブロックされる
	sm.update(0.016f);
	REQUIRE(sm.currentStateName() == "attack");

	// アニメーション完了まで進める（4フレーム * 0.1秒 = 0.4秒）
	sm.update(0.5f);
	// 完了後は遷移可能
	sm.update(0.016f);
	REQUIRE(sm.currentStateName() == "idle");
}
