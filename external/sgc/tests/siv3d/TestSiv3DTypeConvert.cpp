/// @file TestSiv3DTypeConvert.cpp
/// @brief Siv3D型変換アダプターのテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/siv3d/TypeConvert.hpp"

using namespace sgc;
using namespace sgc::siv3d;
using Catch::Approx;

// ── テスト前にスタブをリセット ─────────────────────────

namespace
{

struct ResetFixture
{
	ResetFixture() { siv3d_stub::reset(); }
};

} // anonymous namespace

// ── Vec2 conversion ─────────────────────────────────────

TEST_CASE("TypeConvert toSiv Vec2f converts float to double", "[siv3d][typeconvert]")
{
	ResetFixture fix;

	const Vec2f v{10.5f, 20.3f};
	const s3d::Vec2 result = toSiv(v);

	REQUIRE(result.x == Approx(10.5));
	REQUIRE(result.y == Approx(20.3));
}

TEST_CASE("TypeConvert fromSiv Vec2 converts double to float", "[siv3d][typeconvert]")
{
	ResetFixture fix;

	const s3d::Vec2 v{100.0, 200.0};
	const Vec2f result = fromSiv(v);

	REQUIRE(result.x == Approx(100.0f));
	REQUIRE(result.y == Approx(200.0f));
}

TEST_CASE("TypeConvert Vec2 roundtrip preserves values", "[siv3d][typeconvert]")
{
	ResetFixture fix;

	const Vec2f original{42.0f, 84.0f};
	const Vec2f roundtripped = fromSiv(toSiv(original));

	REQUIRE(roundtripped.x == Approx(original.x));
	REQUIRE(roundtripped.y == Approx(original.y));
}

// ── Vec3 conversion ─────────────────────────────────────

TEST_CASE("TypeConvert toSiv Vec3f converts float to double", "[siv3d][typeconvert]")
{
	ResetFixture fix;

	const Vec3f v{1.0f, 2.0f, 3.0f};
	const s3d::Vec3 result = toSiv(v);

	REQUIRE(result.x == Approx(1.0));
	REQUIRE(result.y == Approx(2.0));
	REQUIRE(result.z == Approx(3.0));
}

TEST_CASE("TypeConvert fromSiv Vec3 converts double to float", "[siv3d][typeconvert]")
{
	ResetFixture fix;

	const s3d::Vec3 v{5.0, 10.0, 15.0};
	const Vec3f result = fromSiv(v);

	REQUIRE(result.x == Approx(5.0f));
	REQUIRE(result.y == Approx(10.0f));
	REQUIRE(result.z == Approx(15.0f));
}

// ── Color conversion ────────────────────────────────────

TEST_CASE("TypeConvert toSivColorF converts Colorf to ColorF", "[siv3d][typeconvert]")
{
	ResetFixture fix;

	const Colorf c{0.5f, 0.6f, 0.7f, 0.8f};
	const s3d::ColorF result = toSivColorF(c);

	REQUIRE(result.r == Approx(0.5));
	REQUIRE(result.g == Approx(0.6));
	REQUIRE(result.b == Approx(0.7));
	REQUIRE(result.a == Approx(0.8));
}

TEST_CASE("TypeConvert toSivColor converts Colorf to Color", "[siv3d][typeconvert]")
{
	ResetFixture fix;

	const Colorf c{1.0f, 0.0f, 0.5f, 1.0f};
	const s3d::Color result = toSivColor(c);

	REQUIRE(result.r == 255);
	REQUIRE(result.g == 0);
	REQUIRE(result.b == 128); // 0.5 * 255 = 127.5 -> 128
	REQUIRE(result.a == 255);
}

TEST_CASE("TypeConvert fromSivColorF converts ColorF to Colorf", "[siv3d][typeconvert]")
{
	ResetFixture fix;

	const s3d::ColorF c{0.2, 0.4, 0.6, 0.8};
	const Colorf result = fromSivColorF(c);

	REQUIRE(result.r == Approx(0.2f));
	REQUIRE(result.g == Approx(0.4f));
	REQUIRE(result.b == Approx(0.6f));
	REQUIRE(result.a == Approx(0.8f));
}

TEST_CASE("TypeConvert fromSivColor converts Color to Colorf", "[siv3d][typeconvert]")
{
	ResetFixture fix;

	const s3d::Color c{255, 128, 0, 255};
	const Colorf result = fromSivColor(c);

	REQUIRE(result.r == Approx(1.0f));
	REQUIRE(result.g == Approx(128.0f / 255.0f));
	REQUIRE(result.b == Approx(0.0f));
	REQUIRE(result.a == Approx(1.0f));
}

// ── AABB2 / RectF conversion ────────────────────────────

TEST_CASE("TypeConvert toSivRect converts AABB2f to RectF", "[siv3d][typeconvert]")
{
	ResetFixture fix;

	const AABB2f aabb{{10.0f, 20.0f}, {110.0f, 70.0f}};
	const s3d::RectF result = toSivRect(aabb);

	REQUIRE(result.x == Approx(10.0));
	REQUIRE(result.y == Approx(20.0));
	REQUIRE(result.w == Approx(100.0));
	REQUIRE(result.h == Approx(50.0));
}

TEST_CASE("TypeConvert fromSivRect converts RectF to AABB2f", "[siv3d][typeconvert]")
{
	ResetFixture fix;

	const s3d::RectF rect{10.0, 20.0, 100.0, 50.0};
	const AABB2f result = fromSivRect(rect);

	REQUIRE(result.min.x == Approx(10.0f));
	REQUIRE(result.min.y == Approx(20.0f));
	REQUIRE(result.max.x == Approx(110.0f));
	REQUIRE(result.max.y == Approx(70.0f));
}

// ── Rectf conversion ────────────────────────────────────

TEST_CASE("TypeConvert toSiv Rectf converts to RectF", "[siv3d][typeconvert]")
{
	ResetFixture fix;

	const Rectf r{5.0f, 10.0f, 200.0f, 100.0f};
	const s3d::RectF result = toSiv(r);

	REQUIRE(result.x == Approx(5.0));
	REQUIRE(result.y == Approx(10.0));
	REQUIRE(result.w == Approx(200.0));
	REQUIRE(result.h == Approx(100.0));
}

TEST_CASE("TypeConvert toRectf converts RectF to Rectf", "[siv3d][typeconvert]")
{
	ResetFixture fix;

	const s3d::RectF r{5.0, 10.0, 200.0, 100.0};
	const Rectf result = toRectf(r);

	REQUIRE(result.x() == Approx(5.0f));
	REQUIRE(result.y() == Approx(10.0f));
	REQUIRE(result.width() == Approx(200.0f));
	REQUIRE(result.height() == Approx(100.0f));
}

// ── Circle conversion ───────────────────────────────────

TEST_CASE("TypeConvert toSivCircle from center and radius", "[siv3d][typeconvert]")
{
	ResetFixture fix;

	const Vec2f center{50.0f, 60.0f};
	const s3d::Circle result = toSivCircle(center, 30.0f);

	REQUIRE(result.center.x == Approx(50.0));
	REQUIRE(result.center.y == Approx(60.0));
	REQUIRE(result.r == Approx(30.0));
}

TEST_CASE("TypeConvert toSivCircle from sgc Circle", "[siv3d][typeconvert]")
{
	ResetFixture fix;

	const sgc::Circle<float> circle{{100.0f, 200.0f}, 50.0f};
	const s3d::Circle result = toSivCircle(circle);

	REQUIRE(result.center.x == Approx(100.0));
	REQUIRE(result.center.y == Approx(200.0));
	REQUIRE(result.r == Approx(50.0));
}
