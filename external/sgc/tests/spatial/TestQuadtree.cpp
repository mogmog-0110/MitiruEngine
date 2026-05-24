/// @file TestQuadtree.cpp
/// @brief Quadtree.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/spatial/Quadtree.hpp"

TEST_CASE("Quadtree insert and size", "[spatial][quadtree]")
{
	sgc::Quadtreef qt(sgc::AABB2f{{0, 0}, {100, 100}});
	REQUIRE(qt.size() == 0);

	qt.insert(sgc::AABB2f{{10, 10}, {20, 20}});
	REQUIRE(qt.size() == 1);

	qt.insert(sgc::AABB2f{{30, 30}, {40, 40}});
	REQUIRE(qt.size() == 2);
}

TEST_CASE("Quadtree remove", "[spatial][quadtree]")
{
	sgc::Quadtreef qt(sgc::AABB2f{{0, 0}, {100, 100}});
	auto h = qt.insert(sgc::AABB2f{{10, 10}, {20, 20}});
	REQUIRE(qt.size() == 1);

	qt.remove(h);
	REQUIRE(qt.size() == 0);
}

TEST_CASE("Quadtree queryRange finds elements in range", "[spatial][quadtree]")
{
	sgc::Quadtreef qt(sgc::AABB2f{{0, 0}, {100, 100}});
	qt.insert(sgc::AABB2f{{10, 10}, {20, 20}});
	qt.insert(sgc::AABB2f{{50, 50}, {60, 60}});
	qt.insert(sgc::AABB2f{{80, 80}, {90, 90}});

	// 左上四分の一を検索（{50,50}は{50,50}-{60,60}と接触するため2つヒット）
	auto results = qt.queryRange(sgc::AABB2f{{0, 0}, {49, 49}});
	REQUIRE(results.size() == 1);
}

TEST_CASE("Quadtree queryRange finds all in full range", "[spatial][quadtree]")
{
	sgc::Quadtreef qt(sgc::AABB2f{{0, 0}, {100, 100}});
	qt.insert(sgc::AABB2f{{10, 10}, {20, 20}});
	qt.insert(sgc::AABB2f{{50, 50}, {60, 60}});

	auto results = qt.queryRange(sgc::AABB2f{{0, 0}, {100, 100}});
	REQUIRE(results.size() == 2);
}

TEST_CASE("Quadtree queryPoint", "[spatial][quadtree]")
{
	sgc::Quadtreef qt(sgc::AABB2f{{0, 0}, {100, 100}});
	qt.insert(sgc::AABB2f{{10, 10}, {20, 20}});
	qt.insert(sgc::AABB2f{{50, 50}, {60, 60}});

	auto results = qt.queryPoint({15, 15});
	REQUIRE(results.size() == 1);

	auto miss = qt.queryPoint({99, 99});
	REQUIRE(miss.empty());
}

TEST_CASE("Quadtree queryRadius", "[spatial][quadtree]")
{
	sgc::Quadtreef qt(sgc::AABB2f{{0, 0}, {100, 100}});
	qt.insert(sgc::AABB2f{{10, 10}, {20, 20}});
	qt.insert(sgc::AABB2f{{80, 80}, {90, 90}});

	// 半径20の円で原点近くを検索
	auto results = qt.queryRadius({15, 15}, 20.0f);
	REQUIRE(results.size() == 1);
}

TEST_CASE("Quadtree clear removes all elements", "[spatial][quadtree]")
{
	sgc::Quadtreef qt(sgc::AABB2f{{0, 0}, {100, 100}});
	qt.insert(sgc::AABB2f{{10, 10}, {20, 20}});
	qt.insert(sgc::AABB2f{{50, 50}, {60, 60}});

	qt.clear();
	REQUIRE(qt.size() == 0);
}

TEST_CASE("Quadtree handles many elements with subdivision", "[spatial][quadtree]")
{
	sgc::Quadtreef qt(sgc::AABB2f{{0, 0}, {1000, 1000}});

	// 多数の要素を挿入して分割をトリガー
	for (int i = 0; i < 50; ++i)
	{
		const float x = static_cast<float>(i * 20);
		const float y = static_cast<float>(i * 20);
		qt.insert(sgc::AABB2f{{x, y}, {x + 5, y + 5}});
	}
	REQUIRE(qt.size() == 50);

	// 範囲クエリが正しく機能する
	auto results = qt.queryRange(sgc::AABB2f{{0, 0}, {100, 100}});
	REQUIRE_FALSE(results.empty());
}

TEST_CASE("Quadtree update changes element bounds", "[spatial][quadtree]")
{
	sgc::Quadtreef qt(sgc::AABB2f{{0, 0}, {100, 100}});
	auto h = qt.insert(sgc::AABB2f{{10, 10}, {20, 20}});
	qt.update(h, sgc::AABB2f{{80, 80}, {90, 90}});

	const auto& bounds = qt.getBounds(h);
	REQUIRE(bounds.min.x == 80.0f);
}

TEST_CASE("Quadtree empty tree query returns empty", "[spatial][quadtree]")
{
	sgc::Quadtreef qt(sgc::AABB2f{{0, 0}, {100, 100}});

	auto rangeResults = qt.queryRange(sgc::AABB2f{{0, 0}, {100, 100}});
	REQUIRE(rangeResults.empty());

	auto pointResults = qt.queryPoint({50, 50});
	REQUIRE(pointResults.empty());

	auto radiusResults = qt.queryRadius({50, 50}, 10.0f);
	REQUIRE(radiusResults.empty());

	REQUIRE(qt.size() == 0);
}

// ── Raycast ─────────────────────────────────────────────

TEST_CASE("Quadtree raycast hits elements", "[spatial][quadtree]")
{
	sgc::Quadtreef qt(sgc::AABB2f{{0, 0}, {100, 100}});
	qt.insert(sgc::AABB2f{{10, 10}, {20, 20}});
	qt.insert(sgc::AABB2f{{50, 10}, {60, 20}});

	// 左から右へのレイ（y=15）
	sgc::Ray2f ray{{0, 15}, {1, 0}};
	auto hits = qt.raycast(ray);
	REQUIRE(hits.size() == 2);
	REQUIRE(hits[0].distance <= hits[1].distance);
}

TEST_CASE("Quadtree raycast misses elements", "[spatial][quadtree]")
{
	sgc::Quadtreef qt(sgc::AABB2f{{0, 0}, {100, 100}});
	qt.insert(sgc::AABB2f{{10, 10}, {20, 20}});

	// 要素の上を通過するレイ
	sgc::Ray2f ray{{0, 50}, {1, 0}};
	auto hits = qt.raycast(ray);
	REQUIRE(hits.empty());
}

TEST_CASE("Quadtree raycast sorted by distance", "[spatial][quadtree]")
{
	sgc::Quadtreef qt(sgc::AABB2f{{0, 0}, {200, 200}});
	auto h1 = qt.insert(sgc::AABB2f{{80, 0}, {90, 10}});
	auto h2 = qt.insert(sgc::AABB2f{{10, 0}, {20, 10}});

	sgc::Ray2f ray{{0, 5}, {1, 0}};
	auto hits = qt.raycast(ray);
	REQUIRE(hits.size() == 2);
	REQUIRE(hits[0].handle == h2);  // 近い方が先
	REQUIRE(hits[1].handle == h1);
}

// ── k-Nearest ───────────────────────────────────────────

TEST_CASE("Quadtree findKNearest returns k nearest", "[spatial][quadtree]")
{
	sgc::Quadtreef qt(sgc::AABB2f{{0, 0}, {100, 100}});
	qt.insert(sgc::AABB2f{{10, 10}, {12, 12}});   // 距離 ~15.6
	qt.insert(sgc::AABB2f{{50, 50}, {52, 52}});   // 距離 ~72.1
	qt.insert(sgc::AABB2f{{80, 80}, {82, 82}});   // 距離 ~114.5

	auto nearest = qt.findKNearest({0, 0}, 2);
	REQUIRE(nearest.size() == 2);
	REQUIRE(nearest[0] == 0);  // 最も近い要素
}

TEST_CASE("Quadtree findKNearest with k > size", "[spatial][quadtree]")
{
	sgc::Quadtreef qt(sgc::AABB2f{{0, 0}, {100, 100}});
	qt.insert(sgc::AABB2f{{10, 10}, {20, 20}});
	qt.insert(sgc::AABB2f{{50, 50}, {60, 60}});

	auto nearest = qt.findKNearest({0, 0}, 10);
	REQUIRE(nearest.size() == 2);
}

TEST_CASE("Quadtree findKNearest empty tree", "[spatial][quadtree]")
{
	sgc::Quadtreef qt(sgc::AABB2f{{0, 0}, {100, 100}});
	auto nearest = qt.findKNearest({50, 50}, 5);
	REQUIRE(nearest.empty());
}
