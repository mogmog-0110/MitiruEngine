#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/ai/SteeringBehaviors.hpp"

using namespace sgc;
using namespace sgc::ai;
using Approx = Catch::Approx;

TEST_CASE("seek returns force toward target", "[ai][steering]")
{
	SteeringAgent<float> agent;
	agent.position = {0.0f, 0.0f};
	agent.velocity = {0.0f, 0.0f};
	agent.maxSpeed = 100.0f;
	agent.maxForce = 50.0f;

	const Vec2f target{100.0f, 0.0f};
	const auto force = seek(agent, target);

	// 力はターゲット方向（正のX方向）を向くべき
	REQUIRE(force.x > 0.0f);
	REQUIRE(force.y == Approx(0.0f).margin(0.01f));
}

TEST_CASE("flee returns force away from threat", "[ai][steering]")
{
	SteeringAgent<float> agent;
	agent.position = {0.0f, 0.0f};
	agent.velocity = {0.0f, 0.0f};
	agent.maxSpeed = 100.0f;
	agent.maxForce = 50.0f;

	const Vec2f threat{100.0f, 0.0f};
	const auto force = flee(agent, threat);

	// 力は脅威と反対方向（負のX方向）を向くべき
	REQUIRE(force.x < 0.0f);
	REQUIRE(force.y == Approx(0.0f).margin(0.01f));
}

TEST_CASE("arrive slows near target", "[ai][steering]")
{
	SteeringAgent<float> agent;
	agent.position = {0.0f, 0.0f};
	agent.velocity = {50.0f, 0.0f};
	agent.maxSpeed = 100.0f;
	agent.maxForce = 200.0f;

	// ターゲットが減速半径内にある場合
	const Vec2f target{10.0f, 0.0f};
	const auto force = arrive(agent, target, 100.0f);

	// 減速半径内では、seekよりも弱い力になるべき
	const auto seekForce = seek(agent, target);
	// arrive は目標に近いため desired speed が小さい → 力もseekと異なる
	REQUIRE(force.lengthSquared() > 0.0f);
}

TEST_CASE("arrive returns braking force at target", "[ai][steering]")
{
	SteeringAgent<float> agent;
	agent.position = {50.0f, 50.0f};
	agent.velocity = {0.0f, 0.0f};
	agent.maxSpeed = 100.0f;
	agent.maxForce = 50.0f;

	// エージェントがターゲット位置にいる場合
	const Vec2f target{50.0f, 50.0f};
	const auto force = arrive(agent, target, 100.0f);

	// 速度0＋位置一致→力はほぼゼロ
	REQUIRE(force.length() == Approx(0.0f).margin(0.1f));
}

TEST_CASE("pursue leads target", "[ai][steering]")
{
	SteeringAgent<float> agent;
	agent.position = {0.0f, 0.0f};
	agent.velocity = {10.0f, 0.0f};
	agent.maxSpeed = 100.0f;
	agent.maxForce = 50.0f;

	SteeringAgent<float> target;
	target.position = {100.0f, 0.0f};
	target.velocity = {0.0f, 50.0f};  // 上方向に移動中

	const auto force = pursue(agent, target);

	// 予測位置を追うので、Y方向成分もあるべき
	REQUIRE(force.y > 0.0f);
}

TEST_CASE("evade moves away from predicted position", "[ai][steering]")
{
	SteeringAgent<float> agent;
	agent.position = {0.0f, 0.0f};
	agent.velocity = {0.0f, 0.0f};
	agent.maxSpeed = 100.0f;
	agent.maxForce = 50.0f;

	SteeringAgent<float> threat;
	threat.position = {50.0f, 0.0f};
	threat.velocity = {-10.0f, 0.0f};  // こちらに向かっている

	const auto force = evade(agent, threat);

	// 予測位置から離れるので、負のX方向成分を持つべき
	REQUIRE(force.x < 0.0f);
}

TEST_CASE("applyForce updates position and velocity", "[ai][steering]")
{
	SteeringAgent<float> agent;
	agent.position = {0.0f, 0.0f};
	agent.velocity = {0.0f, 0.0f};
	agent.maxSpeed = 100.0f;
	agent.maxForce = 50.0f;
	agent.mass = 1.0f;

	const Vec2f force{10.0f, 0.0f};
	const auto updated = applyForce(agent, force, 1.0f);

	// 加速度 = force / mass = {10, 0}
	// 新速度 = {0, 0} + {10, 0} * 1.0 = {10, 0}
	// 新位置 = {0, 0} + {10, 0} * 1.0 = {10, 0}
	REQUIRE(updated.velocity.x == Approx(10.0f));
	REQUIRE(updated.velocity.y == Approx(0.0f));
	REQUIRE(updated.position.x == Approx(10.0f));
	REQUIRE(updated.position.y == Approx(0.0f));
}

TEST_CASE("truncate limits vector magnitude", "[ai][steering]")
{
	const Vec2f v{3.0f, 4.0f};  // 長さ5
	const auto truncated = truncate(v, 2.5f);

	REQUIRE(truncated.length() == Approx(2.5f).margin(0.001f));

	// 方向は維持される
	const auto norm = v.normalized();
	const auto truncNorm = truncated.normalized();
	REQUIRE(truncNorm.x == Approx(norm.x).margin(0.001f));
	REQUIRE(truncNorm.y == Approx(norm.y).margin(0.001f));
}

TEST_CASE("truncate does not modify short vectors", "[ai][steering]")
{
	const Vec2f v{1.0f, 0.0f};  // 長さ1
	const auto result = truncate(v, 5.0f);

	REQUIRE(result.x == Approx(1.0f));
	REQUIRE(result.y == Approx(0.0f));
}
