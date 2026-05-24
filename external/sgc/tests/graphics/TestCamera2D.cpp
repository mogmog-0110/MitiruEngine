#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <numbers>

#include "sgc/graphics/Camera2D.hpp"

using Catch::Approx;

TEST_CASE("Camera2D default center is zero", "[graphics][Camera2D]")
{
	sgc::graphics::Camera2D cam;
	const auto center = cam.getCenter();
	REQUIRE(center.x == Approx(0.0f));
	REQUIRE(center.y == Approx(0.0f));
}

TEST_CASE("Camera2D constructor with config", "[graphics][Camera2D]")
{
	sgc::graphics::Camera2DConfig cfg{
		.center = {100.0f, 200.0f},
		.zoom = 2.0f,
		.rotation = 0.5f,
		.screenSize = {1280.0f, 720.0f}
	};
	sgc::graphics::Camera2D cam(cfg);

	REQUIRE(cam.getCenter().x == Approx(100.0f));
	REQUIRE(cam.getCenter().y == Approx(200.0f));
	REQUIRE(cam.getZoom() == Approx(2.0f));
	REQUIRE(cam.getRotation() == Approx(0.5f));
	REQUIRE(cam.getScreenSize().x == Approx(1280.0f));
}

TEST_CASE("Camera2D worldToScreen maps center to screen center", "[graphics][Camera2D]")
{
	sgc::graphics::Camera2DConfig cfg{
		.center = {50.0f, 30.0f},
		.zoom = 1.0f,
		.rotation = 0.0f,
		.screenSize = {800.0f, 600.0f}
	};
	sgc::graphics::Camera2D cam(cfg);

	const auto screen = cam.worldToScreen({50.0f, 30.0f});
	REQUIRE(screen.x == Approx(400.0f));
	REQUIRE(screen.y == Approx(300.0f));
}

TEST_CASE("Camera2D screenToWorld is inverse of worldToScreen", "[graphics][Camera2D]")
{
	sgc::graphics::Camera2DConfig cfg{
		.center = {100.0f, -50.0f},
		.zoom = 1.5f,
		.rotation = 0.0f,
		.screenSize = {1024.0f, 768.0f}
	};
	sgc::graphics::Camera2D cam(cfg);

	const sgc::Vec2f worldPt{200.0f, 75.0f};
	const auto screenPt = cam.worldToScreen(worldPt);
	const auto recovered = cam.screenToWorld(screenPt);

	REQUIRE(recovered.x == Approx(worldPt.x).margin(0.01f));
	REQUIRE(recovered.y == Approx(worldPt.y).margin(0.01f));
}

TEST_CASE("Camera2D zoom in makes world coords larger on screen", "[graphics][Camera2D]")
{
	sgc::graphics::Camera2DConfig cfg{
		.center = {0.0f, 0.0f},
		.zoom = 1.0f,
		.rotation = 0.0f,
		.screenSize = {800.0f, 600.0f}
	};
	sgc::graphics::Camera2D cam1(cfg);

	cfg.zoom = 2.0f;
	sgc::graphics::Camera2D cam2(cfg);

	const sgc::Vec2f worldPt{100.0f, 0.0f};
	const auto s1 = cam1.worldToScreen(worldPt);
	const auto s2 = cam2.worldToScreen(worldPt);

	// zoom=2 should move the point further from screen center
	const float dist1 = std::abs(s1.x - 400.0f);
	const float dist2 = std::abs(s2.x - 400.0f);
	REQUIRE(dist2 == Approx(dist1 * 2.0f));
}

TEST_CASE("Camera2D follow moves toward target", "[graphics][Camera2D]")
{
	sgc::graphics::Camera2D cam;
	cam.setScreenSize({800.0f, 600.0f});
	cam.setCenter({0.0f, 0.0f});

	const sgc::Vec2f target{100.0f, 100.0f};
	cam.follow(target, 5.0f, 0.1f);

	const auto center = cam.getCenter();
	// Should have moved toward target but not reached it
	REQUIRE(center.x > 0.0f);
	REQUIRE(center.y > 0.0f);
	REQUIRE(center.x < 100.0f);
	REQUIRE(center.y < 100.0f);
}

TEST_CASE("Camera2D follow with zero smoothing teleports", "[graphics][Camera2D]")
{
	sgc::graphics::Camera2D cam;
	cam.setCenter({0.0f, 0.0f});

	const sgc::Vec2f target{500.0f, 300.0f};
	cam.follow(target, 0.0f, 0.016f);

	REQUIRE(cam.getCenter().x == Approx(500.0f));
	REQUIRE(cam.getCenter().y == Approx(300.0f));
}

TEST_CASE("Camera2D getVisibleRect shrinks with zoom", "[graphics][Camera2D]")
{
	sgc::graphics::Camera2DConfig cfg{
		.center = {0.0f, 0.0f},
		.zoom = 1.0f,
		.rotation = 0.0f,
		.screenSize = {800.0f, 600.0f}
	};
	sgc::graphics::Camera2D cam1(cfg);
	const auto rect1 = cam1.getVisibleRect();

	cfg.zoom = 2.0f;
	sgc::graphics::Camera2D cam2(cfg);
	const auto rect2 = cam2.getVisibleRect();

	const float width1 = rect1.max.x - rect1.min.x;
	const float width2 = rect2.max.x - rect2.min.x;

	REQUIRE(width2 == Approx(width1 / 2.0f));
}

TEST_CASE("Camera2D rotation transforms correctly", "[graphics][Camera2D]")
{
	const float halfPi = std::numbers::pi_v<float> / 2.0f;
	sgc::graphics::Camera2DConfig cfg{
		.center = {0.0f, 0.0f},
		.zoom = 1.0f,
		.rotation = halfPi,
		.screenSize = {800.0f, 600.0f}
	};
	sgc::graphics::Camera2D cam(cfg);

	// Roundtrip test with rotation
	const sgc::Vec2f worldPt{50.0f, 30.0f};
	const auto screenPt = cam.worldToScreen(worldPt);
	const auto recovered = cam.screenToWorld(screenPt);

	REQUIRE(recovered.x == Approx(worldPt.x).margin(0.1f));
	REQUIRE(recovered.y == Approx(worldPt.y).margin(0.1f));
}

TEST_CASE("Camera2D shake adds offset", "[graphics][Camera2D]")
{
	sgc::graphics::Camera2D cam;
	cam.setScreenSize({800.0f, 600.0f});
	cam.setCenter({0.0f, 0.0f});

	cam.shake(10.0f, 1.0f);
	cam.update(0.1f); // Trigger shake offset

	const auto offset = cam.getShakeOffset();
	// At least one component should be non-zero
	const bool hasOffset = (offset.x != 0.0f || offset.y != 0.0f);
	REQUIRE(hasOffset);
}

TEST_CASE("Camera2D update decays shake", "[graphics][Camera2D]")
{
	sgc::graphics::Camera2D cam;
	cam.setScreenSize({800.0f, 600.0f});
	cam.setCenter({0.0f, 0.0f});

	cam.shake(20.0f, 0.5f);
	cam.update(0.1f);

	const auto offset1 = cam.getShakeOffset();
	const float mag1 = std::sqrt(offset1.x * offset1.x + offset1.y * offset1.y);

	// After shake expires, offset should be zero
	cam.update(0.5f);
	const auto offsetFinal = cam.getShakeOffset();
	REQUIRE(offsetFinal.x == Approx(0.0f));
	REQUIRE(offsetFinal.y == Approx(0.0f));

	// Magnitude during shake should have been positive
	REQUIRE(mag1 > 0.0f);
}

TEST_CASE("Camera2D setters work correctly", "[graphics][Camera2D]")
{
	sgc::graphics::Camera2D cam;

	cam.setCenter({42.0f, 13.0f});
	REQUIRE(cam.getCenter().x == Approx(42.0f));
	REQUIRE(cam.getCenter().y == Approx(13.0f));

	cam.setZoom(3.0f);
	REQUIRE(cam.getZoom() == Approx(3.0f));

	cam.setRotation(1.5f);
	REQUIRE(cam.getRotation() == Approx(1.5f));

	cam.setScreenSize({1920.0f, 1080.0f});
	REQUIRE(cam.getScreenSize().x == Approx(1920.0f));
	REQUIRE(cam.getScreenSize().y == Approx(1080.0f));
}
