/// @file TestDxLibTextMeasure.cpp
/// @brief DxLibTextMeasure adapter tests with stub

#include <catch2/catch_test_macros.hpp>

#include "sgc/dxlib/DxLibTextMeasure.hpp"

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

TEST_CASE("DxLibTextMeasure registerFont creates font handle", "[dxlib][textmeasure]")
{
	ResetFixture fix;
	DxLibTextMeasure measure;
	measure.registerFont(24);

	REQUIRE(dxlib_stub::fontRecords().size() == 1);
	REQUIRE(dxlib_stub::fontRecords()[0].size == 24);
	REQUIRE_FALSE(dxlib_stub::fontRecords()[0].deleted);
}

TEST_CASE("DxLibTextMeasure registerFont overwrites existing size", "[dxlib][textmeasure]")
{
	ResetFixture fix;
	DxLibTextMeasure measure;
	measure.registerFont(24);
	measure.registerFont(24, "TestFont");

	// 最初のハンドルは削除済み、2つ目が有効
	REQUIRE(dxlib_stub::fontRecords().size() == 2);
	REQUIRE(dxlib_stub::fontRecords()[0].deleted);
	REQUIRE_FALSE(dxlib_stub::fontRecords()[1].deleted);
}

// ── destructor ──────────────────────────────────────────

TEST_CASE("DxLibTextMeasure destructor deletes all font handles", "[dxlib][textmeasure]")
{
	ResetFixture fix;
	{
		DxLibTextMeasure measure;
		measure.registerFont(24);
		measure.registerFont(60);
	}

	// 全フォントが削除済み
	for (const auto& rec : dxlib_stub::fontRecords())
	{
		REQUIRE(rec.deleted);
	}
}

// ── measure ─────────────────────────────────────────────

TEST_CASE("DxLibTextMeasure measure returns width and height", "[dxlib][textmeasure]")
{
	ResetFixture fix;
	DxLibTextMeasure tm;
	tm.registerFont(24);

	// "Hello" = 5文字、stub幅 = 5*12 = 60px、高さ = 24
	const Vec2f size = tm.measure("Hello", 24.0f);
	REQUIRE(size.x == 60.0f);
	REQUIRE(size.y == 24.0f);
}

TEST_CASE("DxLibTextMeasure measure with nearest font fallback", "[dxlib][textmeasure]")
{
	ResetFixture fix;
	DxLibTextMeasure tm;
	tm.registerFont(24);
	tm.registerFont(60);

	// サイズ30を要求→最近接は24（差6 < 差30）
	const Vec2f size = tm.measure("AB", 30.0f);
	// "AB" = 2文字、stub幅 = 2*12 = 24px、高さ = 24（最近接サイズ）
	REQUIRE(size.x == 24.0f);
	REQUIRE(size.y == 24.0f);
}

TEST_CASE("DxLibTextMeasure measure throws when no fonts registered", "[dxlib][textmeasure]")
{
	ResetFixture fix;
	DxLibTextMeasure tm;

	REQUIRE_THROWS_AS(
		tm.measure("Test", 24.0f),
		std::out_of_range);
}

// ── lineHeight ──────────────────────────────────────────

TEST_CASE("DxLibTextMeasure lineHeight returns font size", "[dxlib][textmeasure]")
{
	ResetFixture fix;
	DxLibTextMeasure tm;
	tm.registerFont(24);

	REQUIRE(tm.lineHeight(24.0f) == 24.0f);
}

TEST_CASE("DxLibTextMeasure lineHeight uses nearest size", "[dxlib][textmeasure]")
{
	ResetFixture fix;
	DxLibTextMeasure tm;
	tm.registerFont(24);
	tm.registerFont(60);

	// サイズ50を要求→最近接は60（差10 < 差26）
	REQUIRE(tm.lineHeight(50.0f) == 60.0f);
}

// ── multiple fonts ──────────────────────────────────────

TEST_CASE("DxLibTextMeasure manages multiple font sizes", "[dxlib][textmeasure]")
{
	ResetFixture fix;
	DxLibTextMeasure tm;
	tm.registerFont(24);
	tm.registerFont(60);

	// 両方のサイズで計測できること
	REQUIRE_NOTHROW(tm.measure("small", 24.0f));
	REQUIRE_NOTHROW(tm.measure("large", 60.0f));

	// サイズが正しいこと
	REQUIRE(tm.measure("X", 24.0f).y == 24.0f);
	REQUIRE(tm.measure("X", 60.0f).y == 60.0f);
}
