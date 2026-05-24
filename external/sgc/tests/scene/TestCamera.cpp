#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/scene/Camera.hpp"

using Catch::Approx;

TEST_CASE("Camera default state", "[scene][camera]")
{
	sgc::Cameraf camera;
	REQUIRE(camera.zoom() == 1.0f);
	REQUIRE_FALSE(camera.isOrthographic());
}

TEST_CASE("Camera setPosition and setTarget", "[scene][camera]")
{
	sgc::Cameraf camera;
	camera.setPosition({0.0f, 5.0f, -10.0f});
	camera.setTarget({0.0f, 0.0f, 0.0f});

	REQUIRE(camera.position().x == 0.0f);
	REQUIRE(camera.position().y == 5.0f);
	REQUIRE(camera.position().z == -10.0f);
	REQUIRE(camera.target().x == 0.0f);
}

TEST_CASE("Camera viewMatrix is valid", "[scene][camera]")
{
	sgc::Cameraf camera;
	camera.setPosition({0.0f, 0.0f, -5.0f});
	camera.setTarget({0.0f, 0.0f, 0.0f});

	auto view = camera.viewMatrix();
	// View matrix should not be all zeros
	bool nonZero = false;
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j)
			if (view.m[i][j] != 0.0f) nonZero = true;
	REQUIRE(nonZero);
}

TEST_CASE("Camera perspective projection", "[scene][camera]")
{
	sgc::Cameraf camera;
	camera.setPerspective(1.0472f, 16.0f / 9.0f, 0.1f, 1000.0f);
	REQUIRE_FALSE(camera.isOrthographic());

	auto proj = camera.projectionMatrix();
	// Perspective matrix m[3][3] should be 0
	REQUIRE(proj.m[3][3] == Approx(0.0f));
}

TEST_CASE("Camera orthographic projection", "[scene][camera]")
{
	sgc::Cameraf camera;
	camera.setOrthographic(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 100.0f);
	REQUIRE(camera.isOrthographic());

	auto proj = camera.projectionMatrix();
	// Orthographic matrix m[3][3] should be 1
	REQUIRE(proj.m[3][3] == Approx(1.0f));
}

TEST_CASE("Camera zoom affects perspective", "[scene][camera]")
{
	sgc::Cameraf camera;
	camera.setPerspective(1.0f, 1.0f, 0.1f, 100.0f);

	auto proj1 = camera.projectionMatrix();
	camera.setZoom(0.5f);
	auto proj2 = camera.projectionMatrix();

	// Different zoom = different projection
	REQUIRE(proj1.m[0][0] != Approx(proj2.m[0][0]));
}

TEST_CASE("Camera shake updates offset", "[scene][camera]")
{
	sgc::Cameraf camera;
	camera.setPosition({0.0f, 0.0f, -5.0f});
	camera.setTarget({0.0f, 0.0f, 0.0f});

	camera.shake(1.0f, 0.5f);
	camera.update(0.1f);

	// After shake with time remaining, view matrix should differ
	auto view1 = camera.viewMatrix();
	camera.update(0.1f);
	auto view2 = camera.viewMatrix();

	// Shake offsets change between updates
	bool differs = false;
	for (int i = 0; i < 4 && !differs; ++i)
		for (int j = 0; j < 4 && !differs; ++j)
			if (view1.m[i][j] != view2.m[i][j]) differs = true;
	REQUIRE(differs);
}

TEST_CASE("Camera shake stops after duration", "[scene][camera]")
{
	sgc::Cameraf camera;
	camera.setPosition({0.0f, 0.0f, -5.0f});
	camera.setTarget({0.0f, 0.0f, 0.0f});

	camera.shake(1.0f, 0.1f);
	camera.update(0.5f); // exceeds duration

	auto view1 = camera.viewMatrix();
	camera.update(0.1f);
	auto view2 = camera.viewMatrix();

	// After shake ends, views should be identical
	bool same = true;
	for (int i = 0; i < 4 && same; ++i)
		for (int j = 0; j < 4 && same; ++j)
			if (view1.m[i][j] != view2.m[i][j]) same = false;
	REQUIRE(same);
}

TEST_CASE("Camera follow moves towards target", "[scene][camera]")
{
	sgc::Cameraf camera;
	camera.setPosition({0.0f, 0.0f, 0.0f});

	sgc::Vec3f targetPos{10.0f, 0.0f, 0.0f};
	camera.follow(targetPos, 0.5f, 1.0f);

	// Should move towards target
	REQUIRE(camera.position().x > 0.0f);
	REQUIRE(camera.position().x < 10.0f);
}

TEST_CASE("Camera setUp", "[scene][camera]")
{
	sgc::Cameraf camera;
	camera.setUp({0.0f, 0.0f, 1.0f});
	REQUIRE(camera.up().z == 1.0f);
}
