#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <numbers>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>

#include <mitiru/render/Camera3D.hpp>

using Catch::Approx;

// ============================================================
// Camera3D - basic construction
// ============================================================

TEST_CASE("Camera3D - default constructor", "[camera3d]")
{
	const mitiru::render::Camera3D cam;

	REQUIRE(cam.position().x == Approx(0.0f));
	REQUIRE(cam.position().y == Approx(0.0f));
	REQUIRE(cam.position().z == Approx(-5.0f));
	REQUIRE(cam.target().x == Approx(0.0f));
	REQUIRE(cam.target().y == Approx(0.0f));
	REQUIRE(cam.target().z == Approx(0.0f));
	REQUIRE(cam.nearClip() == Approx(0.1f));
	REQUIRE(cam.farClip() == Approx(1000.0f));
}

TEST_CASE("Camera3D - parameterized constructor", "[camera3d]")
{
	const mitiru::render::Camera3D cam(
		{0, 10, -20}, {0, 0, 0}, {0, 1, 0},
		1.0f, 1.5f, 0.5f, 500.0f);

	REQUIRE(cam.position().y == Approx(10.0f));
	REQUIRE(cam.fov() == Approx(1.0f));
	REQUIRE(cam.aspectRatio() == Approx(1.5f));
	REQUIRE(cam.nearClip() == Approx(0.5f));
	REQUIRE(cam.farClip() == Approx(500.0f));
}

// ============================================================
// Camera3D - view matrix
// ============================================================

TEST_CASE("Camera3D - viewMatrix identity at origin looking +Z", "[camera3d]")
{
	mitiru::render::Camera3D cam;
	cam.setPosition({0, 0, 0});
	cam.setTarget({0, 0, 1});
	cam.setUp({0, 1, 0});

	const auto view = cam.viewMatrix();

	/// 原点のカメラが+Z方向を見ている場合、ビュー行列は原点をそのまま変換する
	const auto transformed = view.transformPoint({0, 0, 0});
	REQUIRE(transformed.x == Approx(0.0f).margin(1e-5f));
	REQUIRE(transformed.y == Approx(0.0f).margin(1e-5f));
	REQUIRE(transformed.z == Approx(0.0f).margin(1e-5f));
}

TEST_CASE("Camera3D - viewMatrix translates origin to camera space", "[camera3d]")
{
	mitiru::render::Camera3D cam;
	cam.setPosition({0, 5, -10});
	cam.setTarget({0, 0, 0});
	cam.setUp({0, 1, 0});

	const auto view = cam.viewMatrix();

	/// ワールド原点をビュー空間に変換する
	const auto origin = view.transformPoint({0, 0, 0});

	/// カメラから原点までの距離が保存されている
	const float distance = std::sqrt(5.0f * 5.0f + 10.0f * 10.0f);
	const float viewDist = std::sqrt(
		origin.x * origin.x + origin.y * origin.y + origin.z * origin.z);
	REQUIRE(viewDist == Approx(distance).margin(0.01f));
}

// ============================================================
// Camera3D - projection matrix
// ============================================================

TEST_CASE("Camera3D - projectionMatrix near plane mapping", "[camera3d]")
{
	mitiru::render::Camera3D cam;
	cam.setFov(std::numbers::pi_v<float> / 3.0f);
	cam.setAspectRatio(16.0f / 9.0f);
	cam.setNearClip(0.1f);
	cam.setFarClip(100.0f);

	const auto proj = cam.projectionMatrix();

	/// 射影行列のm[2][3]にnear*far要素が含まれる
	REQUIRE(proj.m[3][2] == Approx(-1.0f));
}

// ============================================================
// Camera3D - viewProjectionMatrix
// ============================================================

TEST_CASE("Camera3D - viewProjectionMatrix equals proj * view", "[camera3d]")
{
	mitiru::render::Camera3D cam(
		{3, 5, -8}, {0, 0, 0}, {0, 1, 0},
		1.0f, 1.5f, 0.1f, 100.0f);

	const auto vp = cam.viewProjectionMatrix();
	const auto expected = cam.projectionMatrix() * cam.viewMatrix();

	for (int r = 0; r < 4; ++r)
	{
		for (int c = 0; c < 4; ++c)
		{
			REQUIRE(vp.m[r][c] == Approx(expected.m[r][c]).margin(1e-5f));
		}
	}
}

// ============================================================
// Camera3D - direction vectors
// ============================================================

TEST_CASE("Camera3D - forwardDirection points toward target", "[camera3d]")
{
	mitiru::render::Camera3D cam;
	cam.setPosition({0, 0, -10});
	cam.setTarget({0, 0, 0});

	const auto fwd = cam.forwardDirection();
	REQUIRE(fwd.x == Approx(0.0f).margin(1e-5f));
	REQUIRE(fwd.y == Approx(0.0f).margin(1e-5f));
	REQUIRE(fwd.z == Approx(1.0f).margin(1e-5f));
}

TEST_CASE("Camera3D - rightDirection is perpendicular to forward", "[camera3d]")
{
	mitiru::render::Camera3D cam;
	cam.setPosition({0, 0, -10});
	cam.setTarget({0, 0, 0});
	cam.setUp({0, 1, 0});

	const auto fwd = cam.forwardDirection();
	const auto right = cam.rightDirection();

	/// 前方と右方向は直交する
	const float dotProduct = fwd.dot(right);
	REQUIRE(dotProduct == Approx(0.0f).margin(1e-5f));

	/// 右方向は正規化済み
	REQUIRE(right.length() == Approx(1.0f).margin(1e-5f));
}

TEST_CASE("Camera3D - upDirection is perpendicular to forward and right", "[camera3d]")
{
	mitiru::render::Camera3D cam;
	cam.setPosition({0, 0, -10});
	cam.setTarget({0, 0, 0});
	cam.setUp({0, 1, 0});

	const auto fwd = cam.forwardDirection();
	const auto right = cam.rightDirection();
	const auto up = cam.upDirection();

	REQUIRE(fwd.dot(up) == Approx(0.0f).margin(1e-5f));
	REQUIRE(right.dot(up) == Approx(0.0f).margin(1e-5f));
	REQUIRE(up.length() == Approx(1.0f).margin(1e-5f));
}

// ============================================================
// Camera3D - orbitAround
// ============================================================

TEST_CASE("Camera3D - orbitAround at zero yaw zero pitch", "[camera3d]")
{
	mitiru::render::Camera3D cam;
	cam.orbitAround({0, 0, 0}, 0.0f, 0.0f, 10.0f);

	/// yaw=0, pitch=0 → カメラは+Z方向に配置される
	REQUIRE(cam.position().x == Approx(0.0f).margin(1e-4f));
	REQUIRE(cam.position().y == Approx(0.0f).margin(1e-4f));
	REQUIRE(cam.position().z == Approx(10.0f).margin(1e-4f));

	/// 注視点は原点
	REQUIRE(cam.target().x == Approx(0.0f));
	REQUIRE(cam.target().y == Approx(0.0f));
	REQUIRE(cam.target().z == Approx(0.0f));
}

TEST_CASE("Camera3D - orbitAround at 90 deg yaw", "[camera3d]")
{
	mitiru::render::Camera3D cam;
	const float halfPi = std::numbers::pi_v<float> / 2.0f;
	cam.orbitAround({0, 0, 0}, halfPi, 0.0f, 5.0f);

	/// yaw=90度 → カメラは+X方向に配置される
	REQUIRE(cam.position().x == Approx(5.0f).margin(1e-4f));
	REQUIRE(cam.position().y == Approx(0.0f).margin(1e-4f));
	REQUIRE(cam.position().z == Approx(0.0f).margin(1e-3f));
}

TEST_CASE("Camera3D - orbitAround distance is preserved", "[camera3d]")
{
	mitiru::render::Camera3D cam;
	const sgc::Vec3f center{5, 3, -2};
	cam.orbitAround(center, 0.7f, 0.3f, 15.0f);

	const auto diff = cam.position() - center;
	REQUIRE(diff.length() == Approx(15.0f).margin(1e-4f));
}

TEST_CASE("Camera3D - orbitAround pitch clamp prevents gimbal lock", "[camera3d]")
{
	mitiru::render::Camera3D cam;
	/// ピッチが90度を超えてもクランプされる
	cam.orbitAround({0, 0, 0}, 0.0f, 3.0f, 10.0f);

	/// 距離は10のまま
	REQUIRE(cam.position().length() == Approx(10.0f).margin(0.01f));
}

// ============================================================
// Camera3D - lookDirection (FPS camera)
// ============================================================

TEST_CASE("Camera3D - lookDirection at zero yaw zero pitch looks +Z", "[camera3d]")
{
	mitiru::render::Camera3D cam;
	cam.setPosition({0, 0, 0});
	cam.lookDirection(0.0f, 0.0f);

	/// yaw=0, pitch=0 → +Z方向
	const auto dir = cam.forwardDirection();
	REQUIRE(dir.x == Approx(0.0f).margin(1e-4f));
	REQUIRE(dir.y == Approx(0.0f).margin(1e-4f));
	REQUIRE(dir.z == Approx(1.0f).margin(1e-4f));
}

TEST_CASE("Camera3D - lookDirection at 90 deg yaw looks +X", "[camera3d]")
{
	mitiru::render::Camera3D cam;
	cam.setPosition({0, 0, 0});
	const float halfPi = std::numbers::pi_v<float> / 2.0f;
	cam.lookDirection(halfPi, 0.0f);

	const auto dir = cam.forwardDirection();
	REQUIRE(dir.x == Approx(1.0f).margin(1e-4f));
	REQUIRE(dir.y == Approx(0.0f).margin(1e-4f));
	REQUIRE(dir.z == Approx(0.0f).margin(1e-3f));
}

TEST_CASE("Camera3D - lookDirection preserves camera position", "[camera3d]")
{
	mitiru::render::Camera3D cam;
	cam.setPosition({5, 10, -3});
	cam.lookDirection(1.0f, 0.5f);

	/// 位置は変わらない
	REQUIRE(cam.position().x == Approx(5.0f));
	REQUIRE(cam.position().y == Approx(10.0f));
	REQUIRE(cam.position().z == Approx(-3.0f));
}
