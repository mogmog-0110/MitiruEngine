/// @file TestUtilityAI.cpp
/// @brief UtilityAI.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/ai/UtilityAI.hpp"

#include <string>

struct TestContext
{
	float health = 100.0f;
	float enemyDistance = 10.0f;
	float ammo = 50.0f;
};

TEST_CASE("UtilitySelector with no actions returns nullopt", "[ai][utility]")
{
	const sgc::ai::UtilitySelector<TestContext> selector;
	const TestContext ctx;
	const auto result = selector.evaluate(ctx);
	REQUIRE_FALSE(result.has_value());
}

TEST_CASE("UtilitySelector actionCount reflects added actions", "[ai][utility]")
{
	sgc::ai::UtilitySelector<TestContext> selector;
	REQUIRE(selector.actionCount() == 0);
	selector.addAction("attack", [](const TestContext&) {});
	REQUIRE(selector.actionCount() == 1);
	selector.addAction("defend", [](const TestContext&) {});
	REQUIRE(selector.actionCount() == 2);
}

TEST_CASE("Action with no considerations scores zero", "[ai][utility]")
{
	sgc::ai::UtilitySelector<TestContext> selector;
	selector.addAction("idle", [](const TestContext&) {});

	const TestContext ctx;
	const auto result = selector.evaluate(ctx);
	REQUIRE(result.has_value());
	REQUIRE(result->score == Catch::Approx(0.0f));
	REQUIRE(result->name == "idle");
}

TEST_CASE("Single consideration returns its score", "[ai][utility]")
{
	sgc::ai::UtilitySelector<TestContext> selector;
	selector.addAction("attack", [](const TestContext&) {})
		.addConsideration([](const TestContext& ctx) { return ctx.health / 100.0f; });

	const TestContext ctx{80.0f, 10.0f, 50.0f};
	const auto result = selector.evaluate(ctx);
	REQUIRE(result.has_value());
	REQUIRE(result->score == Catch::Approx(0.8f));
}

TEST_CASE("Weighted considerations compute weighted average", "[ai][utility]")
{
	sgc::ai::UtilitySelector<TestContext> selector;
	// 重み2.0で0.5のスコア、重み1.0で1.0のスコア
	// 加重平均 = (0.5*2.0 + 1.0*1.0) / (2.0+1.0) = 2.0/3.0
	selector.addAction("test", [](const TestContext&) {})
		.addConsideration([](const TestContext&) { return 0.5f; }, 2.0f)
		.addConsideration([](const TestContext&) { return 1.0f; }, 1.0f);

	const TestContext ctx;
	const auto result = selector.evaluate(ctx);
	REQUIRE(result.has_value());
	REQUIRE(result->score == Catch::Approx(2.0f / 3.0f));
}

TEST_CASE("Selector picks highest scoring action", "[ai][utility]")
{
	sgc::ai::UtilitySelector<TestContext> selector;

	selector.addAction("low", [](const TestContext&) {})
		.addConsideration([](const TestContext&) { return 0.2f; });

	selector.addAction("high", [](const TestContext&) {})
		.addConsideration([](const TestContext&) { return 0.9f; });

	selector.addAction("mid", [](const TestContext&) {})
		.addConsideration([](const TestContext&) { return 0.5f; });

	const TestContext ctx;
	const auto result = selector.evaluate(ctx);
	REQUIRE(result.has_value());
	REQUIRE(result->name == "high");
	REQUIRE(result->score == Catch::Approx(0.9f));
}

TEST_CASE("evaluateAll returns sorted scores", "[ai][utility]")
{
	sgc::ai::UtilitySelector<TestContext> selector;

	selector.addAction("low", [](const TestContext&) {})
		.addConsideration([](const TestContext&) { return 0.1f; });

	selector.addAction("high", [](const TestContext&) {})
		.addConsideration([](const TestContext&) { return 0.9f; });

	selector.addAction("mid", [](const TestContext&) {})
		.addConsideration([](const TestContext&) { return 0.5f; });

	const TestContext ctx;
	const auto all = selector.evaluateAll(ctx);
	REQUIRE(all.size() == 3);
	REQUIRE(all[0].name == "high");
	REQUIRE(all[1].name == "mid");
	REQUIRE(all[2].name == "low");
}

TEST_CASE("evaluateAndExecute calls the callback", "[ai][utility]")
{
	sgc::ai::UtilitySelector<TestContext> selector;
	bool executed = false;

	selector.addAction("doIt", [&executed](const TestContext&) { executed = true; })
		.addConsideration([](const TestContext&) { return 1.0f; });

	const TestContext ctx;
	const auto result = selector.evaluateAndExecute(ctx);
	REQUIRE(result.has_value());
	REQUIRE(result->name == "doIt");
	REQUIRE(executed);
}

TEST_CASE("Consideration score is clamped to 0-1", "[ai][utility]")
{
	sgc::ai::UtilitySelector<TestContext> selector;

	// スコアが範囲外の場合もクランプされる
	selector.addAction("clamped", [](const TestContext&) {})
		.addConsideration([](const TestContext&) { return 2.5f; })
		.addConsideration([](const TestContext&) { return -1.0f; });

	const TestContext ctx;
	const auto result = selector.evaluate(ctx);
	REQUIRE(result.has_value());
	// (clamp(2.5,0,1)*1 + clamp(-1,0,1)*1) / 2 = (1.0 + 0.0) / 2 = 0.5
	REQUIRE(result->score == Catch::Approx(0.5f));
}

TEST_CASE("Context-dependent scoring selects dynamically", "[ai][utility]")
{
	sgc::ai::UtilitySelector<TestContext> selector;

	selector.addAction("attack", [](const TestContext&) {})
		.addConsideration([](const TestContext& ctx) { return ctx.health / 100.0f; })
		.addConsideration([](const TestContext& ctx) { return 1.0f - ctx.enemyDistance / 20.0f; });

	selector.addAction("flee", [](const TestContext&) {})
		.addConsideration([](const TestContext& ctx) { return 1.0f - ctx.health / 100.0f; });

	// 体力高い・敵近い → attack
	{
		const TestContext ctx{90.0f, 2.0f, 50.0f};
		const auto result = selector.evaluate(ctx);
		REQUIRE(result->name == "attack");
	}

	// 体力低い → flee
	{
		const TestContext ctx{10.0f, 15.0f, 50.0f};
		const auto result = selector.evaluate(ctx);
		REQUIRE(result->name == "flee");
	}
}
