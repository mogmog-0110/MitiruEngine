#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <span>

#include "sgc/ai/Flocking.hpp"

using namespace sgc;
using namespace sgc::ai;
using Approx = Catch::Approx;

TEST_CASE("separation pushes away from close neighbors", "[ai][flocking]")
{
	SteeringAgent<float> agent;
	agent.position = {50.0f, 50.0f};
	agent.velocity = {0.0f, 0.0f};
	agent.maxSpeed = 100.0f;
	agent.maxForce = 50.0f;

	std::array<SteeringAgent<float>, 2> neighbors;
	neighbors[0].position = {55.0f, 50.0f};  // 右隣
	neighbors[0].velocity = {0.0f, 0.0f};
	neighbors[0].maxSpeed = 100.0f;
	neighbors[1].position = {45.0f, 50.0f};  // 左隣
	neighbors[1].velocity = {0.0f, 0.0f};
	neighbors[1].maxSpeed = 100.0f;

	// 左右対称なので分離力はほぼゼロになるはず
	// 片側だけテスト
	std::array<SteeringAgent<float>, 1> rightOnly;
	rightOnly[0] = neighbors[0];

	const auto force = separation(agent, std::span<const SteeringAgent<float>>{rightOnly}, 20.0f);

	// 右隣から離れるので負のX方向
	REQUIRE(force.x < 0.0f);
}

TEST_CASE("separation returns zero with no neighbors", "[ai][flocking]")
{
	SteeringAgent<float> agent;
	agent.position = {50.0f, 50.0f};
	agent.velocity = {0.0f, 0.0f};
	agent.maxSpeed = 100.0f;
	agent.maxForce = 50.0f;

	const auto force = separation(agent, std::span<const SteeringAgent<float>>{}, 20.0f);

	REQUIRE(force.x == Approx(0.0f));
	REQUIRE(force.y == Approx(0.0f));
}

TEST_CASE("alignment matches neighbor velocity", "[ai][flocking]")
{
	SteeringAgent<float> agent;
	agent.position = {0.0f, 0.0f};
	agent.velocity = {0.0f, 0.0f};
	agent.maxSpeed = 100.0f;
	agent.maxForce = 50.0f;

	std::array<SteeringAgent<float>, 2> neighbors;
	neighbors[0].position = {10.0f, 0.0f};
	neighbors[0].velocity = {50.0f, 0.0f};  // 右向きに移動
	neighbors[0].maxSpeed = 100.0f;
	neighbors[1].position = {0.0f, 10.0f};
	neighbors[1].velocity = {50.0f, 0.0f};  // 右向きに移動
	neighbors[1].maxSpeed = 100.0f;

	const auto force = alignment(agent, std::span<const SteeringAgent<float>>{neighbors});

	// 近隣が右向きなので、右向きの力
	REQUIRE(force.x > 0.0f);
}

TEST_CASE("cohesion moves toward neighbor center", "[ai][flocking]")
{
	SteeringAgent<float> agent;
	agent.position = {0.0f, 0.0f};
	agent.velocity = {0.0f, 0.0f};
	agent.maxSpeed = 100.0f;
	agent.maxForce = 50.0f;

	std::array<SteeringAgent<float>, 2> neighbors;
	neighbors[0].position = {100.0f, 0.0f};
	neighbors[0].velocity = {0.0f, 0.0f};
	neighbors[0].maxSpeed = 100.0f;
	neighbors[1].position = {100.0f, 100.0f};
	neighbors[1].velocity = {0.0f, 0.0f};
	neighbors[1].maxSpeed = 100.0f;

	const auto force = cohesion(agent, std::span<const SteeringAgent<float>>{neighbors});

	// 重心は{100, 50}なので、正のX方向と正のY方向の力
	REQUIRE(force.x > 0.0f);
	REQUIRE(force.y > 0.0f);
}

TEST_CASE("flock combines all three behaviors", "[ai][flocking]")
{
	SteeringAgent<float> agent;
	agent.position = {50.0f, 50.0f};
	agent.velocity = {10.0f, 0.0f};
	agent.maxSpeed = 100.0f;
	agent.maxForce = 50.0f;

	std::array<SteeringAgent<float>, 3> boids;
	boids[0].position = {60.0f, 50.0f};
	boids[0].velocity = {10.0f, 5.0f};
	boids[0].maxSpeed = 100.0f;
	boids[0].maxForce = 50.0f;
	boids[1].position = {55.0f, 60.0f};
	boids[1].velocity = {10.0f, -5.0f};
	boids[1].maxSpeed = 100.0f;
	boids[1].maxForce = 50.0f;
	boids[2].position = {45.0f, 45.0f};
	boids[2].velocity = {15.0f, 0.0f};
	boids[2].maxSpeed = 100.0f;
	boids[2].maxForce = 50.0f;

	const auto force = flock(agent, std::span<const SteeringAgent<float>>{boids}, 100.0f);

	// 3つの行動を統合した力が返されるべき（ゼロではない）
	REQUIRE(force.lengthSquared() > 0.0f);
}

TEST_CASE("flock with single neighbor", "[ai][flocking]")
{
	SteeringAgent<float> agent;
	agent.position = {0.0f, 0.0f};
	agent.velocity = {0.0f, 0.0f};
	agent.maxSpeed = 100.0f;
	agent.maxForce = 50.0f;

	std::array<SteeringAgent<float>, 1> neighbors;
	neighbors[0].position = {30.0f, 0.0f};
	neighbors[0].velocity = {10.0f, 0.0f};
	neighbors[0].maxSpeed = 100.0f;
	neighbors[0].maxForce = 50.0f;

	const auto force = flock(agent, std::span<const SteeringAgent<float>>{neighbors}, 50.0f);

	// 1体の近隣に対しても動作するべき
	REQUIRE(force.lengthSquared() > 0.0f);
}

TEST_CASE("flock respects neighbor radius", "[ai][flocking]")
{
	SteeringAgent<float> agent;
	agent.position = {0.0f, 0.0f};
	agent.velocity = {0.0f, 0.0f};
	agent.maxSpeed = 100.0f;
	agent.maxForce = 50.0f;

	std::array<SteeringAgent<float>, 1> neighbors;
	neighbors[0].position = {200.0f, 0.0f};  // 遠すぎる
	neighbors[0].velocity = {10.0f, 0.0f};
	neighbors[0].maxSpeed = 100.0f;
	neighbors[0].maxForce = 50.0f;

	const auto force = flock(agent, std::span<const SteeringAgent<float>>{neighbors}, 50.0f);

	// 半径外のエージェントは無視されるべき
	REQUIRE(force.x == Approx(0.0f));
	REQUIRE(force.y == Approx(0.0f));
}
