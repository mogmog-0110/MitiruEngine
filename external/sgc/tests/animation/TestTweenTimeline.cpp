/// @file TestTweenTimeline.cpp
/// @brief TweenTimeline.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/animation/TweenTimeline.hpp"

using Catch::Approx;

TEST_CASE("TweenTimeline runs all tweens in parallel", "[animation][timeline]")
{
	float valueA = 0.0f;
	float valueB = 0.0f;

	sgc::TweenTimelinef timeline;
	timeline.add(
		sgc::Tweenf{}.from(0.0f).to(100.0f).during(1.0f)
			.onUpdate([&](float v) { valueA = v; })
	);
	timeline.add(
		sgc::Tweenf{}.from(0.0f).to(200.0f).during(2.0f)
			.onUpdate([&](float v) { valueB = v; })
	);

	// 0.5秒経過: 両方同時に更新される
	timeline.step(0.5f);
	REQUIRE(valueA == Approx(50.0f));
	REQUIRE(valueB == Approx(50.0f));
	REQUIRE_FALSE(timeline.isFinished());
}

TEST_CASE("TweenTimeline finishes when all tweens complete", "[animation][timeline]")
{
	sgc::TweenTimelinef timeline;
	timeline.add(sgc::Tweenf{}.from(0.0f).to(10.0f).during(1.0f));
	timeline.add(sgc::Tweenf{}.from(0.0f).to(20.0f).during(2.0f));

	// 1秒: 最初のTweenは完了、2番目はまだ
	timeline.step(1.0f);
	REQUIRE_FALSE(timeline.isFinished());

	// さらに1秒: 2番目も完了
	timeline.step(1.0f);
	REQUIRE(timeline.isFinished());
}

TEST_CASE("TweenTimeline onComplete callback fires", "[animation][timeline]")
{
	bool completed = false;

	sgc::TweenTimelinef timeline;
	timeline.add(sgc::Tweenf{}.from(0.0f).to(10.0f).during(1.0f));
	timeline.onComplete([&] { completed = true; });

	timeline.step(0.5f);
	REQUIRE_FALSE(completed);

	timeline.step(0.5f);
	REQUIRE(completed);
}

TEST_CASE("TweenTimeline onComplete fires only once", "[animation][timeline]")
{
	int callCount = 0;

	sgc::TweenTimelinef timeline;
	timeline.add(sgc::Tweenf{}.from(0.0f).to(10.0f).during(1.0f));
	timeline.onComplete([&] { ++callCount; });

	timeline.step(1.0f);
	timeline.step(1.0f);  // 既に完了済み
	REQUIRE(callCount == 1);
}

TEST_CASE("TweenTimeline reset works", "[animation][timeline]")
{
	sgc::TweenTimelinef timeline;
	timeline.add(sgc::Tweenf{}.from(0.0f).to(10.0f).during(1.0f));

	timeline.step(1.0f);
	REQUIRE(timeline.isFinished());

	timeline.reset();
	REQUIRE_FALSE(timeline.isFinished());

	// リセット後にもう一度実行可能
	timeline.step(1.0f);
	REQUIRE(timeline.isFinished());
}

TEST_CASE("TweenTimeline size returns tween count", "[animation][timeline]")
{
	sgc::TweenTimelinef timeline;
	REQUIRE(timeline.size() == 0);

	timeline.add(sgc::Tweenf{}.from(0.0f).to(10.0f).during(1.0f));
	REQUIRE(timeline.size() == 1);

	timeline.add(sgc::Tweenf{}.from(0.0f).to(20.0f).during(2.0f));
	REQUIRE(timeline.size() == 2);
}

TEST_CASE("TweenTimeline empty timeline is not finished", "[animation][timeline]")
{
	sgc::TweenTimelinef timeline;
	timeline.step(1.0f);
	REQUIRE_FALSE(timeline.isFinished());
}

TEST_CASE("TweenTimeline method chaining", "[animation][timeline]")
{
	bool completed = false;
	sgc::TweenTimelinef timeline;
	timeline.add(sgc::Tweenf{}.from(0.0f).to(10.0f).during(1.0f))
	        .add(sgc::Tweenf{}.from(0.0f).to(20.0f).during(0.5f))
	        .onComplete([&] { completed = true; });

	REQUIRE(timeline.size() == 2);
	timeline.step(1.0f);
	REQUIRE(completed);
}
