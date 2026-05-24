/// @file TestDxLibTextRenderer.cpp
/// @brief DxLibTextRenderer adapter tests with stub

#include <catch2/catch_test_macros.hpp>

#include "sgc/dxlib/DxLibTextRenderer.hpp"

using namespace sgc;
using namespace sgc::dxlib;

// ── テスト前にスタブをリセット ─────────────────────────

namespace
{

struct ResetFixture
{
	ResetFixture() { dxlib_stub::reset(); }
};

} // anonymous namespace

// ── registerFont ─────────────────────────────────────────

TEST_CASE("DxLibTextRenderer registerFont creates font handle", "[dxlib][text]")
{
	ResetFixture fix;
	DxLibTextRenderer renderer;
	renderer.registerFont(24);

	REQUIRE(dxlib_stub::fontRecords().size() == 1);
	REQUIRE(dxlib_stub::fontRecords()[0].size == 24);
	REQUIRE_FALSE(dxlib_stub::fontRecords()[0].deleted);
}

TEST_CASE("DxLibTextRenderer registerFont overwrites existing size", "[dxlib][text]")
{
	ResetFixture fix;
	DxLibTextRenderer renderer;
	renderer.registerFont(24);
	renderer.registerFont(24, "TestFont");

	// 最初のハンドルは削除済み、2つ目が有効
	REQUIRE(dxlib_stub::fontRecords().size() == 2);
	REQUIRE(dxlib_stub::fontRecords()[0].deleted);
	REQUIRE_FALSE(dxlib_stub::fontRecords()[1].deleted);
}

// ── destructor ──────────────────────────────────────────

TEST_CASE("DxLibTextRenderer destructor deletes all font handles", "[dxlib][text]")
{
	ResetFixture fix;
	{
		DxLibTextRenderer renderer;
		renderer.registerFont(24);
		renderer.registerFont(60);
	}

	// 全フォントが削除済み
	for (const auto& rec : dxlib_stub::fontRecords())
	{
		REQUIRE(rec.deleted);
	}
}

// ── drawText ────────────────────────────────────────────

TEST_CASE("DxLibTextRenderer drawText records draw call", "[dxlib][text]")
{
	ResetFixture fix;
	DxLibTextRenderer renderer;
	renderer.registerFont(24);

	renderer.drawText("Hello", 24.0f, Vec2f{10.0f, 20.0f}, Colorf::white());

	// 描画コールが記録されていること
	bool found = false;
	for (const auto& call : dxlib_stub::drawCalls())
	{
		if (call.type == dxlib_stub::DrawType::StringToHandle)
		{
			REQUIRE(call.params[0] == 10.0f);  // x
			REQUIRE(call.params[1] == 20.0f);  // y
			found = true;
			break;
		}
	}
	REQUIRE(found);
}

TEST_CASE("DxLibTextRenderer drawText throws for unregistered size", "[dxlib][text]")
{
	ResetFixture fix;
	DxLibTextRenderer renderer;
	renderer.registerFont(24);

	REQUIRE_THROWS_AS(
		renderer.drawText("Test", 60.0f, Vec2f{}, Colorf::white()),
		std::out_of_range);
}

// ── drawTextCentered ────────────────────────────────────

TEST_CASE("DxLibTextRenderer drawTextCentered centers text", "[dxlib][text]")
{
	ResetFixture fix;
	DxLibTextRenderer renderer;
	renderer.registerFont(24);

	renderer.drawTextCentered("ABCD", 24.0f, Vec2f{400.0f, 300.0f}, Colorf::white());

	// "ABCD" = 4文字、stub幅 = 4*12 = 48px
	// x = 400 - 48/2 = 376
	// y = 300 - 24/2 = 288
	bool found = false;
	for (const auto& call : dxlib_stub::drawCalls())
	{
		if (call.type == dxlib_stub::DrawType::StringToHandle)
		{
			REQUIRE(call.params[0] == 376.0f);  // x
			REQUIRE(call.params[1] == 288.0f);  // y
			found = true;
			break;
		}
	}
	REQUIRE(found);
}

TEST_CASE("DxLibTextRenderer drawTextCentered throws for unregistered size", "[dxlib][text]")
{
	ResetFixture fix;
	DxLibTextRenderer renderer;

	REQUIRE_THROWS_AS(
		renderer.drawTextCentered("Test", 30.0f, Vec2f{}, Colorf::white()),
		std::out_of_range);
}

// ── measure (ITextMeasure) ──────────────────────────────

TEST_CASE("DxLibTextRenderer measure returns width and height", "[dxlib][text]")
{
	ResetFixture fix;
	DxLibTextRenderer renderer;
	renderer.registerFont(24);

	// "Hello" = 5文字、stub幅 = 5*12 = 60px、高さ = 24
	const Vec2f size = renderer.measure("Hello", 24.0f);
	REQUIRE(size.x == 60.0f);
	REQUIRE(size.y == 24.0f);
}

TEST_CASE("DxLibTextRenderer measure with nearest font fallback", "[dxlib][text]")
{
	ResetFixture fix;
	DxLibTextRenderer renderer;
	renderer.registerFont(24);
	renderer.registerFont(60);

	// サイズ30を要求→最近接は24（差6 < 差30）
	const Vec2f size = renderer.measure("AB", 30.0f);
	REQUIRE(size.x == 24.0f);
	REQUIRE(size.y == 24.0f);
}

TEST_CASE("DxLibTextRenderer measure throws when no fonts registered", "[dxlib][text]")
{
	ResetFixture fix;
	DxLibTextRenderer renderer;

	REQUIRE_THROWS_AS(renderer.measure("Test", 24.0f), std::out_of_range);
}

// ── lineHeight (ITextMeasure) ───────────────────────────

TEST_CASE("DxLibTextRenderer lineHeight returns font size", "[dxlib][text]")
{
	ResetFixture fix;
	DxLibTextRenderer renderer;
	renderer.registerFont(24);

	REQUIRE(renderer.lineHeight(24.0f) == 24.0f);
}

TEST_CASE("DxLibTextRenderer lineHeight uses nearest size", "[dxlib][text]")
{
	ResetFixture fix;
	DxLibTextRenderer renderer;
	renderer.registerFont(24);
	renderer.registerFont(60);

	// サイズ50を要求→最近接は60（差10 < 差26）
	REQUIRE(renderer.lineHeight(50.0f) == 60.0f);
}

// ── multiple fonts ──────────────────────────────────────

TEST_CASE("DxLibTextRenderer manages multiple font sizes", "[dxlib][text]")
{
	ResetFixture fix;
	DxLibTextRenderer renderer;
	renderer.registerFont(24);
	renderer.registerFont(60);

	// 両方のサイズで描画できること
	REQUIRE_NOTHROW(
		renderer.drawText("small", 24.0f, Vec2f{}, Colorf::white()));
	REQUIRE_NOTHROW(
		renderer.drawText("large", 60.0f, Vec2f{}, Colorf::white()));
}
