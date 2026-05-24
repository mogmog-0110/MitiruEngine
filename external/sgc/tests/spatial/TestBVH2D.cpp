/// @file TestBVH2D.cpp
/// @brief BVH2D.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/spatial/BVH2D.hpp"

TEST_CASE("BVH2D build from empty span", "[spatial][bvh2d]")
{
	sgc::BVH2Df bvh;
	std::vector<sgc::AABB2f> empty;
	bvh.build(std::span<const sgc::AABB2f>{empty});
	REQUIRE(bvh.size() == 0);
}

TEST_CASE("BVH2D insert single AABB", "[spatial][bvh2d]")
{
	sgc::BVH2Df bvh;
	auto h = bvh.insert(sgc::AABB2f{{10, 10}, {20, 20}});
	REQUIRE(bvh.size() == 1);
	REQUIRE(h == 0);
}

TEST_CASE("BVH2D insert multiple and query range", "[spatial][bvh2d]")
{
	sgc::BVH2Df bvh;
	bvh.insert(sgc::AABB2f{{0, 0}, {10, 10}});
	bvh.insert(sgc::AABB2f{{20, 20}, {30, 30}});
	bvh.insert(sgc::AABB2f{{50, 50}, {60, 60}});

	// 全範囲クエリ
	auto all = bvh.queryRange(sgc::AABB2f{{0, 0}, {100, 100}});
	REQUIRE(all.size() == 3);

	// 部分範囲クエリ
	auto partial = bvh.queryRange(sgc::AABB2f{{0, 0}, {15, 15}});
	REQUIRE(partial.size() == 1);
}

TEST_CASE("BVH2D queryRange returns only overlapping handles", "[spatial][bvh2d]")
{
	sgc::BVH2Df bvh;
	bvh.insert(sgc::AABB2f{{0, 0}, {10, 10}});
	bvh.insert(sgc::AABB2f{{100, 100}, {110, 110}});

	auto results = bvh.queryRange(sgc::AABB2f{{5, 5}, {15, 15}});
	REQUIRE(results.size() == 1);
	REQUIRE(results[0] == 0);
}

TEST_CASE("BVH2D remove handle", "[spatial][bvh2d]")
{
	sgc::BVH2Df bvh;
	auto h0 = bvh.insert(sgc::AABB2f{{0, 0}, {10, 10}});
	bvh.insert(sgc::AABB2f{{20, 20}, {30, 30}});
	REQUIRE(bvh.size() == 2);

	bvh.remove(h0);
	REQUIRE(bvh.size() == 1);

	// 削除された要素はクエリに出ない
	auto results = bvh.queryRange(sgc::AABB2f{{0, 0}, {100, 100}});
	REQUIRE(results.size() == 1);
}

TEST_CASE("BVH2D update handle moves AABB", "[spatial][bvh2d]")
{
	sgc::BVH2Df bvh;
	auto h = bvh.insert(sgc::AABB2f{{0, 0}, {10, 10}});

	// 元の位置でヒット
	auto before = bvh.queryRange(sgc::AABB2f{{0, 0}, {5, 5}});
	REQUIRE(before.size() == 1);

	// 遠い場所に移動
	bvh.update(h, sgc::AABB2f{{200, 200}, {210, 210}});

	// 元の位置ではヒットしない
	auto after = bvh.queryRange(sgc::AABB2f{{0, 0}, {5, 5}});
	REQUIRE(after.empty());

	// 新しい位置でヒット
	auto moved = bvh.queryRange(sgc::AABB2f{{200, 200}, {210, 210}});
	REQUIRE(moved.size() == 1);
}

TEST_CASE("BVH2D raycast hits closest object", "[spatial][bvh2d]")
{
	sgc::BVH2Df bvh;
	bvh.insert(sgc::AABB2f{{10, -5}, {20, 5}});   // 近い
	bvh.insert(sgc::AABB2f{{50, -5}, {60, 5}});   // 遠い

	// 原点からX+方向にレイキャスト
	auto hit = bvh.raycast({0, 0}, {1, 0}, 1000.0f);
	REQUIRE(hit.has_value());
	REQUIRE(hit->handle == 0);
	REQUIRE(hit->distance < 50.0f);
}

TEST_CASE("BVH2D raycast misses when no intersection", "[spatial][bvh2d]")
{
	sgc::BVH2Df bvh;
	bvh.insert(sgc::AABB2f{{10, 10}, {20, 20}});

	// Y-方向にレイキャスト（当たらない）
	auto hit = bvh.raycast({0, 0}, {0, -1}, 1000.0f);
	REQUIRE_FALSE(hit.has_value());
}

TEST_CASE("BVH2D queryPoint returns containing AABBs", "[spatial][bvh2d]")
{
	sgc::BVH2Df bvh;
	bvh.insert(sgc::AABB2f{{0, 0}, {20, 20}});
	bvh.insert(sgc::AABB2f{{5, 5}, {15, 15}});
	bvh.insert(sgc::AABB2f{{50, 50}, {60, 60}});

	// 両方に含まれる点
	auto results = bvh.queryPoint({10, 10});
	REQUIRE(results.size() == 2);

	// どれにも含まれない点
	auto miss = bvh.queryPoint({100, 100});
	REQUIRE(miss.empty());
}

TEST_CASE("BVH2D queryAllPairs finds overlapping pairs", "[spatial][bvh2d]")
{
	sgc::BVH2Df bvh;
	bvh.insert(sgc::AABB2f{{0, 0}, {10, 10}});
	bvh.insert(sgc::AABB2f{{5, 5}, {15, 15}});   // h0と交差
	bvh.insert(sgc::AABB2f{{100, 100}, {110, 110}});  // 誰とも交差しない

	auto pairs = bvh.queryAllPairs();
	REQUIRE(pairs.size() == 1);
	REQUIRE(pairs[0].first == 0);
	REQUIRE(pairs[0].second == 1);
}

TEST_CASE("BVH2D clear empties the tree", "[spatial][bvh2d]")
{
	sgc::BVH2Df bvh;
	bvh.insert(sgc::AABB2f{{0, 0}, {10, 10}});
	bvh.insert(sgc::AABB2f{{20, 20}, {30, 30}});
	REQUIRE(bvh.size() == 2);

	bvh.clear();
	REQUIRE(bvh.size() == 0);

	auto results = bvh.queryRange(sgc::AABB2f{{0, 0}, {100, 100}});
	REQUIRE(results.empty());
}

TEST_CASE("BVH2D size tracks insertions and removals", "[spatial][bvh2d]")
{
	sgc::BVH2Df bvh;
	REQUIRE(bvh.size() == 0);

	auto h0 = bvh.insert(sgc::AABB2f{{0, 0}, {10, 10}});
	REQUIRE(bvh.size() == 1);

	auto h1 = bvh.insert(sgc::AABB2f{{20, 20}, {30, 30}});
	REQUIRE(bvh.size() == 2);

	bvh.remove(h0);
	REQUIRE(bvh.size() == 1);

	bvh.remove(h1);
	REQUIRE(bvh.size() == 0);
}

TEST_CASE("BVH2D build from AABB span then query", "[spatial][bvh2d]")
{
	std::vector<sgc::AABB2f> aabbs = {
		{{0, 0}, {10, 10}},
		{{20, 20}, {30, 30}},
		{{50, 50}, {60, 60}},
		{{80, 80}, {90, 90}}
	};

	sgc::BVH2Df bvh;
	bvh.build(std::span<const sgc::AABB2f>{aabbs});
	REQUIRE(bvh.size() == 4);

	auto results = bvh.queryRange(sgc::AABB2f{{0, 0}, {35, 35}});
	REQUIRE(results.size() == 2);
}
