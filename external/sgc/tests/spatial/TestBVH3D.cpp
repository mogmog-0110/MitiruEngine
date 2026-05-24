/// @file TestBVH3D.cpp
/// @brief BVH3D.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/spatial/BVH3D.hpp"

TEST_CASE("BVH3D build from AABB3 span", "[spatial][bvh3d]")
{
	std::vector<sgc::AABB3f> aabbs = {
		{{0, 0, 0}, {10, 10, 10}},
		{{20, 20, 20}, {30, 30, 30}},
		{{50, 50, 50}, {60, 60, 60}}
	};

	sgc::BVH3Df bvh;
	bvh.build(std::span<const sgc::AABB3f>{aabbs});
	REQUIRE(bvh.size() == 3);

	auto all = bvh.queryRange(sgc::AABB3f{{0, 0, 0}, {100, 100, 100}});
	REQUIRE(all.size() == 3);
}

TEST_CASE("BVH3D insert and query range 3D", "[spatial][bvh3d]")
{
	sgc::BVH3Df bvh;
	bvh.insert(sgc::AABB3f{{0, 0, 0}, {10, 10, 10}});
	bvh.insert(sgc::AABB3f{{20, 20, 20}, {30, 30, 30}});
	bvh.insert(sgc::AABB3f{{100, 100, 100}, {110, 110, 110}});
	REQUIRE(bvh.size() == 3);

	// 部分範囲
	auto partial = bvh.queryRange(sgc::AABB3f{{0, 0, 0}, {35, 35, 35}});
	REQUIRE(partial.size() == 2);

	// 全範囲
	auto all = bvh.queryRange(sgc::AABB3f{{0, 0, 0}, {200, 200, 200}});
	REQUIRE(all.size() == 3);
}

TEST_CASE("BVH3D raycast 3D hits closest", "[spatial][bvh3d]")
{
	sgc::BVH3Df bvh;
	bvh.insert(sgc::AABB3f{{10, -5, -5}, {20, 5, 5}});   // 近い
	bvh.insert(sgc::AABB3f{{50, -5, -5}, {60, 5, 5}});   // 遠い

	auto hit = bvh.raycast({0, 0, 0}, {1, 0, 0}, 1000.0f);
	REQUIRE(hit.has_value());
	REQUIRE(hit->handle == 0);
	REQUIRE(hit->distance < 50.0f);
}

TEST_CASE("BVH3D raycast misses", "[spatial][bvh3d]")
{
	sgc::BVH3Df bvh;
	bvh.insert(sgc::AABB3f{{10, 10, 10}, {20, 20, 20}});

	// Z-方向に撃つが当たらない
	auto hit = bvh.raycast({0, 0, 0}, {0, 0, -1}, 1000.0f);
	REQUIRE_FALSE(hit.has_value());
}

TEST_CASE("BVH3D queryPoint 3D", "[spatial][bvh3d]")
{
	sgc::BVH3Df bvh;
	bvh.insert(sgc::AABB3f{{0, 0, 0}, {20, 20, 20}});
	bvh.insert(sgc::AABB3f{{5, 5, 5}, {15, 15, 15}});
	bvh.insert(sgc::AABB3f{{100, 100, 100}, {110, 110, 110}});

	auto results = bvh.queryPoint({10, 10, 10});
	REQUIRE(results.size() == 2);

	auto miss = bvh.queryPoint({200, 200, 200});
	REQUIRE(miss.empty());
}

TEST_CASE("BVH3D clear and size", "[spatial][bvh3d]")
{
	sgc::BVH3Df bvh;
	REQUIRE(bvh.size() == 0);

	bvh.insert(sgc::AABB3f{{0, 0, 0}, {10, 10, 10}});
	bvh.insert(sgc::AABB3f{{20, 20, 20}, {30, 30, 30}});
	REQUIRE(bvh.size() == 2);

	bvh.clear();
	REQUIRE(bvh.size() == 0);

	auto results = bvh.queryRange(sgc::AABB3f{{0, 0, 0}, {100, 100, 100}});
	REQUIRE(results.empty());
}

TEST_CASE("BVH3D remove and update", "[spatial][bvh3d]")
{
	sgc::BVH3Df bvh;
	auto h0 = bvh.insert(sgc::AABB3f{{0, 0, 0}, {10, 10, 10}});
	auto h1 = bvh.insert(sgc::AABB3f{{20, 20, 20}, {30, 30, 30}});
	REQUIRE(bvh.size() == 2);

	bvh.remove(h0);
	REQUIRE(bvh.size() == 1);

	// 更新テスト
	bvh.update(h1, sgc::AABB3f{{200, 200, 200}, {210, 210, 210}});
	auto results = bvh.queryRange(sgc::AABB3f{{0, 0, 0}, {50, 50, 50}});
	REQUIRE(results.empty());

	auto moved = bvh.queryRange(sgc::AABB3f{{200, 200, 200}, {210, 210, 210}});
	REQUIRE(moved.size() == 1);
}

TEST_CASE("BVH3D queryAllPairs", "[spatial][bvh3d]")
{
	sgc::BVH3Df bvh;
	bvh.insert(sgc::AABB3f{{0, 0, 0}, {10, 10, 10}});
	bvh.insert(sgc::AABB3f{{5, 5, 5}, {15, 15, 15}});   // h0と交差
	bvh.insert(sgc::AABB3f{{100, 100, 100}, {110, 110, 110}});

	auto pairs = bvh.queryAllPairs();
	REQUIRE(pairs.size() == 1);
}
