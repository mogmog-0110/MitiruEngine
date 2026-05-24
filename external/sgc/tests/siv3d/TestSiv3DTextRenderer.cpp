/// @file TestSiv3DTextRenderer.cpp
/// @brief Siv3DTextRenderer adapter tests with stub

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/siv3d/Siv3DTextRenderer.hpp"

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

// ── registerFont ──────────────────────────────────────────

TEST_CASE("Siv3DTextRenderer registerFont creates font record", "[siv3d][textrenderer]")
{
	ResetFixture fix;
	Siv3DTextRenderer renderer;

	renderer.registerFont(24, s3d::Typeface::Regular);

	REQUIRE(fontRecords().size() == 1);
	REQUIRE(fontRecords()[0].size == 24);
	REQUIRE(fontRecords()[0].typeface == static_cast<int>(s3d::Typeface::Regular));
}

TEST_CASE("Siv3DTextRenderer registerFont with multiple sizes", "[siv3d][textrenderer]")
{
	ResetFixture fix;
	Siv3DTextRenderer renderer;

	renderer.registerFont(12);
	renderer.registerFont(24);
	renderer.registerFont(48, s3d::Typeface::Heavy);

	REQUIRE(fontRecords().size() == 3);
	REQUIRE(fontRecords()[2].size == 48);
	REQUIRE(fontRecords()[2].typeface == static_cast<int>(s3d::Typeface::Heavy));
}

TEST_CASE("Siv3DTextRenderer registerFont replaces same size", "[siv3d][textrenderer]")
{
	ResetFixture fix;
	Siv3DTextRenderer renderer;

	renderer.registerFont(24, s3d::Typeface::Regular);
	renderer.registerFont(24, s3d::Typeface::Heavy);

	// insert_or_assign replaces; but both Font constructions are recorded
	REQUIRE(fontRecords().size() == 2);
}

// ── drawText ──────────────────────────────────────────────

TEST_CASE("Siv3DTextRenderer drawText records FontDraw", "[siv3d][textrenderer]")
{
	ResetFixture fix;
	Siv3DTextRenderer renderer;
	renderer.registerFont(24);

	ITextRenderer& iface = renderer;
	iface.drawText("Hello", 24.0f, {10.0f, 20.0f}, Colorf::white());

	// fontRecordsの後のdrawCallsをチェック
	bool found = false;
	for (const auto& call : drawCalls())
	{
		if (call.type == DrawType::FontDraw)
		{
			REQUIRE(call.params[0] == Approx(10.0));
			REQUIRE(call.params[1] == Approx(20.0));
			REQUIRE(call.params[2] == Approx(24.0));
			found = true;
		}
	}
	REQUIRE(found);
}

TEST_CASE("Siv3DTextRenderer drawText throws for unregistered font", "[siv3d][textrenderer]")
{
	ResetFixture fix;
	Siv3DTextRenderer renderer;

	ITextRenderer& iface = renderer;
	REQUIRE_THROWS_AS(
		iface.drawText("Hello", 24.0f, {0.0f, 0.0f}, Colorf::white()),
		std::out_of_range
	);
}

// ── drawTextCentered ──────────────────────────────────────

TEST_CASE("Siv3DTextRenderer drawTextCentered records FontDrawAt", "[siv3d][textrenderer]")
{
	ResetFixture fix;
	Siv3DTextRenderer renderer;
	renderer.registerFont(36);

	ITextRenderer& iface = renderer;
	iface.drawTextCentered("Title", 36.0f, {400.0f, 300.0f}, Colorf::white());

	bool found = false;
	for (const auto& call : drawCalls())
	{
		if (call.type == DrawType::FontDrawAt)
		{
			REQUIRE(call.params[0] == Approx(400.0));
			REQUIRE(call.params[1] == Approx(300.0));
			REQUIRE(call.params[2] == Approx(36.0));
			found = true;
		}
	}
	REQUIRE(found);
}

// ── measure (ITextMeasure) ───────────────────────────────

TEST_CASE("Siv3DTextRenderer measure returns text size", "[siv3d][textrenderer]")
{
	ResetFixture fix;
	Siv3DTextRenderer renderer;
	renderer.registerFont(20);

	ITextMeasure& measure = renderer;
	const Vec2f size = measure.measure("Test", 20.0f);

	// スタブの近似値: 文字数 * fontSize * 0.6 幅、fontSize 高さ
	REQUIRE(size.x > 0.0f);
	REQUIRE(size.y == Approx(20.0f));
}

TEST_CASE("Siv3DTextRenderer measure with exact font match", "[siv3d][textrenderer]")
{
	ResetFixture fix;
	Siv3DTextRenderer renderer;
	renderer.registerFont(16);
	renderer.registerFont(32);

	ITextMeasure& measure = renderer;
	const Vec2f size = measure.measure("AB", 32.0f);

	// 2文字 * 32 * 0.6 = 38.4 幅、32 高さ
	REQUIRE(size.x == Approx(38.4f).margin(0.1f));
	REQUIRE(size.y == Approx(32.0f));
}

// ── lineHeight ───────────────────────────────────────────

TEST_CASE("Siv3DTextRenderer lineHeight returns font height", "[siv3d][textrenderer]")
{
	ResetFixture fix;
	Siv3DTextRenderer renderer;
	renderer.registerFont(24);

	ITextMeasure& measure = renderer;
	const float h = measure.lineHeight(24.0f);

	REQUIRE(h == Approx(24.0f));
}

// ── findNearestFont ─────────────────────────────────────

TEST_CASE("Siv3DTextRenderer measure uses nearest font for unregistered size", "[siv3d][textrenderer]")
{
	ResetFixture fix;
	Siv3DTextRenderer renderer;
	renderer.registerFont(16);
	renderer.registerFont(32);

	ITextMeasure& measure = renderer;
	// fontSize 20 is not registered; nearest is 16
	const Vec2f size = measure.measure("A", 20.0f);

	// 最近接は16なので高さは16
	REQUIRE(size.y == Approx(16.0f));
}

TEST_CASE("Siv3DTextRenderer measure uses nearest font for intermediate size", "[siv3d][textrenderer]")
{
	ResetFixture fix;
	Siv3DTextRenderer renderer;
	renderer.registerFont(16);
	renderer.registerFont(32);

	ITextMeasure& measure = renderer;
	// fontSize 30 is closer to 32
	const Vec2f size = measure.measure("A", 30.0f);

	REQUIRE(size.y == Approx(32.0f));
}

TEST_CASE("Siv3DTextRenderer lineHeight falls back when no fonts registered", "[siv3d][textrenderer]")
{
	ResetFixture fix;
	Siv3DTextRenderer renderer;

	ITextMeasure& measure = renderer;
	// 登録フォントなし → フォールバックFont(16)を使用
	const float h = measure.lineHeight(24.0f);

	REQUIRE(h == Approx(16.0f));
}
