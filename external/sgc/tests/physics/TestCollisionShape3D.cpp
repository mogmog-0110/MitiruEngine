#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/physics/CollisionShape3D.hpp"

using namespace sgc;
using namespace sgc::physics;
using Catch::Matchers::WithinAbs;

TEST_CASE("testSphereSphere colliding", "[physics][collision3d]")
{
	SphereShape a{1.0f};
	SphereShape b{1.0f};
	auto result = testSphereSphere({0, 0, 0}, a, {1.5f, 0, 0}, b);
	CHECK(result.collided);
	CHECK_THAT(result.penetrationDepth, WithinAbs(0.5, 1e-4));
	CHECK_THAT(result.contactNormal.x, WithinAbs(1.0, 1e-4));
}

TEST_CASE("testSphereSphere not colliding", "[physics][collision3d]")
{
	SphereShape a{0.5f};
	SphereShape b{0.5f};
	auto result = testSphereSphere({0, 0, 0}, a, {3.0f, 0, 0}, b);
	CHECK_FALSE(result.collided);
}

TEST_CASE("testSphereBox colliding", "[physics][collision3d]")
{
	SphereShape sphere{1.0f};
	BoxShape box{{1.0f, 1.0f, 1.0f}};
	// 球の中心がボックスの面に近い位置
	auto result = testSphereBox({2.5f, 0, 0}, sphere, {0, 0, 0}, box);
	// 最近接点 = (1, 0, 0), 距離 = 1.5, 半径 = 1 => no collision
	CHECK_FALSE(result.collided);

	// もっと近づける
	auto result2 = testSphereBox({1.5f, 0, 0}, sphere, {0, 0, 0}, box);
	CHECK(result2.collided);
}

TEST_CASE("testSpherePlane colliding", "[physics][collision3d]")
{
	SphereShape sphere{1.0f};
	PlaneShape plane{{0, 1, 0}, 0.0f};

	// 球が平面上にめり込んでいる
	auto result = testSpherePlane({0, 0.5f, 0}, sphere, plane);
	CHECK(result.collided);
	CHECK_THAT(result.penetrationDepth, WithinAbs(0.5, 1e-4));
}

TEST_CASE("testSpherePlane not colliding", "[physics][collision3d]")
{
	SphereShape sphere{0.5f};
	PlaneShape plane{{0, 1, 0}, 0.0f};

	auto result = testSpherePlane({0, 5.0f, 0}, sphere, plane);
	CHECK_FALSE(result.collided);
}

TEST_CASE("testBoxBox colliding and not colliding", "[physics][collision3d]")
{
	BoxShape a{{1.0f, 1.0f, 1.0f}};
	BoxShape b{{1.0f, 1.0f, 1.0f}};

	// 重なる
	auto result = testBoxBox({0, 0, 0}, a, {1.5f, 0, 0}, b);
	CHECK(result.collided);
	CHECK_THAT(result.penetrationDepth, WithinAbs(0.5, 1e-4));

	// 離れている
	auto result2 = testBoxBox({0, 0, 0}, a, {5.0f, 0, 0}, b);
	CHECK_FALSE(result2.collided);
}
