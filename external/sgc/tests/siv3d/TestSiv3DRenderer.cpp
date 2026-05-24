/// @file TestSiv3DRenderer.cpp
/// @brief Siv3DRenderer adapter tests with stub

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/siv3d/Siv3DRenderer.hpp"

using namespace sgc;
using namespace sgc::siv3d;
using namespace siv3d_stub;
using Catch::Approx;

// ── テスト前にスタブをリセット ─────────────────────────

namespace
{

struct ResetFixture
{
	ResetFixture() { siv3d_stub::reset(); }
};

} // anonymous namespace

// ── drawRect ─────────────────────────────────────────────

TEST_CASE("Siv3DRenderer drawRect records RectFill", "[siv3d][renderer]")
{
	ResetFixture fix;
	Siv3DRenderer renderer;

	AABB2f rect{{10.0f, 20.0f}, {110.0f, 70.0f}};
	renderer.drawRect(rect, Colorf::red());

	REQUIRE(drawCalls().size() == 1);
	REQUIRE(drawCalls()[0].type == DrawType::RectFill);
	REQUIRE(drawCalls()[0].params[0] == Approx(10.0));
	REQUIRE(drawCalls()[0].params[1] == Approx(20.0));
	REQUIRE(drawCalls()[0].params[2] == Approx(100.0));  // width
	REQUIRE(drawCalls()[0].params[3] == Approx(50.0));   // height
	REQUIRE(drawCalls()[0].r == Approx(1.0));
	REQUIRE(drawCalls()[0].g == Approx(0.0));
	REQUIRE(drawCalls()[0].b == Approx(0.0));
}

// ── drawRectFrame ────────────────────────────────────────

TEST_CASE("Siv3DRenderer drawRectFrame records RectFrame", "[siv3d][renderer]")
{
	ResetFixture fix;
	Siv3DRenderer renderer;

	AABB2f rect{{0.0f, 0.0f}, {100.0f, 50.0f}};
	renderer.drawRectFrame(rect, 2.0f, Colorf::white());

	REQUIRE(drawCalls().size() == 1);
	REQUIRE(drawCalls()[0].type == DrawType::RectFrame);
	REQUIRE(drawCalls()[0].params[4] == Approx(2.0));  // thickness
}

// ── drawCircle ───────────────────────────────────────────

TEST_CASE("Siv3DRenderer drawCircle records CircleFill", "[siv3d][renderer]")
{
	ResetFixture fix;
	Siv3DRenderer renderer;

	renderer.drawCircle({100.0f, 200.0f}, 30.0f, Colorf::blue());

	REQUIRE(drawCalls().size() == 1);
	REQUIRE(drawCalls()[0].type == DrawType::CircleFill);
	REQUIRE(drawCalls()[0].params[0] == Approx(100.0));
	REQUIRE(drawCalls()[0].params[1] == Approx(200.0));
	REQUIRE(drawCalls()[0].params[2] == Approx(30.0));
}

// ── drawCircleFrame ──────────────────────────────────────

TEST_CASE("Siv3DRenderer drawCircleFrame records CircleFrame", "[siv3d][renderer]")
{
	ResetFixture fix;
	Siv3DRenderer renderer;

	renderer.drawCircleFrame({50.0f, 50.0f}, 20.0f, 2.0f, Colorf::green());

	REQUIRE(drawCalls().size() == 1);
	REQUIRE(drawCalls()[0].type == DrawType::CircleFrame);
	REQUIRE(drawCalls()[0].params[2] == Approx(20.0));  // radius
	REQUIRE(drawCalls()[0].params[3] == Approx(2.0));   // thickness
}

// ── drawLine ─────────────────────────────────────────────

TEST_CASE("Siv3DRenderer drawLine records Line", "[siv3d][renderer]")
{
	ResetFixture fix;
	Siv3DRenderer renderer;

	renderer.drawLine({0.0f, 0.0f}, {100.0f, 100.0f}, 2.0f, Colorf::white());

	REQUIRE(drawCalls().size() == 1);
	REQUIRE(drawCalls()[0].type == DrawType::Line);
	REQUIRE(drawCalls()[0].params[0] == Approx(0.0));
	REQUIRE(drawCalls()[0].params[1] == Approx(0.0));
	REQUIRE(drawCalls()[0].params[2] == Approx(100.0));
	REQUIRE(drawCalls()[0].params[3] == Approx(100.0));
	REQUIRE(drawCalls()[0].params[4] == Approx(2.0));  // thickness
}

// ── drawTriangle ─────────────────────────────────────────

TEST_CASE("Siv3DRenderer drawTriangle records TriangleFill", "[siv3d][renderer]")
{
	ResetFixture fix;
	Siv3DRenderer renderer;

	renderer.drawTriangle(
		{0.0f, 0.0f}, {50.0f, 100.0f}, {100.0f, 0.0f}, Colorf::white());

	REQUIRE(drawCalls().size() == 1);
	REQUIRE(drawCalls()[0].type == DrawType::TriangleFill);
	REQUIRE(drawCalls()[0].params[0] == Approx(0.0));
	REQUIRE(drawCalls()[0].params[1] == Approx(0.0));
	REQUIRE(drawCalls()[0].params[2] == Approx(50.0));
	REQUIRE(drawCalls()[0].params[3] == Approx(100.0));
	REQUIRE(drawCalls()[0].params[4] == Approx(100.0));
	REQUIRE(drawCalls()[0].params[5] == Approx(0.0));
}

// ── drawFadeOverlay ──────────────────────────────────────

TEST_CASE("Siv3DRenderer drawFadeOverlay records SceneRect draw", "[siv3d][renderer]")
{
	ResetFixture fix;
	Siv3DRenderer renderer;

	renderer.drawFadeOverlay(0.5f);

	REQUIRE(drawCalls().size() == 1);
	REQUIRE(drawCalls()[0].type == DrawType::SceneRectFill);
	REQUIRE(drawCalls()[0].a == Approx(0.5));
}

TEST_CASE("Siv3DRenderer drawFadeOverlay with zero alpha still draws", "[siv3d][renderer]")
{
	ResetFixture fix;
	Siv3DRenderer renderer;

	renderer.drawFadeOverlay(0.0f);

	// Siv3DRenderer always calls Scene::Rect().draw() regardless of alpha
	REQUIRE(drawCalls().size() == 1);
}

TEST_CASE("Siv3DRenderer drawFadeOverlay with custom color", "[siv3d][renderer]")
{
	ResetFixture fix;
	Siv3DRenderer renderer;

	renderer.drawFadeOverlay(0.8f, Colorf{1.0f, 0.0f, 0.0f, 1.0f});

	REQUIRE(drawCalls().size() == 1);
	REQUIRE(drawCalls()[0].r == Approx(1.0));
	REQUIRE(drawCalls()[0].g == Approx(0.0));
	REQUIRE(drawCalls()[0].b == Approx(0.0));
	REQUIRE(drawCalls()[0].a == Approx(0.8));
}

// ── clearBackground ──────────────────────────────────────

TEST_CASE("Siv3DRenderer clearBackground records SetBackground", "[siv3d][renderer]")
{
	ResetFixture fix;
	Siv3DRenderer renderer;

	renderer.clearBackground(Colorf{0.1f, 0.2f, 0.3f, 1.0f});

	REQUIRE(drawCalls().size() == 1);
	REQUIRE(drawCalls()[0].type == DrawType::SetBackground);
	REQUIRE(drawCalls()[0].r == Approx(0.1));
	REQUIRE(drawCalls()[0].g == Approx(0.2));
	REQUIRE(drawCalls()[0].b == Approx(0.3));
}

// ── multiple draw calls ──────────────────────────────────

TEST_CASE("Siv3DRenderer multiple draw calls accumulate", "[siv3d][renderer]")
{
	ResetFixture fix;
	Siv3DRenderer renderer;

	AABB2f rect{{0.0f, 0.0f}, {100.0f, 50.0f}};
	renderer.drawRect(rect, Colorf::red());
	renderer.drawCircle({50.0f, 50.0f}, 10.0f, Colorf::blue());
	renderer.drawLine({0.0f, 0.0f}, {100.0f, 100.0f}, 1.0f, Colorf::white());

	REQUIRE(drawCalls().size() == 3);
	REQUIRE(drawCalls()[0].type == DrawType::RectFill);
	REQUIRE(drawCalls()[1].type == DrawType::CircleFill);
	REQUIRE(drawCalls()[2].type == DrawType::Line);
}
