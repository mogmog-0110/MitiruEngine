/// @file TestBehaviorTree.cpp
/// @brief BehaviorTree.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/ai/BehaviorTree.hpp"

using namespace sgc::bt;

TEST_CASE("Blackboard set and get", "[ai][bt]")
{
	Blackboard bb;
	bb.set<int>("health", 100);

	auto val = bb.get<int>("health");
	REQUIRE(val.has_value());
	REQUIRE(val.value() == 100);
}

TEST_CASE("Blackboard get returns nullopt for missing key", "[ai][bt]")
{
	Blackboard bb;
	auto val = bb.get<int>("missing");
	REQUIRE_FALSE(val.has_value());
}

TEST_CASE("Blackboard get returns nullopt for wrong type", "[ai][bt]")
{
	Blackboard bb;
	bb.set<int>("value", 42);
	auto val = bb.get<std::string>("value");
	REQUIRE_FALSE(val.has_value());
}

TEST_CASE("Blackboard has and remove", "[ai][bt]")
{
	Blackboard bb;
	bb.set<int>("x", 1);
	REQUIRE(bb.has("x"));

	bb.remove("x");
	REQUIRE_FALSE(bb.has("x"));
}

TEST_CASE("Blackboard clear", "[ai][bt]")
{
	Blackboard bb;
	bb.set<int>("a", 1);
	bb.set<int>("b", 2);
	bb.clear();
	REQUIRE_FALSE(bb.has("a"));
	REQUIRE_FALSE(bb.has("b"));
}

TEST_CASE("Action node returns function result", "[ai][bt]")
{
	Blackboard bb;
	Action act([](Blackboard&) { return Status::Success; });
	REQUIRE(act.tick(bb) == Status::Success);

	Action fail([](Blackboard&) { return Status::Failure; });
	REQUIRE(fail.tick(bb) == Status::Failure);
}

TEST_CASE("Condition node returns Success or Failure", "[ai][bt]")
{
	Blackboard bb;
	bb.set<int>("hp", 100);

	Condition cond([](Blackboard& b) { return b.get<int>("hp").value_or(0) > 50; });
	REQUIRE(cond.tick(bb) == Status::Success);

	bb.set<int>("hp", 10);
	REQUIRE(cond.tick(bb) == Status::Failure);
}

TEST_CASE("Sequence succeeds when all children succeed", "[ai][bt]")
{
	Blackboard bb;
	Sequence seq;
	seq.addChild(std::make_unique<Action>([](Blackboard&) { return Status::Success; }));
	seq.addChild(std::make_unique<Action>([](Blackboard&) { return Status::Success; }));

	REQUIRE(seq.tick(bb) == Status::Success);
}

TEST_CASE("Sequence fails on first failure", "[ai][bt]")
{
	Blackboard bb;
	int callCount = 0;

	Sequence seq;
	seq.addChild(std::make_unique<Action>([&](Blackboard&) { ++callCount; return Status::Failure; }));
	seq.addChild(std::make_unique<Action>([&](Blackboard&) { ++callCount; return Status::Success; }));

	REQUIRE(seq.tick(bb) == Status::Failure);
	REQUIRE(callCount == 1);
}

TEST_CASE("Selector succeeds on first success", "[ai][bt]")
{
	Blackboard bb;
	int callCount = 0;

	Selector sel;
	sel.addChild(std::make_unique<Action>([&](Blackboard&) { ++callCount; return Status::Success; }));
	sel.addChild(std::make_unique<Action>([&](Blackboard&) { ++callCount; return Status::Success; }));

	REQUIRE(sel.tick(bb) == Status::Success);
	REQUIRE(callCount == 1);
}

TEST_CASE("Selector fails when all children fail", "[ai][bt]")
{
	Blackboard bb;
	Selector sel;
	sel.addChild(std::make_unique<Action>([](Blackboard&) { return Status::Failure; }));
	sel.addChild(std::make_unique<Action>([](Blackboard&) { return Status::Failure; }));

	REQUIRE(sel.tick(bb) == Status::Failure);
}

TEST_CASE("Inverter flips Success and Failure", "[ai][bt]")
{
	Blackboard bb;
	Inverter inv(std::make_unique<Action>([](Blackboard&) { return Status::Success; }));
	REQUIRE(inv.tick(bb) == Status::Failure);

	Inverter inv2(std::make_unique<Action>([](Blackboard&) { return Status::Failure; }));
	REQUIRE(inv2.tick(bb) == Status::Success);
}

TEST_CASE("Inverter passes Running through", "[ai][bt]")
{
	Blackboard bb;
	Inverter inv(std::make_unique<Action>([](Blackboard&) { return Status::Running; }));
	REQUIRE(inv.tick(bb) == Status::Running);
}

TEST_CASE("Repeater runs child N times", "[ai][bt]")
{
	Blackboard bb;
	int callCount = 0;

	Repeater rep(std::make_unique<Action>([&](Blackboard&) { ++callCount; return Status::Success; }), 5);
	REQUIRE(rep.tick(bb) == Status::Success);
	REQUIRE(callCount == 5);
}

TEST_CASE("Succeeder always returns Success", "[ai][bt]")
{
	Blackboard bb;
	Succeeder succ(std::make_unique<Action>([](Blackboard&) { return Status::Failure; }));
	REQUIRE(succ.tick(bb) == Status::Success);
}

TEST_CASE("Builder creates simple action tree", "[ai][bt]")
{
	auto tree = Builder()
		.action([](Blackboard& bb) { bb.set("done", true); return Status::Success; })
		.build();

	REQUIRE(tree != nullptr);
	Blackboard bb;
	REQUIRE(tree->tick(bb) == Status::Success);
	REQUIRE(bb.get<bool>("done").value_or(false));
}

TEST_CASE("Builder creates selector with children", "[ai][bt]")
{
	auto tree = Builder()
		.selector()
			.action([](Blackboard&) { return Status::Failure; })
			.action([](Blackboard& bb) { bb.set("chosen", 2); return Status::Success; })
		.end()
		.build();

	Blackboard bb;
	REQUIRE(tree->tick(bb) == Status::Success);
	REQUIRE(bb.get<int>("chosen").value_or(0) == 2);
}

TEST_CASE("Builder creates nested sequence and selector", "[ai][bt]")
{
	auto tree = Builder()
		.selector()
			.sequence()
				.condition([](Blackboard& bb) { return bb.get<int>("hp").value_or(0) > 50; })
				.action([](Blackboard& bb) { bb.set<std::string>("state", std::string("attack")); return Status::Success; })
			.end()
			.action([](Blackboard& bb) { bb.set<std::string>("state", std::string("flee")); return Status::Success; })
		.end()
		.build();

	// HP高い → attack
	Blackboard bb;
	bb.set<int>("hp", 100);
	REQUIRE(tree->tick(bb) == Status::Success);
	REQUIRE(bb.get<std::string>("state").value_or("") == "attack");

	// HP低い → flee
	bb.set<int>("hp", 10);
	bb.remove("state");
	REQUIRE(tree->tick(bb) == Status::Success);
	REQUIRE(bb.get<std::string>("state").value_or("") == "flee");
}

TEST_CASE("Builder with inverter decorator", "[ai][bt]")
{
	auto tree = Builder()
		.inverter()
		.action([](Blackboard&) { return Status::Success; })
		.build();

	Blackboard bb;
	REQUIRE(tree->tick(bb) == Status::Failure);
}

TEST_CASE("Builder with repeater decorator", "[ai][bt]")
{
	int count = 0;
	auto tree = Builder()
		.repeater(3)
		.action([&](Blackboard&) { ++count; return Status::Success; })
		.build();

	Blackboard bb;
	REQUIRE(tree->tick(bb) == Status::Success);
	REQUIRE(count == 3);
}

// ── Running propagation ─────────────────────────────────

TEST_CASE("Sequence returns Running when child returns Running", "[ai][bt]")
{
	Blackboard bb;
	int secondCallCount = 0;

	Sequence seq;
	seq.addChild(std::make_unique<Action>([](Blackboard&) { return Status::Running; }));
	seq.addChild(std::make_unique<Action>([&](Blackboard&) { ++secondCallCount; return Status::Success; }));

	REQUIRE(seq.tick(bb) == Status::Running);
	REQUIRE(secondCallCount == 0); // 2番目は呼ばれない
}

TEST_CASE("Selector returns Running when child returns Running", "[ai][bt]")
{
	Blackboard bb;
	int secondCallCount = 0;

	Selector sel;
	sel.addChild(std::make_unique<Action>([](Blackboard&) { return Status::Running; }));
	sel.addChild(std::make_unique<Action>([&](Blackboard&) { ++secondCallCount; return Status::Success; }));

	REQUIRE(sel.tick(bb) == Status::Running);
	REQUIRE(secondCallCount == 0);
}

TEST_CASE("Deep nesting 3 levels works correctly", "[ai][bt]")
{
	auto tree = Builder()
		.sequence()
			.selector()
				.sequence()
					.action([](Blackboard& bb) { bb.set<int>("depth", 3); return Status::Success; })
				.end()
			.end()
		.end()
		.build();

	Blackboard bb;
	REQUIRE(tree->tick(bb) == Status::Success);
	REQUIRE(bb.get<int>("depth").value_or(0) == 3);
}

TEST_CASE("Repeater returns Running when child returns Running", "[ai][bt]")
{
	Blackboard bb;
	int callCount = 0;

	Repeater rep(std::make_unique<Action>([&](Blackboard&)
	{
		++callCount;
		return Status::Running;
	}), 5);

	REQUIRE(rep.tick(bb) == Status::Running);
	REQUIRE(callCount == 1); // Running時点で中断
}

// ── Node name ─────────────────────────────────────────────

TEST_CASE("Node name for debugging", "[ai][bt]")
{
	Action act([](Blackboard&) { return Status::Success; });
	REQUIRE(act.name().empty());

	act.setName("Attack");
	REQUIRE(act.name() == "Attack");
}

// ── Parallel ──────────────────────────────────────────────

TEST_CASE("Parallel RequireAll succeeds when all succeed", "[ai][bt]")
{
	Blackboard bb;
	Parallel par(ParallelPolicy::RequireAll);
	par.addChild(std::make_unique<Action>([](Blackboard&) { return Status::Success; }));
	par.addChild(std::make_unique<Action>([](Blackboard&) { return Status::Success; }));

	REQUIRE(par.tick(bb) == Status::Success);
}

TEST_CASE("Parallel RequireAll fails when any fails", "[ai][bt]")
{
	Blackboard bb;
	Parallel par(ParallelPolicy::RequireAll);
	par.addChild(std::make_unique<Action>([](Blackboard&) { return Status::Success; }));
	par.addChild(std::make_unique<Action>([](Blackboard&) { return Status::Failure; }));

	REQUIRE(par.tick(bb) == Status::Failure);
}

TEST_CASE("Parallel RequireOne succeeds when any succeeds", "[ai][bt]")
{
	Blackboard bb;
	Parallel par(ParallelPolicy::RequireOne);
	par.addChild(std::make_unique<Action>([](Blackboard&) { return Status::Failure; }));
	par.addChild(std::make_unique<Action>([](Blackboard&) { return Status::Success; }));

	REQUIRE(par.tick(bb) == Status::Success);
}

TEST_CASE("Parallel RequireOne fails when all fail", "[ai][bt]")
{
	Blackboard bb;
	Parallel par(ParallelPolicy::RequireOne);
	par.addChild(std::make_unique<Action>([](Blackboard&) { return Status::Failure; }));
	par.addChild(std::make_unique<Action>([](Blackboard&) { return Status::Failure; }));

	REQUIRE(par.tick(bb) == Status::Failure);
}

// ── RandomSelector ────────────────────────────────────────

TEST_CASE("RandomSelector tries all children until success", "[ai][bt]")
{
	Blackboard bb;
	int callCount = 0;

	RandomSelector rsel;
	rsel.addChild(std::make_unique<Action>([&](Blackboard&) { ++callCount; return Status::Failure; }));
	rsel.addChild(std::make_unique<Action>([&](Blackboard&) { ++callCount; return Status::Failure; }));
	rsel.addChild(std::make_unique<Action>([&](Blackboard&) { ++callCount; return Status::Success; }));

	REQUIRE(rsel.tick(bb) == Status::Success);
	// 順序はランダムだが、1つ成功するまで試行する
	REQUIRE(callCount >= 1);
	REQUIRE(callCount <= 3);
}

TEST_CASE("RandomSelector fails when all fail", "[ai][bt]")
{
	Blackboard bb;
	RandomSelector rsel;
	rsel.addChild(std::make_unique<Action>([](Blackboard&) { return Status::Failure; }));
	rsel.addChild(std::make_unique<Action>([](Blackboard&) { return Status::Failure; }));

	REQUIRE(rsel.tick(bb) == Status::Failure);
}

// ── Builder with Parallel ─────────────────────────────────

TEST_CASE("Builder parallel creates Parallel node", "[ai][bt]")
{
	auto tree = Builder()
		.parallel(ParallelPolicy::RequireAll)
			.action([](Blackboard& bb) { bb.set("a", true); return Status::Success; })
			.action([](Blackboard& bb) { bb.set("b", true); return Status::Success; })
		.end()
		.build();

	Blackboard bb;
	REQUIRE(tree->tick(bb) == Status::Success);
	REQUIRE(bb.get<bool>("a").value_or(false));
	REQUIRE(bb.get<bool>("b").value_or(false));
}

TEST_CASE("Builder randomSelector creates RandomSelector node", "[ai][bt]")
{
	int count = 0;
	auto tree = Builder()
		.randomSelector()
			.action([&](Blackboard&) { ++count; return Status::Failure; })
			.action([&](Blackboard&) { ++count; return Status::Success; })
		.end()
		.build();

	Blackboard bb;
	REQUIRE(tree->tick(bb) == Status::Success);
	REQUIRE(count >= 1);
}
