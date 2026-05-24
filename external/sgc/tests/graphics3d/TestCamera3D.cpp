#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/graphics3d/Camera3D.hpp"

using namespace sgc;
using namespace sgc::graphics3d;
using Catch::Matchers::WithinAbs;

TEST_CASE("Camera3D default state", "[graphics3d][camera]")
{
	Camera3D cam;
	CHECK(cam.position.z == 5.0f);
	CHECK(cam.target == Vec3f::zero());
	CHECK(cam.fovY == 60.0f);
}

TEST_CASE("Camera3D viewMatrix produces valid lookAt", "[graphics3d][camera]")
{
	Camera3D cam;
	cam.position = {0, 0, 5};
	cam.target = {0, 0, 0};
	cam.up = {0, 1, 0};

	const auto view = cam.viewMatrix();
	// カメラ位置をビュー行列で変換すると原点近くに来る
	const auto transformed = view.transformPoint(cam.position);
	CHECK_THAT(transformed.x, WithinAbs(0.0, 1e-4));
	CHECK_THAT(transformed.y, WithinAbs(0.0, 1e-4));
}

TEST_CASE("Camera3D projectionMatrix is non-identity", "[graphics3d][camera]")
{
	Camera3D cam;
	const auto proj = cam.projectionMatrix();
	// 透視投影行列は単位行列ではない
	CHECK(proj.m[3][2] != 0.0f);
}

TEST_CASE("Camera3D forward and right vectors", "[graphics3d][camera]")
{
	Camera3D cam;
	cam.position = {0, 0, 5};
	cam.target = {0, 0, 0};

	const auto fwd = cam.forward();
	CHECK_THAT(fwd.z, WithinAbs(-1.0, 1e-4));

	const auto r = cam.right();
	// 前方 (0,0,-1) x 上 (0,1,0) = (1,0,0) ではなく cross(forward, up)
	CHECK_THAT(std::abs(r.x), WithinAbs(1.0, 1e-4));
}

TEST_CASE("Camera3D orbit moves position", "[graphics3d][camera]")
{
	Camera3D cam;
	cam.target = {0, 0, 0};
	cam.orbit(0.0f, 0.0f, 10.0f);

	// yaw=0, pitch=0 => position = target + (0, 0, distance)
	CHECK_THAT(cam.position.z, WithinAbs(10.0, 1e-4));
	CHECK_THAT(cam.position.x, WithinAbs(0.0, 1e-4));
}
