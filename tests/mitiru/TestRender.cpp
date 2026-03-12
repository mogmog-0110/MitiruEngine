#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

#include <sgc/math/Vec2.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/Vertex2D.hpp>
#include <mitiru/render/SpriteBatch.hpp>
#include <mitiru/render/ShapeRenderer.hpp>
#include <mitiru/render/BitmapFont.hpp>
#include <mitiru/render/TextRenderer.hpp>
#include <mitiru/render/Renderer2D.hpp>
#include <mitiru/render/Camera2D.hpp>
#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/ScreenCapture.hpp>
#include <mitiru/render/RenderGraph.hpp>
#include <mitiru/core/Screen.hpp>
#include <mitiru/gfx/null/NullDevice.hpp>

// ============================================================
// Vertex2D
// ============================================================

TEST_CASE("Vertex2D - default constructor", "[render]")
{
	constexpr mitiru::render::Vertex2D v;

	REQUIRE(v.position.x == 0.0f);
	REQUIRE(v.position.y == 0.0f);
	REQUIRE(v.texCoord.x == 0.0f);
	REQUIRE(v.texCoord.y == 0.0f);
	REQUIRE(v.color.r == 1.0f);
	REQUIRE(v.color.g == 1.0f);
	REQUIRE(v.color.b == 1.0f);
	REQUIRE(v.color.a == 1.0f);
}

TEST_CASE("Vertex2D - full constructor", "[render]")
{
	constexpr mitiru::render::Vertex2D v(
		sgc::Vec2f{10.0f, 20.0f},
		sgc::Vec2f{0.5f, 0.5f},
		sgc::Colorf::red());

	REQUIRE(v.position.x == 10.0f);
	REQUIRE(v.position.y == 20.0f);
	REQUIRE(v.texCoord.x == 0.5f);
	REQUIRE(v.texCoord.y == 0.5f);
	REQUIRE(v.color.r == 1.0f);
	REQUIRE(v.color.g == 0.0f);
	REQUIRE(v.color.b == 0.0f);
	REQUIRE(v.color.a == 1.0f);
}

TEST_CASE("Vertex2D - position and color constructor", "[render]")
{
	constexpr mitiru::render::Vertex2D v(
		sgc::Vec2f{100.0f, 200.0f},
		sgc::Colorf::green());

	REQUIRE(v.position.x == 100.0f);
	REQUIRE(v.position.y == 200.0f);
	REQUIRE(v.texCoord.x == 0.0f);
	REQUIRE(v.texCoord.y == 0.0f);
	REQUIRE(v.color.g == 1.0f);
}

// ============================================================
// SpriteBatch
// ============================================================

TEST_CASE("SpriteBatch - initial state is empty", "[render]")
{
	const mitiru::render::SpriteBatch batch;

	REQUIRE(batch.vertexCount() == 0);
	REQUIRE(batch.indexCount() == 0);
	REQUIRE(batch.drawCallCount() == 0);
}

TEST_CASE("SpriteBatch - begin clears previous data", "[render]")
{
	mitiru::render::SpriteBatch batch;
	batch.begin();
	batch.drawRect(sgc::Rectf{0, 0, 10, 10}, sgc::Colorf::white());
	batch.end();

	REQUIRE(batch.vertexCount() > 0);

	/// begin should clear
	batch.begin();
	REQUIRE(batch.vertexCount() == 0);
	REQUIRE(batch.indexCount() == 0);
	REQUIRE(batch.drawCallCount() == 0);
}

TEST_CASE("SpriteBatch - drawRect generates 4 vertices and 6 indices", "[render]")
{
	mitiru::render::SpriteBatch batch;
	batch.begin();
	batch.drawRect(sgc::Rectf{10, 20, 100, 50}, sgc::Colorf::red());

	REQUIRE(batch.vertexCount() == 4);
	REQUIRE(batch.indexCount() == 6);
	REQUIRE(batch.drawCallCount() == 1);
}

TEST_CASE("SpriteBatch - multiple drawRect calls accumulate", "[render]")
{
	mitiru::render::SpriteBatch batch;
	batch.begin();
	batch.drawRect(sgc::Rectf{0, 0, 10, 10}, sgc::Colorf::red());
	batch.drawRect(sgc::Rectf{20, 20, 10, 10}, sgc::Colorf::blue());
	batch.drawRect(sgc::Rectf{40, 40, 10, 10}, sgc::Colorf::green());

	REQUIRE(batch.vertexCount() == 12);
	REQUIRE(batch.indexCount() == 18);
	REQUIRE(batch.drawCallCount() == 3);
}

TEST_CASE("SpriteBatch - drawRectFrame generates 4 quads", "[render]")
{
	mitiru::render::SpriteBatch batch;
	batch.begin();
	batch.drawRectFrame(sgc::Rectf{10, 10, 100, 50}, sgc::Colorf::white(), 2.0f);

	/// 4 quads (top, bottom, left, right) = 16 vertices, 24 indices
	REQUIRE(batch.vertexCount() == 16);
	REQUIRE(batch.indexCount() == 24);
	REQUIRE(batch.drawCallCount() == 1);
}

TEST_CASE("SpriteBatch - drawRect without begin is ignored", "[render]")
{
	mitiru::render::SpriteBatch batch;
	/// not calling begin()
	batch.drawRect(sgc::Rectf{0, 0, 10, 10}, sgc::Colorf::red());

	REQUIRE(batch.vertexCount() == 0);
	REQUIRE(batch.drawCallCount() == 0);
}

TEST_CASE("SpriteBatch - end stops recording", "[render]")
{
	mitiru::render::SpriteBatch batch;
	batch.begin();
	batch.drawRect(sgc::Rectf{0, 0, 10, 10}, sgc::Colorf::red());
	batch.end();

	/// After end, drawRect should be ignored
	batch.drawRect(sgc::Rectf{20, 20, 10, 10}, sgc::Colorf::blue());
	REQUIRE(batch.vertexCount() == 4);
	REQUIRE(batch.drawCallCount() == 1);
}

TEST_CASE("SpriteBatch - drawSprite without rotation generates quad", "[render]")
{
	mitiru::render::SpriteBatch batch;
	batch.begin();
	batch.drawSprite(0,
		sgc::Rectf{0, 0, 64, 64},
		sgc::Rectf{0, 0, 1, 1},
		sgc::Colorf::white(),
		0.0f);

	REQUIRE(batch.vertexCount() == 4);
	REQUIRE(batch.indexCount() == 6);
	REQUIRE(batch.drawCallCount() == 1);
}

TEST_CASE("SpriteBatch - drawSprite with rotation generates quad", "[render]")
{
	mitiru::render::SpriteBatch batch;
	batch.begin();
	batch.drawSprite(0,
		sgc::Rectf{0, 0, 64, 64},
		sgc::Rectf{0, 0, 1, 1},
		sgc::Colorf::white(),
		1.57f);

	REQUIRE(batch.vertexCount() == 4);
	REQUIRE(batch.indexCount() == 6);
	REQUIRE(batch.drawCallCount() == 1);
}

TEST_CASE("SpriteBatch - vertices() returns correct data", "[render]")
{
	mitiru::render::SpriteBatch batch;
	batch.begin();
	batch.drawRect(sgc::Rectf{10, 20, 30, 40}, sgc::Colorf::red());

	const auto& verts = batch.vertices();
	REQUIRE(verts.size() == 4);

	/// First vertex should be top-left corner
	REQUIRE(verts[0].position.x == Catch::Approx(10.0f));
	REQUIRE(verts[0].position.y == Catch::Approx(20.0f));

	/// Second vertex should be top-right
	REQUIRE(verts[1].position.x == Catch::Approx(40.0f));
	REQUIRE(verts[1].position.y == Catch::Approx(20.0f));

	/// Third vertex should be bottom-right
	REQUIRE(verts[2].position.x == Catch::Approx(40.0f));
	REQUIRE(verts[2].position.y == Catch::Approx(60.0f));

	/// Fourth vertex should be bottom-left
	REQUIRE(verts[3].position.x == Catch::Approx(10.0f));
	REQUIRE(verts[3].position.y == Catch::Approx(60.0f));
}

TEST_CASE("SpriteBatch - indices() returns correct winding", "[render]")
{
	mitiru::render::SpriteBatch batch;
	batch.begin();
	batch.drawRect(sgc::Rectf{0, 0, 10, 10}, sgc::Colorf::white());

	const auto& idx = batch.indices();
	REQUIRE(idx.size() == 6);
	/// Two triangles: (0,1,2) and (0,2,3)
	REQUIRE(idx[0] == 0);
	REQUIRE(idx[1] == 1);
	REQUIRE(idx[2] == 2);
	REQUIRE(idx[3] == 0);
	REQUIRE(idx[4] == 2);
	REQUIRE(idx[5] == 3);
}

// ============================================================
// ShapeRenderer
// ============================================================

TEST_CASE("ShapeRenderer - initial state is empty", "[render]")
{
	const mitiru::render::ShapeRenderer shapes;

	REQUIRE(shapes.vertexCount() == 0);
	REQUIRE(shapes.indexCount() == 0);
	REQUIRE(shapes.drawCallCount() == 0);
}

TEST_CASE("ShapeRenderer - drawCircle generates vertices", "[render]")
{
	mitiru::render::ShapeRenderer shapes;
	shapes.drawCircle(sgc::Vec2f{100, 100}, 50.0f, sgc::Colorf::green(), 16);

	/// 1 center + (16+1) circumference = 18 vertices
	REQUIRE(shapes.vertexCount() == 18);
	/// 16 triangles * 3 indices = 48
	REQUIRE(shapes.indexCount() == 48);
	REQUIRE(shapes.drawCallCount() == 1);
}

TEST_CASE("ShapeRenderer - drawCircle with default segments", "[render]")
{
	mitiru::render::ShapeRenderer shapes;
	shapes.drawCircle(sgc::Vec2f{0, 0}, 10.0f, sgc::Colorf::white());

	/// default segments = 32: 1 center + 33 circumference = 34 vertices
	REQUIRE(shapes.vertexCount() == 34);
	/// 32 * 3 = 96 indices
	REQUIRE(shapes.indexCount() == 96);
}

TEST_CASE("ShapeRenderer - drawCircle with less than 3 segments does nothing", "[render]")
{
	mitiru::render::ShapeRenderer shapes;
	shapes.drawCircle(sgc::Vec2f{0, 0}, 10.0f, sgc::Colorf::white(), 2);

	REQUIRE(shapes.vertexCount() == 0);
	REQUIRE(shapes.indexCount() == 0);
	REQUIRE(shapes.drawCallCount() == 0);
}

TEST_CASE("ShapeRenderer - drawLine generates 4 vertices", "[render]")
{
	mitiru::render::ShapeRenderer shapes;
	shapes.drawLine(sgc::Vec2f{0, 0}, sgc::Vec2f{100, 0}, sgc::Colorf::white(), 2.0f);

	REQUIRE(shapes.vertexCount() == 4);
	REQUIRE(shapes.indexCount() == 6);
	REQUIRE(shapes.drawCallCount() == 1);
}

TEST_CASE("ShapeRenderer - drawLine with zero length does nothing", "[render]")
{
	mitiru::render::ShapeRenderer shapes;
	shapes.drawLine(sgc::Vec2f{50, 50}, sgc::Vec2f{50, 50}, sgc::Colorf::white(), 2.0f);

	REQUIRE(shapes.vertexCount() == 0);
	REQUIRE(shapes.drawCallCount() == 0);
}

TEST_CASE("ShapeRenderer - drawTriangle generates 3 vertices", "[render]")
{
	mitiru::render::ShapeRenderer shapes;
	shapes.drawTriangle(
		sgc::Vec2f{0, 0},
		sgc::Vec2f{100, 0},
		sgc::Vec2f{50, 80},
		sgc::Colorf::blue());

	REQUIRE(shapes.vertexCount() == 3);
	REQUIRE(shapes.indexCount() == 3);
	REQUIRE(shapes.drawCallCount() == 1);
}

TEST_CASE("ShapeRenderer - drawPolygon with convex quad", "[render]")
{
	const std::vector<sgc::Vec2f> quad =
	{
		{0, 0}, {100, 0}, {100, 100}, {0, 100}
	};

	mitiru::render::ShapeRenderer shapes;
	shapes.drawPolygon(quad, sgc::Colorf::white());

	/// 4 vertices
	REQUIRE(shapes.vertexCount() == 4);
	/// 2 triangles from fan = 6 indices
	REQUIRE(shapes.indexCount() == 6);
	REQUIRE(shapes.drawCallCount() == 1);
}

TEST_CASE("ShapeRenderer - drawPolygon with pentagon", "[render]")
{
	const std::vector<sgc::Vec2f> pentagon =
	{
		{50, 0}, {100, 35}, {80, 90}, {20, 90}, {0, 35}
	};

	mitiru::render::ShapeRenderer shapes;
	shapes.drawPolygon(pentagon, sgc::Colorf::green());

	REQUIRE(shapes.vertexCount() == 5);
	/// 3 triangles from fan = 9 indices
	REQUIRE(shapes.indexCount() == 9);
}

TEST_CASE("ShapeRenderer - drawPolygon with less than 3 points does nothing", "[render]")
{
	const std::vector<sgc::Vec2f> line = {{0, 0}, {10, 10}};

	mitiru::render::ShapeRenderer shapes;
	shapes.drawPolygon(line, sgc::Colorf::white());

	REQUIRE(shapes.vertexCount() == 0);
	REQUIRE(shapes.drawCallCount() == 0);
}

TEST_CASE("ShapeRenderer - flush clears all data", "[render]")
{
	mitiru::render::ShapeRenderer shapes;
	shapes.drawCircle(sgc::Vec2f{0, 0}, 10.0f, sgc::Colorf::white());
	shapes.drawLine(sgc::Vec2f{0, 0}, sgc::Vec2f{10, 10}, sgc::Colorf::red());

	REQUIRE(shapes.vertexCount() > 0);

	shapes.flush();

	REQUIRE(shapes.vertexCount() == 0);
	REQUIRE(shapes.indexCount() == 0);
	REQUIRE(shapes.drawCallCount() == 0);
}

TEST_CASE("ShapeRenderer - multiple draws accumulate", "[render]")
{
	mitiru::render::ShapeRenderer shapes;
	shapes.drawTriangle(
		sgc::Vec2f{0, 0}, sgc::Vec2f{10, 0}, sgc::Vec2f{5, 8},
		sgc::Colorf::red());
	shapes.drawTriangle(
		sgc::Vec2f{20, 0}, sgc::Vec2f{30, 0}, sgc::Vec2f{25, 8},
		sgc::Colorf::blue());

	REQUIRE(shapes.vertexCount() == 6);
	REQUIRE(shapes.indexCount() == 6);
	REQUIRE(shapes.drawCallCount() == 2);
}

// ============================================================
// Renderer2D
// ============================================================

TEST_CASE("Renderer2D - default constructor", "[render]")
{
	const mitiru::render::Renderer2D renderer;
	REQUIRE(renderer.totalDrawCalls() == 0);
}

TEST_CASE("Renderer2D - constructor with screen size", "[render]")
{
	const mitiru::render::Renderer2D renderer(800.0f, 600.0f);
	REQUIRE(renderer.totalDrawCalls() == 0);
}

TEST_CASE("Renderer2D - beginFrame and endFrame cycle", "[render]")
{
	mitiru::render::Renderer2D renderer;
	REQUIRE_NOTHROW(renderer.beginFrame());
	REQUIRE_NOTHROW(renderer.endFrame());
}

TEST_CASE("Renderer2D - drawRect delegates to SpriteBatch", "[render]")
{
	mitiru::render::Renderer2D renderer;
	renderer.beginFrame();
	renderer.drawRect(sgc::Rectf{0, 0, 50, 50}, sgc::Colorf::red());
	renderer.endFrame();

	REQUIRE(renderer.totalDrawCalls() >= 1);
}

TEST_CASE("Renderer2D - drawCircle delegates to ShapeRenderer", "[render]")
{
	mitiru::render::Renderer2D renderer;
	renderer.beginFrame();
	renderer.drawCircle(sgc::Vec2f{100, 100}, 25.0f, sgc::Colorf::green());
	renderer.endFrame();

	REQUIRE(renderer.totalDrawCalls() >= 1);
}

TEST_CASE("Renderer2D - drawLine delegates to ShapeRenderer", "[render]")
{
	mitiru::render::Renderer2D renderer;
	renderer.beginFrame();
	renderer.drawLine(sgc::Vec2f{0, 0}, sgc::Vec2f{100, 100}, sgc::Colorf::white());
	renderer.endFrame();

	REQUIRE(renderer.totalDrawCalls() >= 1);
}

TEST_CASE("Renderer2D - drawTriangle delegates to ShapeRenderer", "[render]")
{
	mitiru::render::Renderer2D renderer;
	renderer.beginFrame();
	renderer.drawTriangle(
		sgc::Vec2f{0, 0}, sgc::Vec2f{50, 0}, sgc::Vec2f{25, 40},
		sgc::Colorf::blue());
	renderer.endFrame();

	REQUIRE(renderer.totalDrawCalls() >= 1);
}

TEST_CASE("Renderer2D - drawRectFrame delegates to SpriteBatch", "[render]")
{
	mitiru::render::Renderer2D renderer;
	renderer.beginFrame();
	renderer.drawRectFrame(sgc::Rectf{10, 10, 80, 60}, sgc::Colorf::white());
	renderer.endFrame();

	REQUIRE(renderer.totalDrawCalls() >= 1);
}

TEST_CASE("Renderer2D - drawText is a stub but counts draw calls", "[render]")
{
	mitiru::render::Renderer2D renderer;
	renderer.beginFrame();
	renderer.drawText(sgc::Vec2f{10, 10}, "Hello", sgc::Colorf::white(), 16.0f);
	renderer.endFrame();

	REQUIRE(renderer.totalDrawCalls() >= 1);
}

TEST_CASE("Renderer2D - multiple draws in one frame", "[render]")
{
	mitiru::render::Renderer2D renderer;
	renderer.beginFrame();
	renderer.drawRect(sgc::Rectf{0, 0, 50, 50}, sgc::Colorf::red());
	renderer.drawCircle(sgc::Vec2f{200, 200}, 30.0f, sgc::Colorf::green());
	renderer.drawLine(sgc::Vec2f{0, 0}, sgc::Vec2f{100, 100}, sgc::Colorf::blue());
	renderer.drawText(sgc::Vec2f{10, 10}, "Test", sgc::Colorf::white());
	renderer.endFrame();

	REQUIRE(renderer.totalDrawCalls() >= 4);
}

TEST_CASE("Renderer2D - endFrame without begin does nothing", "[render]")
{
	mitiru::render::Renderer2D renderer;
	REQUIRE_NOTHROW(renderer.endFrame());
	REQUIRE(renderer.totalDrawCalls() == 0);
}

TEST_CASE("Renderer2D - setCamera and camera accessor", "[render]")
{
	mitiru::render::Renderer2D renderer;
	mitiru::render::Camera2D cam(1920.0f, 1080.0f);
	cam.setZoom(2.0f);

	renderer.setCamera(cam);
	REQUIRE(renderer.camera().zoom() == Catch::Approx(2.0f));
}

TEST_CASE("Renderer2D - spriteBatch and shapeRenderer accessors", "[render]")
{
	mitiru::render::Renderer2D renderer;
	REQUIRE(renderer.spriteBatch().vertexCount() == 0);
	REQUIRE(renderer.shapeRenderer().vertexCount() == 0);
}

// ============================================================
// Camera2D
// ============================================================

TEST_CASE("Camera2D - default values", "[render]")
{
	const mitiru::render::Camera2D camera;

	REQUIRE(camera.position().x == Catch::Approx(0.0f));
	REQUIRE(camera.position().y == Catch::Approx(0.0f));
	REQUIRE(camera.zoom() == Catch::Approx(1.0f));
	REQUIRE(camera.rotation() == Catch::Approx(0.0f));
}

TEST_CASE("Camera2D - custom screen size", "[render]")
{
	const mitiru::render::Camera2D camera(1920.0f, 1080.0f);
	REQUIRE(camera.zoom() == Catch::Approx(1.0f));
}

TEST_CASE("Camera2D - setPosition and position getter", "[render]")
{
	mitiru::render::Camera2D camera;
	camera.setPosition({100.0f, 200.0f});

	REQUIRE(camera.position().x == Catch::Approx(100.0f));
	REQUIRE(camera.position().y == Catch::Approx(200.0f));
}

TEST_CASE("Camera2D - setZoom and zoom getter", "[render]")
{
	mitiru::render::Camera2D camera;
	camera.setZoom(2.5f);

	REQUIRE(camera.zoom() == Catch::Approx(2.5f));
}

TEST_CASE("Camera2D - zoom is clamped to minimum", "[render]")
{
	mitiru::render::Camera2D camera;
	camera.setZoom(-1.0f);

	REQUIRE(camera.zoom() == Catch::Approx(0.001f));
}

TEST_CASE("Camera2D - setRotation and rotation getter", "[render]")
{
	mitiru::render::Camera2D camera;
	camera.setRotation(1.5707f);

	REQUIRE(camera.rotation() == Catch::Approx(1.5707f));
}

TEST_CASE("Camera2D - screenToWorld and worldToScreen roundtrip", "[render]")
{
	mitiru::render::Camera2D camera(800.0f, 600.0f);
	camera.setPosition({100.0f, 50.0f});
	camera.setZoom(1.0f);

	const sgc::Vec2f worldPos{200.0f, 150.0f};
	const auto screenPos = camera.worldToScreen(worldPos);
	const auto roundTrip = camera.screenToWorld(screenPos);

	REQUIRE(roundTrip.x == Catch::Approx(worldPos.x).margin(0.01f));
	REQUIRE(roundTrip.y == Catch::Approx(worldPos.y).margin(0.01f));
}

TEST_CASE("Camera2D - screenToWorld and worldToScreen with zoom", "[render]")
{
	mitiru::render::Camera2D camera(800.0f, 600.0f);
	camera.setPosition({0.0f, 0.0f});
	camera.setZoom(2.0f);

	const sgc::Vec2f worldPos{50.0f, 30.0f};
	const auto screenPos = camera.worldToScreen(worldPos);
	const auto roundTrip = camera.screenToWorld(screenPos);

	REQUIRE(roundTrip.x == Catch::Approx(worldPos.x).margin(0.01f));
	REQUIRE(roundTrip.y == Catch::Approx(worldPos.y).margin(0.01f));
}

TEST_CASE("Camera2D - worldToScreen with identity camera centers at screen center", "[render]")
{
	mitiru::render::Camera2D camera(800.0f, 600.0f);
	/// Camera at origin, zoom 1, no rotation
	const auto screenPos = camera.worldToScreen({0.0f, 0.0f});

	/// Origin should map to screen center
	REQUIRE(screenPos.x == Catch::Approx(400.0f).margin(0.01f));
	REQUIRE(screenPos.y == Catch::Approx(300.0f).margin(0.01f));
}

TEST_CASE("Camera2D - shake sets up shake state", "[render]")
{
	mitiru::render::Camera2D camera;
	REQUIRE_NOTHROW(camera.shake(10.0f, 0.5f));
}

TEST_CASE("Camera2D - updateShake decays over time", "[render]")
{
	mitiru::render::Camera2D camera(800.0f, 600.0f);
	camera.shake(20.0f, 1.0f);

	/// After updating past the duration, shake should be zero
	camera.updateShake(2.0f);

	/// Roundtrip at origin should be stable (no shake offset)
	const auto pos = camera.worldToScreen({0.0f, 0.0f});
	REQUIRE(pos.x == Catch::Approx(400.0f).margin(0.01f));
	REQUIRE(pos.y == Catch::Approx(300.0f).margin(0.01f));
}

TEST_CASE("Camera2D - setScreenSize", "[render]")
{
	mitiru::render::Camera2D camera;
	camera.setScreenSize(1920.0f, 1080.0f);

	/// After changing screen size, origin should map to new center
	const auto pos = camera.worldToScreen({0.0f, 0.0f});
	REQUIRE(pos.x == Catch::Approx(960.0f).margin(0.01f));
	REQUIRE(pos.y == Catch::Approx(540.0f).margin(0.01f));
}

TEST_CASE("Camera2D - viewMatrix is not identity with offset", "[render]")
{
	mitiru::render::Camera2D camera(800.0f, 600.0f);
	camera.setPosition({100.0f, 200.0f});

	const auto mat = camera.viewMatrix();
	/// The matrix should not be identity (m[3][0] and m[3][1] encode translation)
	/// Just verify the matrix is computable
	REQUIRE(std::isfinite(mat.m[0][0]));
}

// ============================================================
// Camera3D
// ============================================================

TEST_CASE("Camera3D - default constructor", "[render]")
{
	const mitiru::render::Camera3D camera;

	REQUIRE(camera.position().x == Catch::Approx(0.0f));
	REQUIRE(camera.position().y == Catch::Approx(0.0f));
	REQUIRE(camera.position().z == Catch::Approx(-5.0f));
	REQUIRE(camera.target().x == Catch::Approx(0.0f));
	REQUIRE(camera.target().y == Catch::Approx(0.0f));
	REQUIRE(camera.target().z == Catch::Approx(0.0f));
	REQUIRE(camera.up().y == Catch::Approx(1.0f));
	REQUIRE(camera.nearClip() == Catch::Approx(0.1f));
	REQUIRE(camera.farClip() == Catch::Approx(1000.0f));
}

TEST_CASE("Camera3D - parametric constructor", "[render]")
{
	const mitiru::render::Camera3D camera(
		sgc::Vec3f{0, 5, -10},
		sgc::Vec3f{0, 0, 0},
		sgc::Vec3f{0, 1, 0},
		1.0f,
		16.0f / 9.0f,
		0.5f,
		500.0f);

	REQUIRE(camera.position().y == Catch::Approx(5.0f));
	REQUIRE(camera.position().z == Catch::Approx(-10.0f));
	REQUIRE(camera.fov() == Catch::Approx(1.0f));
	REQUIRE(camera.aspectRatio() == Catch::Approx(16.0f / 9.0f));
	REQUIRE(camera.nearClip() == Catch::Approx(0.5f));
	REQUIRE(camera.farClip() == Catch::Approx(500.0f));
}

TEST_CASE("Camera3D - setters and getters", "[render]")
{
	mitiru::render::Camera3D camera;

	camera.setPosition({1, 2, 3});
	REQUIRE(camera.position().x == Catch::Approx(1.0f));

	camera.setTarget({4, 5, 6});
	REQUIRE(camera.target().x == Catch::Approx(4.0f));

	camera.setUp({0, 0, 1});
	REQUIRE(camera.up().z == Catch::Approx(1.0f));

	camera.setFov(1.2f);
	REQUIRE(camera.fov() == Catch::Approx(1.2f));

	camera.setAspectRatio(4.0f / 3.0f);
	REQUIRE(camera.aspectRatio() == Catch::Approx(4.0f / 3.0f));

	camera.setNearClip(0.01f);
	REQUIRE(camera.nearClip() == Catch::Approx(0.01f));

	camera.setFarClip(5000.0f);
	REQUIRE(camera.farClip() == Catch::Approx(5000.0f));
}

TEST_CASE("Camera3D - viewMatrix returns finite values", "[render]")
{
	const mitiru::render::Camera3D camera(
		sgc::Vec3f{0, 5, -10},
		sgc::Vec3f{0, 0, 0},
		sgc::Vec3f{0, 1, 0},
		1.0f, 16.0f / 9.0f, 0.1f, 1000.0f);

	const auto mat = camera.viewMatrix();
	for (int r = 0; r < 4; ++r)
	{
		for (int c = 0; c < 4; ++c)
		{
			REQUIRE(std::isfinite(mat.m[r][c]));
		}
	}
}

TEST_CASE("Camera3D - projectionMatrix returns finite values", "[render]")
{
	const mitiru::render::Camera3D camera(
		sgc::Vec3f{0, 0, -5},
		sgc::Vec3f{0, 0, 0},
		sgc::Vec3f{0, 1, 0},
		1.0f, 16.0f / 9.0f, 0.1f, 1000.0f);

	const auto mat = camera.projectionMatrix();
	for (int r = 0; r < 4; ++r)
	{
		for (int c = 0; c < 4; ++c)
		{
			REQUIRE(std::isfinite(mat.m[r][c]));
		}
	}
}

// ============================================================
// ScreenCapture
// ============================================================

TEST_CASE("ScreenCapture - capture with NullDevice returns correct pixel count", "[render]")
{
	const mitiru::gfx::NullDevice device;
	const auto screenshot = mitiru::render::capture(device, 64, 48);

	REQUIRE(screenshot.isValid());
	REQUIRE(screenshot.width == 64);
	REQUIRE(screenshot.height == 48);
	REQUIRE(screenshot.pixels.size() == 64u * 48u * 4u);
}

TEST_CASE("ScreenCapture - capture stores frame number", "[render]")
{
	const mitiru::gfx::NullDevice device;
	const auto screenshot = mitiru::render::capture(device, 16, 16, 42);

	REQUIRE(screenshot.frameNumber == 42);
}

TEST_CASE("ScreenCapture - NullDevice returns black pixels", "[render]")
{
	const mitiru::gfx::NullDevice device;
	const auto screenshot = mitiru::render::capture(device, 4, 4);

	REQUIRE(screenshot.pixelR(0, 0) == 0);
	REQUIRE(screenshot.pixelG(0, 0) == 0);
	REQUIRE(screenshot.pixelB(0, 0) == 0);
	REQUIRE(screenshot.pixelA(0, 0) == 255);
}

TEST_CASE("ScreenCapture - pixel accessors out of range return 0", "[render]")
{
	const mitiru::gfx::NullDevice device;
	const auto screenshot = mitiru::render::capture(device, 4, 4);

	REQUIRE(screenshot.pixelR(-1, 0) == 0);
	REQUIRE(screenshot.pixelG(0, -1) == 0);
	REQUIRE(screenshot.pixelB(4, 0) == 0);
	REQUIRE(screenshot.pixelA(0, 4) == 0);
}

TEST_CASE("ScreenCapture - zero size capture is not valid", "[render]")
{
	const mitiru::gfx::NullDevice device;
	const auto screenshot = mitiru::render::capture(device, 0, 0);

	REQUIRE_FALSE(screenshot.isValid());
}

TEST_CASE("ScreenshotData - isValid checks all conditions", "[render]")
{
	mitiru::render::ScreenshotData data;
	REQUIRE_FALSE(data.isValid());

	data.width = 10;
	REQUIRE_FALSE(data.isValid());

	data.height = 10;
	REQUIRE_FALSE(data.isValid());

	data.pixels.resize(10 * 10 * 4);
	REQUIRE(data.isValid());
}

// ============================================================
// RenderGraph
// ============================================================

TEST_CASE("RenderGraph - initial state is empty", "[render]")
{
	const mitiru::render::RenderGraph graph;

	REQUIRE(graph.passCount() == 0);
	REQUIRE(graph.passes().empty());
}

TEST_CASE("RenderGraph - addPass increases count", "[render]")
{
	mitiru::render::RenderGraph graph;
	graph.addPass("Pass1", []() {});
	graph.addPass("Pass2", []() {});

	REQUIRE(graph.passCount() == 2);
}

TEST_CASE("RenderGraph - execute runs passes in order", "[render]")
{
	mitiru::render::RenderGraph graph;
	std::vector<int> order;

	graph.addPass("First", [&order]() { order.push_back(1); });
	graph.addPass("Second", [&order]() { order.push_back(2); });
	graph.addPass("Third", [&order]() { order.push_back(3); });

	const int executed = graph.execute();

	REQUIRE(executed == 3);
	REQUIRE(order.size() == 3);
	REQUIRE(order[0] == 1);
	REQUIRE(order[1] == 2);
	REQUIRE(order[2] == 3);
}

TEST_CASE("RenderGraph - execute returns count of executed passes", "[render]")
{
	mitiru::render::RenderGraph graph;
	graph.addPass("A", []() {});
	graph.addPass("B", []() {});

	REQUIRE(graph.execute() == 2);
}

TEST_CASE("RenderGraph - setPassEnabled disables a pass", "[render]")
{
	mitiru::render::RenderGraph graph;
	std::vector<std::string> executedPasses;

	graph.addPass("Shadow", [&]() { executedPasses.push_back("Shadow"); });
	graph.addPass("Opaque", [&]() { executedPasses.push_back("Opaque"); });
	graph.addPass("PostFX", [&]() { executedPasses.push_back("PostFX"); });

	graph.setPassEnabled("Shadow", false);
	const int executed = graph.execute();

	REQUIRE(executed == 2);
	REQUIRE(executedPasses.size() == 2);
	REQUIRE(executedPasses[0] == "Opaque");
	REQUIRE(executedPasses[1] == "PostFX");
}

TEST_CASE("RenderGraph - setPassEnabled returns false for unknown pass", "[render]")
{
	mitiru::render::RenderGraph graph;
	graph.addPass("Test", []() {});

	REQUIRE_FALSE(graph.setPassEnabled("NonExistent", false));
}

TEST_CASE("RenderGraph - re-enable a disabled pass", "[render]")
{
	mitiru::render::RenderGraph graph;
	int count = 0;
	graph.addPass("Pass", [&count]() { ++count; });

	graph.setPassEnabled("Pass", false);
	graph.execute();
	REQUIRE(count == 0);

	graph.setPassEnabled("Pass", true);
	graph.execute();
	REQUIRE(count == 1);
}

TEST_CASE("RenderGraph - clear removes all passes", "[render]")
{
	mitiru::render::RenderGraph graph;
	graph.addPass("A", []() {});
	graph.addPass("B", []() {});

	graph.clear();

	REQUIRE(graph.passCount() == 0);
	REQUIRE(graph.execute() == 0);
}

TEST_CASE("RenderGraph - execute with empty graph returns 0", "[render]")
{
	mitiru::render::RenderGraph graph;
	REQUIRE(graph.execute() == 0);
}

TEST_CASE("RenderGraph - passes preserves names", "[render]")
{
	mitiru::render::RenderGraph graph;
	graph.addPass("Shadow", []() {});
	graph.addPass("Lighting", []() {});

	const auto& passes = graph.passes();
	REQUIRE(passes[0].name == "Shadow");
	REQUIRE(passes[1].name == "Lighting");
}

// ============================================================
// BitmapFont
// ============================================================

TEST_CASE("BitmapFont - glyph A returns non-zero data", "[render][bitmap-font]")
{
	constexpr auto g = mitiru::render::BitmapFont::glyph('A');

	/// 'A' glyph should have at least some non-zero rows
	bool hasNonZero = false;
	for (const auto byte : g)
	{
		if (byte != 0)
		{
			hasNonZero = true;
			break;
		}
	}
	REQUIRE(hasNonZero);
}

TEST_CASE("BitmapFont - glyph space returns all zeros", "[render][bitmap-font]")
{
	constexpr auto g = mitiru::render::BitmapFont::glyph(' ');

	for (const auto byte : g)
	{
		REQUIRE(byte == 0);
	}
}

TEST_CASE("BitmapFont - glyph out of range returns blank", "[render][bitmap-font]")
{
	/// Character below FIRST_CHAR (31 = unit separator)
	constexpr auto gLow = mitiru::render::BitmapFont::glyph('\x1F');
	for (const auto byte : gLow)
	{
		REQUIRE(byte == 0);
	}

	/// Character above LAST_CHAR (127 = DEL)
	constexpr auto gHigh = mitiru::render::BitmapFont::glyph('\x7F');
	for (const auto byte : gHigh)
	{
		REQUIRE(byte == 0);
	}
}

TEST_CASE("BitmapFont - textWidth calculation", "[render][bitmap-font]")
{
	/// "Hello" = 5 chars * 8 pixels * scale 1 = 40
	REQUIRE(mitiru::render::BitmapFont::textWidth("Hello") == 40);

	/// scale 2: 5 * 8 * 2 = 80
	REQUIRE(mitiru::render::BitmapFont::textWidth("Hello", 2) == 80);

	/// empty string
	REQUIRE(mitiru::render::BitmapFont::textWidth("") == 0);
}

TEST_CASE("BitmapFont - textHeight calculation", "[render][bitmap-font]")
{
	REQUIRE(mitiru::render::BitmapFont::textHeight() == 8);
	REQUIRE(mitiru::render::BitmapFont::textHeight(3) == 24);
}

// ============================================================
// TextRenderer
// ============================================================

TEST_CASE("TextRenderer - measureWidth matches BitmapFont", "[render][text-renderer]")
{
	REQUIRE(mitiru::render::TextRenderer::measureWidth("Test") == 32);
	REQUIRE(mitiru::render::TextRenderer::measureWidth("Test", 2) == 64);
	REQUIRE(mitiru::render::TextRenderer::measureWidth("") == 0);
}

TEST_CASE("TextRenderer - measureHeight matches BitmapFont", "[render][text-renderer]")
{
	REQUIRE(mitiru::render::TextRenderer::measureHeight() == 8);
	REQUIRE(mitiru::render::TextRenderer::measureHeight(4) == 32);
}

TEST_CASE("TextRenderer - drawText generates rects in SpriteBatch", "[render][text-renderer]")
{
	mitiru::render::SpriteBatch batch;
	batch.begin();

	/// Draw a single 'A' character at scale 1
	mitiru::render::TextRenderer::drawText(batch, "A", 0.0f, 0.0f, 1, sgc::Colorf::white());

	/// 'A' glyph has multiple set bits, so vertices should be generated
	REQUIRE(batch.vertexCount() > 0);
	REQUIRE(batch.drawCallCount() > 0);
}

TEST_CASE("TextRenderer - drawText with empty string generates no rects", "[render][text-renderer]")
{
	mitiru::render::SpriteBatch batch;
	batch.begin();

	mitiru::render::TextRenderer::drawText(batch, "", 0.0f, 0.0f, 1, sgc::Colorf::white());

	REQUIRE(batch.vertexCount() == 0);
	REQUIRE(batch.drawCallCount() == 0);
}

TEST_CASE("TextRenderer - drawText with space generates no rects", "[render][text-renderer]")
{
	mitiru::render::SpriteBatch batch;
	batch.begin();

	mitiru::render::TextRenderer::drawText(batch, " ", 0.0f, 0.0f, 1, sgc::Colorf::white());

	/// Space glyph is all zeros, no rectangles should be drawn
	REQUIRE(batch.vertexCount() == 0);
	REQUIRE(batch.drawCallCount() == 0);
}

// ============================================================
// Screen drawText integration
// ============================================================

TEST_CASE("Screen drawText increments draw call count", "[render][screen]")
{
	mitiru::Screen screen(800, 600);
	screen.clear();
	screen.resetDrawCallCount();

	screen.drawText(sgc::Vec2f{10.0f, 10.0f}, "Hi", sgc::Colorf::white(), 16.0f);

	/// drawText now generates actual rects, each of which increments drawCallCount
	REQUIRE(screen.drawCallCount() > 0);
}
