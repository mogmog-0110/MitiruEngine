/// @file TestDxLibRenderer.cpp
/// @brief DxLibRenderer adapter tests with stub

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/dxlib/DxLibRenderer.hpp"

using namespace sgc;
using namespace sgc::dxlib;
using namespace dxlib_stub;
using Catch::Approx;

// ── テスト前にスタブをリセット ─────────────────────────

namespace
{

struct ResetFixture
{
	ResetFixture() { dxlib_stub::reset(); }
};

} // anonymous namespace

// ── drawRect ─────────────────────────────────────────────

TEST_CASE("DxLibRenderer drawRect records filled BoxAA", "[dxlib][renderer]")
{
	ResetFixture fix;
	DxLibRenderer renderer(800, 600);

	AABB2f rect{{10.0f, 20.0f}, {110.0f, 70.0f}};
	renderer.drawRect(rect, Colorf::red());

	REQUIRE(drawCalls().size() == 1);
	REQUIRE(drawCalls()[0].type == DrawType::BoxAA);
	REQUIRE(drawCalls()[0].params[0] == Approx(10.0f));
	REQUIRE(drawCalls()[0].params[1] == Approx(20.0f));
	REQUIRE(drawCalls()[0].params[2] == Approx(110.0f));
	REQUIRE(drawCalls()[0].params[3] == Approx(70.0f));
	REQUIRE(drawCalls()[0].fillFlag == TRUE);
}

// ── drawRectFrame ────────────────────────────────────────

TEST_CASE("DxLibRenderer drawRectFrame thin uses non-filled BoxAA", "[dxlib][renderer]")
{
	ResetFixture fix;
	DxLibRenderer renderer(800, 600);

	AABB2f rect{{0.0f, 0.0f}, {100.0f, 50.0f}};
	renderer.drawRectFrame(rect, 1.0f, Colorf::white());

	REQUIRE(drawCalls().size() == 1);
	REQUIRE(drawCalls()[0].type == DrawType::BoxAA);
	REQUIRE(drawCalls()[0].fillFlag == FALSE);
}

TEST_CASE("DxLibRenderer drawRectFrame thick uses 4 filled BoxAA", "[dxlib][renderer]")
{
	ResetFixture fix;
	DxLibRenderer renderer(800, 600);

	AABB2f rect{{0.0f, 0.0f}, {100.0f, 50.0f}};
	renderer.drawRectFrame(rect, 3.0f, Colorf::white());

	REQUIRE(drawCalls().size() == 4);
	for (const auto& call : drawCalls())
	{
		REQUIRE(call.type == DrawType::BoxAA);
		REQUIRE(call.fillFlag == TRUE);
	}
}

// ── drawCircle ───────────────────────────────────────────

TEST_CASE("DxLibRenderer drawCircle records filled CircleAA", "[dxlib][renderer]")
{
	ResetFixture fix;
	DxLibRenderer renderer(800, 600);

	renderer.drawCircle({100.0f, 200.0f}, 30.0f, Colorf::blue());

	REQUIRE(drawCalls().size() == 1);
	REQUIRE(drawCalls()[0].type == DrawType::CircleAA);
	REQUIRE(drawCalls()[0].params[0] == Approx(100.0f));
	REQUIRE(drawCalls()[0].params[1] == Approx(200.0f));
	REQUIRE(drawCalls()[0].params[2] == Approx(30.0f));
	REQUIRE(drawCalls()[0].fillFlag == TRUE);
}

// ── drawCircleFrame ──────────────────────────────────────

TEST_CASE("DxLibRenderer drawCircleFrame records non-filled CircleAA", "[dxlib][renderer]")
{
	ResetFixture fix;
	DxLibRenderer renderer(800, 600);

	renderer.drawCircleFrame({50.0f, 50.0f}, 20.0f, 2.0f, Colorf::green());

	REQUIRE(drawCalls().size() == 1);
	REQUIRE(drawCalls()[0].type == DrawType::CircleAA);
	REQUIRE(drawCalls()[0].fillFlag == FALSE);
}

// ── drawLine ─────────────────────────────────────────────

TEST_CASE("DxLibRenderer drawLine records LineAA", "[dxlib][renderer]")
{
	ResetFixture fix;
	DxLibRenderer renderer(800, 600);

	renderer.drawLine({0.0f, 0.0f}, {100.0f, 100.0f}, 2.0f, Colorf::white());

	REQUIRE(drawCalls().size() == 1);
	REQUIRE(drawCalls()[0].type == DrawType::LineAA);
	REQUIRE(drawCalls()[0].params[0] == Approx(0.0f));
	REQUIRE(drawCalls()[0].params[1] == Approx(0.0f));
	REQUIRE(drawCalls()[0].params[2] == Approx(100.0f));
	REQUIRE(drawCalls()[0].params[3] == Approx(100.0f));
}

// ── drawTriangle ─────────────────────────────────────────

TEST_CASE("DxLibRenderer drawTriangle records filled TriangleAA", "[dxlib][renderer]")
{
	ResetFixture fix;
	DxLibRenderer renderer(800, 600);

	renderer.drawTriangle(
		{0.0f, 0.0f}, {50.0f, 100.0f}, {100.0f, 0.0f}, Colorf::white());

	REQUIRE(drawCalls().size() == 1);
	REQUIRE(drawCalls()[0].type == DrawType::TriangleAA);
	REQUIRE(drawCalls()[0].fillFlag == TRUE);
}

// ── drawFadeOverlay ──────────────────────────────────────

TEST_CASE("DxLibRenderer drawFadeOverlay records blend + box + unblend", "[dxlib][renderer]")
{
	ResetFixture fix;
	DxLibRenderer renderer(800, 600);

	renderer.drawFadeOverlay(0.5f);

	REQUIRE(drawCalls().size() == 3);
	REQUIRE(drawCalls()[0].type == DrawType::BlendMode);
	REQUIRE(drawCalls()[1].type == DrawType::Box);
	REQUIRE(drawCalls()[2].type == DrawType::BlendMode);
}

TEST_CASE("DxLibRenderer drawFadeOverlay with zero alpha skips draw", "[dxlib][renderer]")
{
	ResetFixture fix;
	DxLibRenderer renderer(800, 600);

	renderer.drawFadeOverlay(0.0f);

	REQUIRE(drawCalls().empty());
}

// ── clearBackground ──────────────────────────────────────

TEST_CASE("DxLibRenderer clearBackground records filled Box", "[dxlib][renderer]")
{
	ResetFixture fix;
	DxLibRenderer renderer(800, 600);

	renderer.clearBackground(Colorf::black());

	REQUIRE(drawCalls().size() == 1);
	REQUIRE(drawCalls()[0].type == DrawType::Box);
	REQUIRE(drawCalls()[0].fillFlag == TRUE);
}
