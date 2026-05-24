/// @file TestOctree.cpp
/// @brief Octree.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/spatial/Octree.hpp"

TEST_CASE("Octree insert and size", "[spatial][octree]")
{
	sgc::Octreef ot(sgc::AABB3f{{0, 0, 0}, {100, 100, 100}});
	REQUIRE(ot.size() == 0);

	ot.insert(sgc::AABB3f{{10, 10, 10}, {20, 20, 20}});
	REQUIRE(ot.size() == 1);
}

TEST_CASE("Octree remove", "[spatial][octree]")
{
	sgc::Octreef ot(sgc::AABB3f{{0, 0, 0}, {100, 100, 100}});
	auto h = ot.insert(sgc::AABB3f{{10, 10, 10}, {20, 20, 20}});
	ot.remove(h);
	REQUIRE(ot.size() == 0);
}

TEST_CASE("Octree queryRange finds elements", "[spatial][octree]")
{
	sgc::Octreef ot(sgc::AABB3f{{0, 0, 0}, {100, 100, 100}});
	ot.insert(sgc::AABB3f{{10, 10, 10}, {20, 20, 20}});
	ot.insert(sgc::AABB3f{{80, 80, 80}, {90, 90, 90}});

	auto results = ot.queryRange(sgc::AABB3f{{0, 0, 0}, {50, 50, 50}});
	REQUIRE(results.size() == 1);
}

TEST_CASE("Octree queryPoint", "[spatial][octree]")
{
	sgc::Octreef ot(sgc::AABB3f{{0, 0, 0}, {100, 100, 100}});
	ot.insert(sgc::AABB3f{{10, 10, 10}, {20, 20, 20}});

	auto hit = ot.queryPoint({15, 15, 15});
	REQUIRE(hit.size() == 1);

	auto miss = ot.queryPoint({99, 99, 99});
	REQUIRE(miss.empty());
}

TEST_CASE("Octree querySphere", "[spatial][octree]")
{
	sgc::Octreef ot(sgc::AABB3f{{0, 0, 0}, {100, 100, 100}});
	ot.insert(sgc::AABB3f{{10, 10, 10}, {20, 20, 20}});
	ot.insert(sgc::AABB3f{{80, 80, 80}, {90, 90, 90}});

	auto results = ot.querySphere({15, 15, 15}, 10.0f);
	REQUIRE(results.size() == 1);
}

TEST_CASE("Octree queryFrustum", "[spatial][octree]")
{
	sgc::Octreef ot(sgc::AABB3f{{-100, -100, -200}, {100, 100, 0}});
	ot.insert(sgc::AABB3f{{-5, -5, -50}, {5, 5, -40}});    // 視錐台内
	ot.insert(sgc::AABB3f{{90, 90, 90}, {95, 95, 95}});     // 視錐台外

	// 正射影のフラスタムを作成
	const auto view = sgc::Mat4f::lookAt({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
	const auto proj = sgc::Mat4f::orthographic(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 100.0f);
	const auto frustum = sgc::Frustumf::fromViewProjection(proj * view);

	auto results = ot.queryFrustum(frustum);
	REQUIRE(results.size() == 1);
}

TEST_CASE("Octree clear", "[spatial][octree]")
{
	sgc::Octreef ot(sgc::AABB3f{{0, 0, 0}, {100, 100, 100}});
	ot.insert(sgc::AABB3f{{10, 10, 10}, {20, 20, 20}});
	ot.clear();
	REQUIRE(ot.size() == 0);
}

TEST_CASE("Octree many elements triggers subdivision", "[spatial][octree]")
{
	sgc::Octreef ot(sgc::AABB3f{{0, 0, 0}, {1000, 1000, 1000}});

	for (int i = 0; i < 30; ++i)
	{
		const float v = static_cast<float>(i * 30);
		ot.insert(sgc::AABB3f{{v, v, v}, {v + 5, v + 5, v + 5}});
	}
	REQUIRE(ot.size() == 30);

	auto results = ot.queryRange(sgc::AABB3f{{0, 0, 0}, {100, 100, 100}});
	REQUIRE_FALSE(results.empty());
}

TEST_CASE("Octree empty tree query returns empty", "[spatial][octree]")
{
	sgc::Octreef ot(sgc::AABB3f{{0, 0, 0}, {100, 100, 100}});

	auto rangeResults = ot.queryRange(sgc::AABB3f{{0, 0, 0}, {100, 100, 100}});
	REQUIRE(rangeResults.empty());

	auto pointResults = ot.queryPoint({50, 50, 50});
	REQUIRE(pointResults.empty());

	auto sphereResults = ot.querySphere({50, 50, 50}, 10.0f);
	REQUIRE(sphereResults.empty());

	REQUIRE(ot.size() == 0);
}

// ── Raycast ─────────────────────────────────────────────

TEST_CASE("Octree raycast hits elements", "[spatial][octree]")
{
	sgc::Octreef ot(sgc::AABB3f{{0, 0, 0}, {100, 100, 100}});
	ot.insert(sgc::AABB3f{{10, 10, 10}, {20, 20, 20}});
	ot.insert(sgc::AABB3f{{50, 10, 10}, {60, 20, 20}});

	// x方向へのレイ（y=15, z=15）
	sgc::Ray3f ray{{0, 15, 15}, {1, 0, 0}};
	auto hits = ot.raycast(ray);
	REQUIRE(hits.size() == 2);
	REQUIRE(hits[0].distance <= hits[1].distance);
}

TEST_CASE("Octree raycast misses elements", "[spatial][octree]")
{
	sgc::Octreef ot(sgc::AABB3f{{0, 0, 0}, {100, 100, 100}});
	ot.insert(sgc::AABB3f{{10, 10, 10}, {20, 20, 20}});

	// 要素の上を通過するレイ
	sgc::Ray3f ray{{0, 50, 50}, {1, 0, 0}};
	auto hits = ot.raycast(ray);
	REQUIRE(hits.empty());
}

TEST_CASE("Octree raycast sorted by distance", "[spatial][octree]")
{
	sgc::Octreef ot(sgc::AABB3f{{0, 0, 0}, {200, 200, 200}});
	auto h1 = ot.insert(sgc::AABB3f{{80, 0, 0}, {90, 10, 10}});
	auto h2 = ot.insert(sgc::AABB3f{{10, 0, 0}, {20, 10, 10}});

	sgc::Ray3f ray{{0, 5, 5}, {1, 0, 0}};
	auto hits = ot.raycast(ray);
	REQUIRE(hits.size() == 2);
	REQUIRE(hits[0].handle == h2);
	REQUIRE(hits[1].handle == h1);
}

// ── k-Nearest ───────────────────────────────────────────

TEST_CASE("Octree findKNearest returns k nearest", "[spatial][octree]")
{
	sgc::Octreef ot(sgc::AABB3f{{0, 0, 0}, {100, 100, 100}});
	ot.insert(sgc::AABB3f{{10, 10, 10}, {12, 12, 12}});   // 近い
	ot.insert(sgc::AABB3f{{50, 50, 50}, {52, 52, 52}});   // 中間
	ot.insert(sgc::AABB3f{{80, 80, 80}, {82, 82, 82}});   // 遠い

	auto nearest = ot.findKNearest({0, 0, 0}, 2);
	REQUIRE(nearest.size() == 2);
	REQUIRE(nearest[0] == 0);  // 最も近い要素
}

TEST_CASE("Octree findKNearest with k > size", "[spatial][octree]")
{
	sgc::Octreef ot(sgc::AABB3f{{0, 0, 0}, {100, 100, 100}});
	ot.insert(sgc::AABB3f{{10, 10, 10}, {20, 20, 20}});

	auto nearest = ot.findKNearest({0, 0, 0}, 10);
	REQUIRE(nearest.size() == 1);
}

TEST_CASE("Octree findKNearest empty tree", "[spatial][octree]")
{
	sgc::Octreef ot(sgc::AABB3f{{0, 0, 0}, {100, 100, 100}});
	auto nearest = ot.findKNearest({50, 50, 50}, 5);
	REQUIRE(nearest.empty());
}
