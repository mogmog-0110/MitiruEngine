#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <random>

#include "sgc/effects/EmitterShape.hpp"

using Catch::Matchers::WithinAbs;

TEST_CASE("PointEmitter spawns at origin by default", "[effects]")
{
	sgc::PointEmitter emitter;
	std::mt19937 rng(42);

	const auto result = emitter.spawn(rng);
	REQUIRE_THAT(result.posX, WithinAbs(0.0f, 1e-5f));
	REQUIRE_THAT(result.posY, WithinAbs(0.0f, 1e-5f));
}

TEST_CASE("PointEmitter spawns at specified position", "[effects]")
{
	sgc::PointEmitter emitter(10.0f, 20.0f);
	std::mt19937 rng(42);

	const auto result = emitter.spawn(rng);
	REQUIRE_THAT(result.posX, WithinAbs(10.0f, 1e-5f));
	REQUIRE_THAT(result.posY, WithinAbs(20.0f, 1e-5f));
}

TEST_CASE("PointEmitter direction is normalized", "[effects]")
{
	sgc::PointEmitter emitter;
	std::mt19937 rng(42);

	for (int i = 0; i < 100; ++i)
	{
		const auto result = emitter.spawn(rng);
		const float len = std::sqrt(result.dirX * result.dirX + result.dirY * result.dirY);
		REQUIRE_THAT(len, WithinAbs(1.0f, 1e-5f));
	}
}

TEST_CASE("CircleEmitter edge mode produces points on circle", "[effects]")
{
	const float radius = 50.0f;
	sgc::CircleEmitter emitter(radius, false, 0.0f, 0.0f);
	std::mt19937 rng(42);

	for (int i = 0; i < 100; ++i)
	{
		const auto result = emitter.spawn(rng);
		const float dist = std::sqrt(result.posX * result.posX + result.posY * result.posY);
		REQUIRE_THAT(dist, WithinAbs(radius, 1e-3f));
	}
}

TEST_CASE("CircleEmitter filled mode produces points within radius", "[effects]")
{
	const float radius = 50.0f;
	sgc::CircleEmitter emitter(radius, true, 0.0f, 0.0f);
	std::mt19937 rng(42);

	for (int i = 0; i < 200; ++i)
	{
		const auto result = emitter.spawn(rng);
		const float dist = std::sqrt(result.posX * result.posX + result.posY * result.posY);
		REQUIRE(dist <= radius + 1e-3f);
	}
}

TEST_CASE("CircleEmitter accessors", "[effects]")
{
	sgc::CircleEmitter emitter(30.0f, true);
	REQUIRE_THAT(emitter.radius(), WithinAbs(30.0f, 1e-5f));
	REQUIRE(emitter.filled() == true);
}

TEST_CASE("RectEmitter filled mode produces points within bounds", "[effects]")
{
	const float w = 100.0f;
	const float h = 60.0f;
	sgc::RectEmitter emitter(w, h, true, 0.0f, 0.0f);
	std::mt19937 rng(42);

	for (int i = 0; i < 200; ++i)
	{
		const auto result = emitter.spawn(rng);
		REQUIRE(result.posX >= -w / 2.0f - 1e-3f);
		REQUIRE(result.posX <= w / 2.0f + 1e-3f);
		REQUIRE(result.posY >= -h / 2.0f - 1e-3f);
		REQUIRE(result.posY <= h / 2.0f + 1e-3f);
	}
}

TEST_CASE("RectEmitter edge mode stays on perimeter", "[effects]")
{
	const float w = 80.0f;
	const float h = 40.0f;
	sgc::RectEmitter emitter(w, h, false, 0.0f, 0.0f);
	std::mt19937 rng(42);

	for (int i = 0; i < 200; ++i)
	{
		const auto result = emitter.spawn(rng);
		const float halfW = w / 2.0f;
		const float halfH = h / 2.0f;

		// ポイントは辺上にあるはず
		const bool onHorizontalEdge =
			(std::abs(result.posY - halfH) < 1e-2f ||
			 std::abs(result.posY + halfH) < 1e-2f);
		const bool onVerticalEdge =
			(std::abs(result.posX - halfW) < 1e-2f ||
			 std::abs(result.posX + halfW) < 1e-2f);

		REQUIRE((onHorizontalEdge || onVerticalEdge));
	}
}

TEST_CASE("ConeEmitter direction within cone angle", "[effects]")
{
	const float halfAngle = 0.5f;
	const float baseAngle = 0.0f;
	sgc::ConeEmitter emitter(halfAngle, baseAngle);
	std::mt19937 rng(42);

	for (int i = 0; i < 200; ++i)
	{
		const auto result = emitter.spawn(rng);
		const float angle = std::atan2(result.dirY, result.dirX);
		REQUIRE(angle >= baseAngle - halfAngle - 1e-5f);
		REQUIRE(angle <= baseAngle + halfAngle + 1e-5f);
	}
}

TEST_CASE("ConeEmitter accessors", "[effects]")
{
	sgc::ConeEmitter emitter(0.3f, 1.5f);
	REQUIRE_THAT(emitter.halfAngle(), WithinAbs(0.3f, 1e-5f));
	REQUIRE_THAT(emitter.baseAngle(), WithinAbs(1.5f, 1e-5f));
}

TEST_CASE("EmitterShapeConcept is satisfied", "[effects]")
{
	STATIC_REQUIRE(sgc::EmitterShapeConcept<sgc::PointEmitter>);
	STATIC_REQUIRE(sgc::EmitterShapeConcept<sgc::CircleEmitter>);
	STATIC_REQUIRE(sgc::EmitterShapeConcept<sgc::ConeEmitter>);
}
