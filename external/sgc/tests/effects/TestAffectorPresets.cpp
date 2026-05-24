#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

#include "sgc/effects/AffectorPresets.hpp"

using Catch::Matchers::WithinAbs;

TEST_CASE("makeGravity applies gravity to velocity", "[effects]")
{
	auto affector = sgc::makeGravity(0.0f, 98.0f);
	sgc::Particle p;
	p.vx = 10.0f;
	p.vy = 0.0f;

	affector(p, 1.0f);

	REQUIRE_THAT(p.vx, WithinAbs(10.0f, 1e-5f));
	REQUIRE_THAT(p.vy, WithinAbs(98.0f, 1e-5f));
}

TEST_CASE("makeGravity accumulates over frames", "[effects]")
{
	auto affector = sgc::makeGravity(0.0f, 10.0f);
	sgc::Particle p;
	p.vy = 0.0f;

	affector(p, 0.1f);
	REQUIRE_THAT(p.vy, WithinAbs(1.0f, 1e-5f));

	affector(p, 0.1f);
	REQUIRE_THAT(p.vy, WithinAbs(2.0f, 1e-5f));
}

TEST_CASE("makeDrag reduces velocity", "[effects]")
{
	auto affector = sgc::makeDrag(2.0f);
	sgc::Particle p;
	p.vx = 100.0f;
	p.vy = 50.0f;

	affector(p, 0.1f);

	REQUIRE(p.vx < 100.0f);
	REQUIRE(p.vy < 50.0f);
	REQUIRE(p.vx > 0.0f);
	REQUIRE(p.vy > 0.0f);
}

TEST_CASE("makeDrag does not produce negative velocity factor", "[effects]")
{
	auto affector = sgc::makeDrag(100.0f);
	sgc::Particle p;
	p.vx = 50.0f;
	p.vy = 30.0f;

	affector(p, 1.0f);

	// 大きな減衰係数でも速度は0以上
	REQUIRE_THAT(p.vx, WithinAbs(0.0f, 1e-5f));
	REQUIRE_THAT(p.vy, WithinAbs(0.0f, 1e-5f));
}

TEST_CASE("makeVortex applies tangential force", "[effects]")
{
	auto affector = sgc::makeVortex(0.0f, 0.0f, 100.0f);
	sgc::Particle p;
	p.x = 10.0f;
	p.y = 0.0f;
	p.vx = 0.0f;
	p.vy = 0.0f;

	affector(p, 1.0f);

	// パーティクルが中心の右にある場合、反時計回りの力は上向き（-Y方向）
	REQUIRE_THAT(p.vx, WithinAbs(0.0f, 1e-3f));
	REQUIRE(p.vy != 0.0f);
}

TEST_CASE("makeVortex ignores particle at center", "[effects]")
{
	auto affector = sgc::makeVortex(0.0f, 0.0f, 100.0f);
	sgc::Particle p;
	p.x = 0.0f;
	p.y = 0.0f;
	p.vx = 5.0f;
	p.vy = 3.0f;

	affector(p, 1.0f);

	// 中心にあるパーティクルは影響を受けない
	REQUIRE_THAT(p.vx, WithinAbs(5.0f, 1e-5f));
	REQUIRE_THAT(p.vy, WithinAbs(3.0f, 1e-5f));
}

TEST_CASE("makeColorOverLife interpolates from start to end", "[effects]")
{
	auto affector = sgc::makeColorOverLife(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
	sgc::Particle p;
	p.lifetime = 1.0f;
	p.maxLifetime = 2.0f;

	affector(p, 0.0f);

	// t = 1 - (1/2) = 0.5
	REQUIRE_THAT(p.r, WithinAbs(0.5f, 1e-5f));
	REQUIRE_THAT(p.g, WithinAbs(0.5f, 1e-5f));
	REQUIRE_THAT(p.b, WithinAbs(0.0f, 1e-5f));
}

TEST_CASE("makeColorOverLife at start of life", "[effects]")
{
	auto affector = sgc::makeColorOverLife(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
	sgc::Particle p;
	p.lifetime = 2.0f;
	p.maxLifetime = 2.0f;

	affector(p, 0.0f);

	// t = 1 - (2/2) = 0, 開始色のまま
	REQUIRE_THAT(p.r, WithinAbs(1.0f, 1e-5f));
	REQUIRE_THAT(p.g, WithinAbs(0.0f, 1e-5f));
	REQUIRE_THAT(p.b, WithinAbs(0.0f, 1e-5f));
}

TEST_CASE("makeTurbulence modifies velocity", "[effects]")
{
	auto affector = sgc::makeTurbulence(50.0f, 1.0f);
	sgc::Particle p;
	p.x = 5.0f;
	p.y = 3.0f;
	p.vx = 0.0f;
	p.vy = 0.0f;

	affector(p, 1.0f);

	// 乱流によって速度が変化しているはず
	REQUIRE((p.vx != 0.0f || p.vy != 0.0f));
}

TEST_CASE("makeTurbulence strength scales effect", "[effects]")
{
	sgc::Particle p1;
	p1.x = 1.0f;
	p1.y = 1.0f;
	p1.vx = 0.0f;
	p1.vy = 0.0f;

	sgc::Particle p2 = p1;

	sgc::makeTurbulence(10.0f, 1.0f)(p1, 1.0f);
	sgc::makeTurbulence(100.0f, 1.0f)(p2, 1.0f);

	const float mag1 = std::sqrt(p1.vx * p1.vx + p1.vy * p1.vy);
	const float mag2 = std::sqrt(p2.vx * p2.vx + p2.vy * p2.vy);

	// 強い乱流はより大きな変化を生むはず
	REQUIRE(mag2 > mag1);
}
