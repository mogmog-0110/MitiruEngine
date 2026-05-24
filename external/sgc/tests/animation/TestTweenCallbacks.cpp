/// @file TestTweenCallbacks.cpp
/// @brief TweenCallbacks.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/animation/TweenCallbacks.hpp"

using Catch::Approx;

TEST_CASE("CallbackTween onStart fires on first update", "[animation][callback]")
{
	bool started = false;
	sgc::CallbackTweenf ct{sgc::Tweenf{}.from(0.0f).to(100.0f).during(1.0f)};
	ct.onStart([&started]{ started = true; });

	REQUIRE_FALSE(ct.isStarted());
	ct.update(0.1f);
	REQUIRE(ct.isStarted());
	REQUIRE(started);
}

TEST_CASE("CallbackTween onComplete fires when finished", "[animation][callback]")
{
	bool completed = false;
	sgc::CallbackTweenf ct{sgc::Tweenf{}.from(0.0f).to(100.0f).during(1.0f)};
	ct.onComplete([&completed]{ completed = true; });

	ct.update(0.5f);
	REQUIRE_FALSE(completed);

	ct.update(0.5f);
	REQUIRE(completed);
	REQUIRE(ct.isComplete());
}

TEST_CASE("CallbackTween onUpdate fires every step", "[animation][callback]")
{
	int updateCount = 0;
	float lastValue = 0.0f;

	sgc::CallbackTweenf ct{sgc::Tweenf{}.from(0.0f).to(100.0f).during(1.0f)};
	ct.onUpdate([&](const float& v)
	{
		++updateCount;
		lastValue = v;
	});

	ct.update(0.25f);
	REQUIRE(updateCount == 1);
	REQUIRE(lastValue == Approx(25.0f));

	ct.update(0.25f);
	REQUIRE(updateCount == 2);
	REQUIRE(lastValue == Approx(50.0f));
}

TEST_CASE("CallbackTween value returns current tween value", "[animation][callback]")
{
	sgc::CallbackTweenf ct{sgc::Tweenf{}.from(10.0f).to(20.0f).during(1.0f)};

	ct.update(0.5f);
	REQUIRE(ct.value() == Approx(15.0f));

	ct.update(0.5f);
	REQUIRE(ct.value() == Approx(20.0f));
}

TEST_CASE("CallbackTween reset clears all state", "[animation][callback]")
{
	bool started = false;
	bool completed = false;

	sgc::CallbackTweenf ct{sgc::Tweenf{}.from(0.0f).to(100.0f).during(1.0f)};
	ct.onStart([&started]{ started = true; });
	ct.onComplete([&completed]{ completed = true; });

	ct.update(1.0f);
	REQUIRE(ct.isStarted());
	REQUIRE(ct.isComplete());

	ct.reset();
	REQUIRE_FALSE(ct.isStarted());
	REQUIRE_FALSE(ct.isComplete());
	REQUIRE(ct.value() == Approx(0.0f));

	/// リセット後に再利用可能
	started = false;
	completed = false;
	ct.update(0.5f);
	REQUIRE(started);
	REQUIRE_FALSE(completed);
}

TEST_CASE("CallbackTween update returns true when complete", "[animation][callback]")
{
	sgc::CallbackTweenf ct{sgc::Tweenf{}.from(0.0f).to(100.0f).during(1.0f)};

	REQUIRE_FALSE(ct.update(0.5f));
	REQUIRE(ct.update(0.5f));

	/// 完了後もtrueを返し続ける
	REQUIRE(ct.update(0.1f));
}

TEST_CASE("CallbackTween onStart fires only once", "[animation][callback]")
{
	int startCount = 0;
	sgc::CallbackTweenf ct{sgc::Tweenf{}.from(0.0f).to(100.0f).during(1.0f)};
	ct.onStart([&startCount]{ ++startCount; });

	ct.update(0.1f);
	ct.update(0.1f);
	ct.update(0.1f);
	REQUIRE(startCount == 1);
}
