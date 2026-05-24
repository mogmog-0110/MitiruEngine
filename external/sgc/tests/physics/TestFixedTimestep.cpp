#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/physics/FixedTimestep.hpp"

TEST_CASE("FixedTimestep - basic step execution", "[physics][FixedTimestep]")
{
	sgc::physics::FixedTimestep stepper{0.01};  // 100Hz
	int callCount = 0;

	stepper.update(0.025, [&](double step)
	{
		REQUIRE_THAT(step, Catch::Matchers::WithinAbs(0.01, 1e-10));
		++callCount;
	});

	REQUIRE(callCount == 2);
}

TEST_CASE("FixedTimestep - accumulation across frames", "[physics][FixedTimestep]")
{
	sgc::physics::FixedTimestep stepper{0.01};
	int total = 0;

	stepper.update(0.005, [&](double) { ++total; });
	REQUIRE(total == 0);

	stepper.update(0.005, [&](double) { ++total; });
	REQUIRE(total == 1);

	stepper.update(0.025, [&](double) { ++total; });
	REQUIRE(total == 3);
}

TEST_CASE("FixedTimestep - returns step count", "[physics][FixedTimestep]")
{
	sgc::physics::FixedTimestep stepper{0.01};

	REQUIRE(stepper.update(0.005, [](double) {}) == 0);
	REQUIRE(stepper.update(0.015, [](double) {}) == 2);
}

TEST_CASE("FixedTimestep - resetAccumulator clears state", "[physics][FixedTimestep]")
{
	sgc::physics::FixedTimestep stepper{0.01};

	stepper.update(0.005, [](double) {});
	REQUIRE_THAT(stepper.accumulated(), Catch::Matchers::WithinAbs(0.005, 1e-10));

	stepper.resetAccumulator();
	REQUIRE_THAT(stepper.accumulated(), Catch::Matchers::WithinAbs(0.0, 1e-10));
}

TEST_CASE("FixedTimestep - interpolationFactor", "[physics][FixedTimestep]")
{
	sgc::physics::FixedTimestep stepper{0.01};

	stepper.update(0.005, [](double) {});
	REQUIRE_THAT(stepper.interpolationFactor(), Catch::Matchers::WithinAbs(0.5, 1e-10));

	stepper.update(0.003, [](double) {});
	REQUIRE_THAT(stepper.interpolationFactor(), Catch::Matchers::WithinAbs(0.8, 1e-10));
}

TEST_CASE("FixedTimestep - stepSize getter and setter", "[physics][FixedTimestep]")
{
	sgc::physics::FixedTimestep stepper{0.01};
	REQUIRE_THAT(stepper.stepSize(), Catch::Matchers::WithinAbs(0.01, 1e-10));

	stepper.setStepSize(0.02);
	REQUIRE_THAT(stepper.stepSize(), Catch::Matchers::WithinAbs(0.02, 1e-10));
}

TEST_CASE("FixedTimestep - zero dt does nothing", "[physics][FixedTimestep]")
{
	sgc::physics::FixedTimestep stepper{0.01};
	REQUIRE(stepper.update(0.0, [](double) {}) == 0);
}

TEST_CASE("FixedTimestep - large dt clamped by maxSteps", "[physics][FixedTimestep]")
{
	sgc::physics::FixedTimestep stepper{0.01};
	// デフォルトmaxSteps=10なので、1.0秒分あっても10ステップまで
	int count = stepper.update(1.0, [](double) {});
	REQUIRE(count == 10);
	// スパイラル・オブ・デス防止: 蓄積時間がリセットされる
	REQUIRE_THAT(stepper.accumulated(), Catch::Matchers::WithinAbs(0.0, 1e-10));
}

TEST_CASE("FixedTimestep - large dt with high maxSteps", "[physics][FixedTimestep]")
{
	sgc::physics::FixedTimestep stepper{0.01};
	stepper.setMaxSteps(200);
	// 浮動小数点の蓄積誤差で99または100になりうる
	int count = stepper.update(1.0, [](double) {});
	REQUIRE(count >= 99);
	REQUIRE(count <= 100);
}

TEST_CASE("FixedTimestep - maxSteps getter and setter", "[physics][FixedTimestep]")
{
	sgc::physics::FixedTimestep stepper{0.01};
	REQUIRE(stepper.maxSteps() == 10);

	stepper.setMaxSteps(5);
	REQUIRE(stepper.maxSteps() == 5);

	int count = stepper.update(0.1, [](double) {});
	REQUIRE(count == 5);
}

TEST_CASE("FixedTimestep - spiral of death resets accumulator", "[physics][FixedTimestep]")
{
	sgc::physics::FixedTimestep stepper{0.01};
	stepper.setMaxSteps(3);

	stepper.update(0.1, [](double) {});
	// maxStepsに到達したので蓄積時間がリセットされる
	REQUIRE_THAT(stepper.accumulated(), Catch::Matchers::WithinAbs(0.0, 1e-10));

	// 次のフレームは正常に動作する
	int count = stepper.update(0.025, [](double) {});
	REQUIRE(count == 2);
}
