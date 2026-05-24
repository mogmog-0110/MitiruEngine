/// @file TestDxLibTypeConvert.cpp
/// @brief DxLib TypeConvert tests (DxLib.h independent)

#include <catch2/catch_test_macros.hpp>

#include "sgc/dxlib/TypeConvert.hpp"

using namespace sgc;
using namespace sgc::dxlib;

TEST_CASE("toDxAlpha converts float to int [0,255]", "[dxlib][TypeConvert]")
{
	CHECK(toDxAlpha(0.0f) == 0);
	CHECK(toDxAlpha(1.0f) == 255);
	CHECK(toDxAlpha(0.5f) == 128);
	CHECK(toDxAlpha(-0.5f) == 0);
	CHECK(toDxAlpha(1.5f) == 255);
}

TEST_CASE("toDxPoint converts Vec2f to integer coordinates", "[dxlib][TypeConvert]")
{
	const auto p1 = toDxPoint(Vec2f{10.7f, 20.3f});
	CHECK(p1.x == 10);
	CHECK(p1.y == 20);

	const auto p2 = toDxPoint(Vec2f{-5.0f, -10.0f});
	CHECK(p2.x == -5);
	CHECK(p2.y == -10);
}

TEST_CASE("toDxRect converts AABB2f to integer rect", "[dxlib][TypeConvert]")
{
	const AABB2f aabb{.min = {10.0f, 20.0f}, .max = {100.0f, 200.0f}};
	const auto r = toDxRect(aabb);

	CHECK(r.x1 == 10);
	CHECK(r.y1 == 20);
	CHECK(r.x2 == 100);
	CHECK(r.y2 == 200);
}

TEST_CASE("packDxColor packs RGB into DxLib format", "[dxlib][TypeConvert]")
{
	// DxLib format: r | (g << 8) | (b << 16)
	const unsigned int white = packDxColor(255, 255, 255);
	CHECK(white == (255u | (255u << 8) | (255u << 16)));

	const unsigned int red = packDxColor(255, 0, 0);
	CHECK(red == 255u);

	const unsigned int green = packDxColor(0, 255, 0);
	CHECK(green == (255u << 8));

	const unsigned int blue = packDxColor(0, 0, 255);
	CHECK(blue == (255u << 16));

	const unsigned int black = packDxColor(0, 0, 0);
	CHECK(black == 0u);
}

TEST_CASE("toDxColor converts Colorf to packed DxLib color", "[dxlib][TypeConvert]")
{
	const unsigned int red = toDxColor(Colorf::red());
	CHECK(red == packDxColor(255, 0, 0));

	const unsigned int white = toDxColor(Colorf::white());
	CHECK(white == packDxColor(255, 255, 255));

	const unsigned int black = toDxColor(Colorf::black());
	CHECK(black == packDxColor(0, 0, 0));
}

TEST_CASE("toDxColor ignores alpha component", "[dxlib][TypeConvert]")
{
	const Colorf semiRed{1.0f, 0.0f, 0.0f, 0.5f};
	const Colorf opaqueRed{1.0f, 0.0f, 0.0f, 1.0f};

	CHECK(toDxColor(semiRed) == toDxColor(opaqueRed));
}

TEST_CASE("toDxAlpha boundary values are correct", "[dxlib][TypeConvert]")
{
	// 0.25 -> 64 (0.25 * 255 + 0.5 = 64.25 -> 64)
	CHECK(toDxAlpha(0.25f) == 64);

	// 0.75 -> 191 (0.75 * 255 + 0.5 = 191.75 -> 191)
	CHECK(toDxAlpha(0.75f) == 191);
}

TEST_CASE("toDxRect with zero-area AABB", "[dxlib][TypeConvert]")
{
	const AABB2f zero{.min = {50.0f, 50.0f}, .max = {50.0f, 50.0f}};
	const auto r = toDxRect(zero);

	CHECK(r.x1 == 50);
	CHECK(r.y1 == 50);
	CHECK(r.x2 == 50);
	CHECK(r.y2 == 50);
}
