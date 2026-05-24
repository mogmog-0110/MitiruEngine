/// @file TestTween.cpp
/// @brief Tween.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/animation/Tween.hpp"
#include "sgc/math/Easing.hpp"

using Catch::Approx;

TEST_CASE("Tween basic from-to-during", "[animation][tween]")
{
	sgc::Tweenf tween;
	tween.from(0.0f).to(100.0f).during(1.0f);

	REQUIRE(tween.step(0.5f) == Approx(50.0f));
	REQUIRE(tween.step(0.5f) == Approx(100.0f));
	REQUIRE(tween.isFinished());
}

TEST_CASE("Tween with easing function", "[animation][tween]")
{
	sgc::Tweenf tween;
	tween.from(0.0f).to(100.0f).during(1.0f)
		.withEasing(sgc::easing::inQuad<float>);

	const float val = tween.step(0.5f);
	// inQuad: t^2, so at t=0.5 -> 0.25 * 100 = 25
	REQUIRE(val == Approx(25.0f));
}

TEST_CASE("Tween seek sets progress directly", "[animation][tween]")
{
	sgc::Tweenf tween;
	tween.from(0.0f).to(100.0f).during(1.0f);

	tween.seek(0.75f);
	REQUIRE(tween.progress() == Approx(0.75f));
	// step(0) just returns current value
	REQUIRE(tween.step(0.0f) == Approx(75.0f));
}

TEST_CASE("Tween pause and resume", "[animation][tween]")
{
	sgc::Tweenf tween;
	tween.from(0.0f).to(100.0f).during(1.0f);

	tween.step(0.25f);
	tween.pause();
	const float val1 = tween.step(0.5f);  // should not advance
	REQUIRE(val1 == Approx(25.0f));

	tween.resume();
	const float val2 = tween.step(0.25f);
	REQUIRE(val2 == Approx(50.0f));
}

TEST_CASE("Tween reset restores initial state", "[animation][tween]")
{
	sgc::Tweenf tween;
	tween.from(0.0f).to(100.0f).during(1.0f);

	tween.step(1.0f);
	REQUIRE(tween.isFinished());

	tween.reset();
	REQUIRE_FALSE(tween.isFinished());
	REQUIRE(tween.progress() == Approx(0.0f));
}

TEST_CASE("Tween loop count", "[animation][tween]")
{
	sgc::Tweenf tween;
	tween.from(0.0f).to(100.0f).during(1.0f).setLoopCount(2);

	tween.step(1.0f);  // 完了 → 残りループ1
	REQUIRE_FALSE(tween.isFinished());

	tween.step(1.0f);  // 完了 → 残りループ0
	REQUIRE_FALSE(tween.isFinished());

	tween.step(1.0f);  // 完了 → 本当に完了
	REQUIRE(tween.isFinished());
}

TEST_CASE("Tween yoyo mode", "[animation][tween]")
{
	sgc::Tweenf tween;
	tween.from(0.0f).to(100.0f).during(1.0f).setLoopCount(1).setYoyo(true);

	const float atEnd = tween.step(1.0f);
	REQUIRE(atEnd == Approx(100.0f).margin(1.0f));

	// 次のサイクルはリバース
	const float atMidReverse = tween.step(0.5f);
	REQUIRE(atMidReverse == Approx(50.0f).margin(5.0f));
}

TEST_CASE("Tween onUpdate callback is called", "[animation][tween]")
{
	int callCount = 0;
	sgc::Tweenf tween;
	tween.from(0.0f).to(100.0f).during(1.0f)
		.onUpdate([&callCount](float) { ++callCount; });

	tween.step(0.5f);
	tween.step(0.5f);
	REQUIRE(callCount == 2);
}

TEST_CASE("Tween onComplete callback is called", "[animation][tween]")
{
	bool completed = false;
	sgc::Tweenf tween;
	tween.from(0.0f).to(100.0f).during(1.0f)
		.onComplete([&completed]() { completed = true; });

	tween.step(0.5f);
	REQUIRE_FALSE(completed);

	tween.step(0.5f);
	REQUIRE(completed);
}

TEST_CASE("TweenSequence runs tweens in order", "[animation][tween]")
{
	sgc::TweenSequence<float> seq;

	sgc::Tweenf t1;
	t1.from(0.0f).to(50.0f).during(1.0f);

	sgc::Tweenf t2;
	t2.from(50.0f).to(100.0f).during(1.0f);

	seq.add(std::move(t1)).add(std::move(t2));

	// 最初のトゥイーン
	REQUIRE(seq.step(0.5f) == Approx(25.0f));
	seq.step(0.5f);  // 最初のトゥイーン完了

	// 2番目のトゥイーン
	const float val = seq.step(0.5f);
	REQUIRE(val == Approx(75.0f));

	seq.step(0.5f);
	REQUIRE(seq.isFinished());
}

TEST_CASE("Tween is not finished while running", "[animation][tween]")
{
	sgc::Tweenf tween;
	tween.from(0.0f).to(100.0f).during(1.0f);

	tween.step(0.3f);
	REQUIRE_FALSE(tween.isFinished());
	REQUIRE(tween.progress() == Approx(0.3f));
}

TEST_CASE("Tween infinite loop never finishes", "[animation][tween]")
{
	sgc::Tweenf tween;
	tween.from(0.0f).to(100.0f).during(1.0f).setLoopCount(-1);

	// 10回分ステップしても終わらない
	for (int i = 0; i < 10; ++i)
		tween.step(1.0f);

	REQUIRE_FALSE(tween.isFinished());
}

TEST_CASE("Tween yoyo reverses direction", "[animation][tween]")
{
	sgc::Tweenf tween;
	tween.from(0.0f).to(100.0f).during(1.0f).setLoopCount(1).setYoyo(true);

	tween.step(1.0f); // 前進完了
	// リバース半分
	const float val = tween.step(0.5f);
	REQUIRE(val == Approx(50.0f).margin(5.0f));
}

TEST_CASE("Tween zero duration returns start value", "[animation][tween]")
{
	sgc::Tweenf tween;
	tween.from(10.0f).to(90.0f).during(0.0f);

	const float val = tween.step(1.0f);
	// during(0)ではduration<=0ガードで現在値を返す
	REQUIRE(tween.progress() == Approx(1.0f));
}
