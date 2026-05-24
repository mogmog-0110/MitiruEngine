#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/physics/RayCast3D.hpp"

using namespace sgc;
using namespace sgc::physics;
using Catch::Matchers::WithinAbs;

TEST_CASE("raycastSphere hit", "[physics][raycast3d]")
{
	Ray3D ray{{0, 0, 0}, {0, 0, -1}};
	auto hit = raycastSphere(ray, {0, 0, -5}, 1.0f);
	CHECK(hit.hit);
	CHECK_THAT(hit.distance, WithinAbs(4.0, 1e-4));
	CHECK_THAT(hit.point.z, WithinAbs(-4.0, 1e-4));
}

TEST_CASE("raycastSphere miss", "[physics][raycast3d]")
{
	Ray3D ray{{0, 0, 0}, {0, 0, -1}};
	auto hit = raycastSphere(ray, {10, 0, -5}, 1.0f);
	CHECK_FALSE(hit.hit);
}

TEST_CASE("raycastBox hit", "[physics][raycast3d]")
{
	Ray3D ray{{0, 0, 5}, {0, 0, -1}};
	auto hit = raycastBox(ray, {-1, -1, -1}, {1, 1, 1});
	CHECK(hit.hit);
	CHECK_THAT(hit.distance, WithinAbs(4.0, 1e-4));
	CHECK_THAT(hit.point.z, WithinAbs(1.0, 1e-4));
}

TEST_CASE("raycastBox miss", "[physics][raycast3d]")
{
	Ray3D ray{{0, 0, 5}, {1, 0, 0}};  // 横向きのレイ
	auto hit = raycastBox(ray, {-1, -1, -1}, {1, 1, 1});
	CHECK_FALSE(hit.hit);
}

TEST_CASE("raycastPlane hit", "[physics][raycast3d]")
{
	Ray3D ray{{0, 5, 0}, {0, -1, 0}};
	auto hit = raycastPlane(ray, {0, 1, 0}, 0.0f);
	CHECK(hit.hit);
	CHECK_THAT(hit.distance, WithinAbs(5.0, 1e-4));
	CHECK_THAT(hit.point.y, WithinAbs(0.0, 1e-4));
}
