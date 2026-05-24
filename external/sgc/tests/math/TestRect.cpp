/// @file TestRect.cpp
/// @brief Rect.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/math/Rect.hpp"

using Catch::Approx;

// ── Construction ──────────────────────────────────────────────────

TEST_CASE("Rect default construction is zero", "[math][rect]")
{
	constexpr sgc::Rectf r;
	STATIC_REQUIRE(r.position.x == 0.0f);
	STATIC_REQUIRE(r.size.x == 0.0f);
}

TEST_CASE("Rect construction with position and size", "[math][rect]")
{
	constexpr sgc::Rectf r{{10.0f, 20.0f}, {100.0f, 50.0f}};
	STATIC_REQUIRE(r.position.x == 10.0f);
	STATIC_REQUIRE(r.size.y == 50.0f);
}

TEST_CASE("Rect construction with individual components", "[math][rect]")
{
	constexpr sgc::Rectf r{10.0f, 20.0f, 100.0f, 50.0f};
	STATIC_REQUIRE(r.x() == 10.0f);
	STATIC_REQUIRE(r.y() == 20.0f);
	STATIC_REQUIRE(r.width() == 100.0f);
	STATIC_REQUIRE(r.height() == 50.0f);
}

// ── Factory functions ────────────────────────────────────────────

TEST_CASE("Rect fromMinMax", "[math][rect]")
{
	constexpr auto r = sgc::Rectf::fromMinMax({10.0f, 20.0f}, {110.0f, 70.0f});
	STATIC_REQUIRE(r.position.x == 10.0f);
	STATIC_REQUIRE(r.position.y == 20.0f);
	STATIC_REQUIRE(r.size.x == 100.0f);
	STATIC_REQUIRE(r.size.y == 50.0f);
}

TEST_CASE("Rect fromCenter", "[math][rect]")
{
	constexpr auto r = sgc::Rectf::fromCenter({50.0f, 50.0f}, {100.0f, 40.0f});
	STATIC_REQUIRE(r.position.x == 0.0f);
	STATIC_REQUIRE(r.position.y == 30.0f);
	STATIC_REQUIRE(r.size.x == 100.0f);
}

TEST_CASE("Rect toAABB2 conversion", "[math][rect]")
{
	constexpr sgc::Rectf r{10.0f, 20.0f, 100.0f, 50.0f};
	constexpr auto aabb = r.toAABB2();
	STATIC_REQUIRE(aabb.min.x == 10.0f);
	STATIC_REQUIRE(aabb.max.x == 110.0f);
	STATIC_REQUIRE(aabb.max.y == 70.0f);
}

// ── Accessors ────────────────────────────────────────────────────

TEST_CASE("Rect center calculation", "[math][rect]")
{
	constexpr sgc::Rectf r{0.0f, 0.0f, 100.0f, 80.0f};
	constexpr auto c = r.center();
	STATIC_REQUIRE(c.x == 50.0f);
	STATIC_REQUIRE(c.y == 40.0f);
}

TEST_CASE("Rect edge accessors", "[math][rect]")
{
	constexpr sgc::Rectf r{10.0f, 20.0f, 100.0f, 50.0f};
	STATIC_REQUIRE(r.left() == 10.0f);
	STATIC_REQUIRE(r.right() == 110.0f);
	STATIC_REQUIRE(r.top() == 20.0f);
	STATIC_REQUIRE(r.bottom() == 70.0f);
}

TEST_CASE("Rect area", "[math][rect]")
{
	constexpr sgc::Rectf r{0.0f, 0.0f, 10.0f, 5.0f};
	STATIC_REQUIRE(r.area() == 50.0f);
}

// ── Contains ─────────────────────────────────────────────────────

TEST_CASE("Rect contains point inside", "[math][rect]")
{
	constexpr sgc::Rectf r{0.0f, 0.0f, 100.0f, 100.0f};
	STATIC_REQUIRE(r.contains({50.0f, 50.0f}));
}

TEST_CASE("Rect does not contain point outside", "[math][rect]")
{
	constexpr sgc::Rectf r{0.0f, 0.0f, 100.0f, 100.0f};
	STATIC_REQUIRE_FALSE(r.contains({150.0f, 50.0f}));
}

TEST_CASE("Rect contains another rect", "[math][rect]")
{
	constexpr sgc::Rectf outer{0.0f, 0.0f, 100.0f, 100.0f};
	constexpr sgc::Rectf inner{10.0f, 10.0f, 50.0f, 50.0f};
	STATIC_REQUIRE(outer.contains(inner));
	STATIC_REQUIRE_FALSE(inner.contains(outer));
}

// ── Intersects ───────────────────────────────────────────────────

TEST_CASE("Rect intersects overlapping rects", "[math][rect]")
{
	constexpr sgc::Rectf a{0.0f, 0.0f, 100.0f, 100.0f};
	constexpr sgc::Rectf b{50.0f, 50.0f, 100.0f, 100.0f};
	STATIC_REQUIRE(a.intersects(b));
}

TEST_CASE("Rect does not intersect separated rects", "[math][rect]")
{
	constexpr sgc::Rectf a{0.0f, 0.0f, 10.0f, 10.0f};
	constexpr sgc::Rectf b{20.0f, 20.0f, 10.0f, 10.0f};
	STATIC_REQUIRE_FALSE(a.intersects(b));
}

TEST_CASE("Rect intersection area", "[math][rect]")
{
	constexpr sgc::Rectf a{0.0f, 0.0f, 100.0f, 100.0f};
	constexpr sgc::Rectf b{50.0f, 50.0f, 100.0f, 100.0f};
	constexpr auto inter = a.intersection(b);
	STATIC_REQUIRE(inter.position.x == 50.0f);
	STATIC_REQUIRE(inter.position.y == 50.0f);
	STATIC_REQUIRE(inter.size.x == 50.0f);
	STATIC_REQUIRE(inter.size.y == 50.0f);
}

TEST_CASE("Rect intersection returns zero for non-overlapping", "[math][rect]")
{
	constexpr sgc::Rectf a{0.0f, 0.0f, 10.0f, 10.0f};
	constexpr sgc::Rectf b{20.0f, 20.0f, 10.0f, 10.0f};
	constexpr auto inter = a.intersection(b);
	STATIC_REQUIRE(inter.size.x == 0.0f);
	STATIC_REQUIRE(inter.size.y == 0.0f);
}

// ── Transforms ───────────────────────────────────────────────────

TEST_CASE("Rect expanded increases size uniformly", "[math][rect]")
{
	constexpr sgc::Rectf r{10.0f, 10.0f, 100.0f, 50.0f};
	constexpr auto expanded = r.expanded(5.0f);
	STATIC_REQUIRE(expanded.position.x == 5.0f);
	STATIC_REQUIRE(expanded.position.y == 5.0f);
	STATIC_REQUIRE(expanded.size.x == 110.0f);
	STATIC_REQUIRE(expanded.size.y == 60.0f);
}

TEST_CASE("Rect moved translates position", "[math][rect]")
{
	constexpr sgc::Rectf r{10.0f, 20.0f, 100.0f, 50.0f};
	constexpr auto moved = r.moved({5.0f, -3.0f});
	STATIC_REQUIRE(moved.position.x == 15.0f);
	STATIC_REQUIRE(moved.position.y == 17.0f);
	STATIC_REQUIRE(moved.size.x == 100.0f);
}

TEST_CASE("Rect scaled around center", "[math][rect]")
{
	constexpr sgc::Rectf r{0.0f, 0.0f, 100.0f, 100.0f};
	constexpr auto scaled = r.scaled(0.5f);
	STATIC_REQUIRE(scaled.position.x == 25.0f);
	STATIC_REQUIRE(scaled.position.y == 25.0f);
	STATIC_REQUIRE(scaled.size.x == 50.0f);
	STATIC_REQUIRE(scaled.size.y == 50.0f);
}

// ── Integer rect ─────────────────────────────────────────────────

TEST_CASE("Recti basic operations", "[math][rect]")
{
	constexpr sgc::Recti r{0, 0, 100, 50};
	STATIC_REQUIRE(r.area() == 5000);
	STATIC_REQUIRE(r.contains(sgc::Vec2i{50, 25}));
}

// ── Edge cases ──────────────────────────────────────────

TEST_CASE("Rect contains point on boundary", "[math][rect]")
{
	constexpr sgc::Rectf r{0.0f, 0.0f, 100.0f, 100.0f};
	// boundary is inclusive (>= and <=)
	STATIC_REQUIRE(r.contains({0.0f, 0.0f}));     // top-left corner
	STATIC_REQUIRE(r.contains({100.0f, 100.0f}));  // bottom-right corner
	STATIC_REQUIRE(r.contains({50.0f, 0.0f}));     // top edge
	STATIC_REQUIRE(r.contains({0.0f, 50.0f}));     // left edge
}

TEST_CASE("Rect contains itself", "[math][rect]")
{
	constexpr sgc::Rectf r{10.0f, 20.0f, 50.0f, 30.0f};
	STATIC_REQUIRE(r.contains(r));
}

TEST_CASE("Rect zero size area is zero", "[math][rect]")
{
	constexpr sgc::Rectf r{10.0f, 20.0f, 0.0f, 0.0f};
	STATIC_REQUIRE(r.area() == 0.0f);
}
