#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/physics/RayCast2D.hpp"

using namespace sgc;
using namespace sgc::physics;
using Catch::Approx;

// ── レイ vs AABB ───────────────────────────────────────────

TEST_CASE("raycastAABB - ray hitting AABB from left", "[physics][RayCast2D]")
{
	const Vec2f origin{0.0f, 5.0f};
	const Vec2f direction{1.0f, 0.0f};
	const AABB2f box{{3.0f, 0.0f}, {6.0f, 10.0f}};

	const auto hit = raycastAABB(origin, direction, box);

	REQUIRE(hit.hit);
	REQUIRE(hit.distance == Approx(3.0f));
	REQUIRE(hit.point.x == Approx(3.0f));
	REQUIRE(hit.point.y == Approx(5.0f));
	REQUIRE(hit.normal.x == Approx(-1.0f));
	REQUIRE(hit.normal.y == Approx(0.0f));
}

TEST_CASE("raycastAABB - ray hitting AABB from top", "[physics][RayCast2D]")
{
	const Vec2f origin{5.0f, 0.0f};
	const Vec2f direction{0.0f, 1.0f};
	const AABB2f box{{0.0f, 3.0f}, {10.0f, 6.0f}};

	const auto hit = raycastAABB(origin, direction, box);

	REQUIRE(hit.hit);
	REQUIRE(hit.distance == Approx(3.0f));
	REQUIRE(hit.point.x == Approx(5.0f));
	REQUIRE(hit.point.y == Approx(3.0f));
	REQUIRE(hit.normal.x == Approx(0.0f));
	REQUIRE(hit.normal.y == Approx(-1.0f));
}

TEST_CASE("raycastAABB - ray missing AABB", "[physics][RayCast2D]")
{
	const Vec2f origin{0.0f, 0.0f};
	const Vec2f direction{1.0f, 0.0f};
	const AABB2f box{{5.0f, 5.0f}, {10.0f, 10.0f}};

	const auto hit = raycastAABB(origin, direction, box);

	REQUIRE_FALSE(hit.hit);
}

TEST_CASE("raycastAABB - ray pointing away from AABB", "[physics][RayCast2D]")
{
	const Vec2f origin{0.0f, 5.0f};
	const Vec2f direction{-1.0f, 0.0f};  // 反対方向
	const AABB2f box{{3.0f, 0.0f}, {6.0f, 10.0f}};

	const auto hit = raycastAABB(origin, direction, box);

	REQUIRE_FALSE(hit.hit);
}

TEST_CASE("raycastAABB - max distance limiting", "[physics][RayCast2D]")
{
	const Vec2f origin{0.0f, 5.0f};
	const Vec2f direction{1.0f, 0.0f};
	const AABB2f box{{10.0f, 0.0f}, {15.0f, 10.0f}};

	SECTION("distance within range")
	{
		const auto hit = raycastAABB(origin, direction, box, 20.0f);
		REQUIRE(hit.hit);
	}

	SECTION("distance exceeds max")
	{
		const auto hit = raycastAABB(origin, direction, box, 5.0f);
		REQUIRE_FALSE(hit.hit);
	}
}

TEST_CASE("raycastAABB - ray origin inside AABB", "[physics][RayCast2D]")
{
	const Vec2f origin{5.0f, 5.0f};
	const Vec2f direction{1.0f, 0.0f};
	const AABB2f box{{0.0f, 0.0f}, {10.0f, 10.0f}};

	const auto hit = raycastAABB(origin, direction, box);

	// レイが内部から始まる場合、tMin=0で衝突
	REQUIRE(hit.hit);
	REQUIRE(hit.distance == Approx(0.0f));
}

TEST_CASE("raycastAABB - diagonal ray", "[physics][RayCast2D]")
{
	const Vec2f origin{0.0f, 0.0f};
	const Vec2f dir = Vec2f{1.0f, 1.0f}.normalized();
	const AABB2f box{{3.0f, 3.0f}, {6.0f, 6.0f}};

	const auto hit = raycastAABB(origin, dir, box);

	REQUIRE(hit.hit);
	REQUIRE(hit.point.x == Approx(3.0f));
	REQUIRE(hit.point.y == Approx(3.0f));
}

// ── レイ vs 円 ────────────────────────────────────────────

TEST_CASE("raycastCircle - ray hitting circle", "[physics][RayCast2D]")
{
	const Vec2f origin{0.0f, 0.0f};
	const Vec2f direction{1.0f, 0.0f};
	const Vec2f center{5.0f, 0.0f};
	const float radius = 2.0f;

	const auto hit = raycastCircle(origin, direction, center, radius);

	REQUIRE(hit.hit);
	REQUIRE(hit.distance == Approx(3.0f));
	REQUIRE(hit.point.x == Approx(3.0f));
	REQUIRE(hit.point.y == Approx(0.0f));
	REQUIRE(hit.normal.x == Approx(-1.0f));
	REQUIRE(hit.normal.y == Approx(0.0f));
}

TEST_CASE("raycastCircle - ray missing circle", "[physics][RayCast2D]")
{
	const Vec2f origin{0.0f, 0.0f};
	const Vec2f direction{1.0f, 0.0f};
	const Vec2f center{5.0f, 5.0f};
	const float radius = 1.0f;

	const auto hit = raycastCircle(origin, direction, center, radius);

	REQUIRE_FALSE(hit.hit);
}

TEST_CASE("raycastCircle - ray pointing away", "[physics][RayCast2D]")
{
	const Vec2f origin{0.0f, 0.0f};
	const Vec2f direction{-1.0f, 0.0f};
	const Vec2f center{5.0f, 0.0f};
	const float radius = 2.0f;

	const auto hit = raycastCircle(origin, direction, center, radius);

	REQUIRE_FALSE(hit.hit);
}

TEST_CASE("raycastCircle - max distance limiting", "[physics][RayCast2D]")
{
	const Vec2f origin{0.0f, 0.0f};
	const Vec2f direction{1.0f, 0.0f};
	const Vec2f center{10.0f, 0.0f};
	const float radius = 2.0f;

	SECTION("within range")
	{
		const auto hit = raycastCircle(origin, direction, center, radius, 20.0f);
		REQUIRE(hit.hit);
	}

	SECTION("out of range")
	{
		const auto hit = raycastCircle(origin, direction, center, radius, 5.0f);
		REQUIRE_FALSE(hit.hit);
	}
}

TEST_CASE("raycastCircle - tangent ray", "[physics][RayCast2D]")
{
	const Vec2f origin{0.0f, 2.0f};
	const Vec2f direction{1.0f, 0.0f};
	const Vec2f center{5.0f, 0.0f};
	const float radius = 2.0f;

	const auto hit = raycastCircle(origin, direction, center, radius);

	// 接線の場合、判別式 == 0 でギリギリヒット
	REQUIRE(hit.hit);
	REQUIRE(hit.point.y == Approx(2.0f));
}

TEST_CASE("raycastCircle - ray origin inside circle", "[physics][RayCast2D]")
{
	const Vec2f origin{5.0f, 0.0f};
	const Vec2f direction{1.0f, 0.0f};
	const Vec2f center{5.0f, 0.0f};
	const float radius = 3.0f;

	const auto hit = raycastCircle(origin, direction, center, radius);

	// 内部から発射 → 遠い交点にヒット
	REQUIRE(hit.hit);
	REQUIRE(hit.distance == Approx(3.0f));
}
