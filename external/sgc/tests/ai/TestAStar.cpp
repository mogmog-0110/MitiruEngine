/// @file TestAStar.cpp
/// @brief AStar.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/ai/AStar.hpp"

#include <cmath>

TEST_CASE("GridGraph construction and dimensions", "[ai][astar]")
{
	const sgc::ai::GridGraph grid(10, 8);
	REQUIRE(grid.width() == 10);
	REQUIRE(grid.height() == 8);
}

TEST_CASE("GridGraph walkability", "[ai][astar]")
{
	sgc::ai::GridGraph grid(5, 5);

	// デフォルトは全て通行可能
	REQUIRE(grid.isWalkable(0, 0));
	REQUIRE(grid.isWalkable(4, 4));

	// 障害物を設定
	grid.setWalkable(2, 2, false);
	REQUIRE_FALSE(grid.isWalkable(2, 2));

	// 範囲外は通行不可
	REQUIRE_FALSE(grid.isWalkable(-1, 0));
	REQUIRE_FALSE(grid.isWalkable(5, 0));
}

TEST_CASE("GridGraph 4-directional neighbors", "[ai][astar]")
{
	const sgc::ai::GridGraph grid(5, 5, false);

	// 中央のセル → 4方向
	const auto mid = grid.neighbors({2, 2});
	REQUIRE(mid.size() == 4);

	// 角のセル → 2方向
	const auto corner = grid.neighbors({0, 0});
	REQUIRE(corner.size() == 2);

	// 辺のセル → 3方向
	const auto edge = grid.neighbors({0, 2});
	REQUIRE(edge.size() == 3);
}

TEST_CASE("GridGraph 8-directional neighbors", "[ai][astar]")
{
	const sgc::ai::GridGraph grid(5, 5, true);

	// 中央のセル → 8方向
	const auto mid = grid.neighbors({2, 2});
	REQUIRE(mid.size() == 8);

	// 角のセル → 3方向
	const auto corner = grid.neighbors({0, 0});
	REQUIRE(corner.size() == 3);
}

TEST_CASE("GridGraph neighbors excludes walls", "[ai][astar]")
{
	sgc::ai::GridGraph grid(5, 5, false);
	grid.setWalkable(2, 1, false);
	grid.setWalkable(1, 2, false);

	const auto n = grid.neighbors({2, 2});
	// 上(2,1)と左(1,2)が壁 → 2方向のみ
	REQUIRE(n.size() == 2);
}

TEST_CASE("findPath on empty grid - straight line", "[ai][astar]")
{
	const sgc::ai::GridGraph grid(10, 1, false);

	const auto result = sgc::ai::findPath(grid, {0, 0}, {9, 0});
	REQUIRE(result.has_value());
	REQUIRE(result->path.size() == 10);
	REQUIRE(result->path.front() == sgc::ai::GridPos{0, 0});
	REQUIRE(result->path.back() == sgc::ai::GridPos{9, 0});
	REQUIRE(result->totalCost == Catch::Approx(9.0f));
}

TEST_CASE("findPath same start and goal", "[ai][astar]")
{
	const sgc::ai::GridGraph grid(5, 5);

	const auto result = sgc::ai::findPath(grid, {2, 2}, {2, 2});
	REQUIRE(result.has_value());
	REQUIRE(result->path.size() == 1);
	REQUIRE(result->totalCost == Catch::Approx(0.0f));
}

TEST_CASE("findPath avoids obstacles", "[ai][astar]")
{
	sgc::ai::GridGraph grid(5, 5, false);
	// 壁を作る（縦一列、ただし1か所通路あり）
	grid.setWalkable(2, 0, false);
	grid.setWalkable(2, 1, false);
	grid.setWalkable(2, 2, false);
	grid.setWalkable(2, 3, false);
	// (2,4) は通行可能

	const auto result = sgc::ai::findPath(grid, {0, 0}, {4, 0});
	REQUIRE(result.has_value());

	// パスが壁を通っていないことを確認
	for (const auto& pos : result->path)
	{
		if (pos.x == 2 && pos.y < 4)
		{
			FAIL("Path went through wall at (" << pos.x << "," << pos.y << ")");
		}
	}
}

TEST_CASE("findPath returns nullopt for blocked goal", "[ai][astar]")
{
	sgc::ai::GridGraph grid(5, 5, false);
	// ゴールを完全に囲む
	grid.setWalkable(3, 3, false);
	grid.setWalkable(4, 3, false);
	grid.setWalkable(3, 4, false);
	// (4,4)がゴール、壁(3,3)(4,3)(3,4)で3方向封鎖、残り1方向は範囲外
	// → 4方向移動で到達不可

	const auto result = sgc::ai::findPath(grid, {0, 0}, {4, 4});
	REQUIRE_FALSE(result.has_value());
}

TEST_CASE("findPath diagonal movement", "[ai][astar]")
{
	const sgc::ai::GridGraph grid(5, 5, true);

	const auto result = sgc::ai::findPath(grid, {0, 0}, {4, 4});
	REQUIRE(result.has_value());

	// 斜め移動なら最短は直線4ステップ
	REQUIRE(result->path.size() == 5);
	REQUIRE(result->totalCost == Catch::Approx(4.0f * 1.41421356f));
}

TEST_CASE("findPath with custom graph via concept", "[ai][astar]")
{
	// 単純な線形グラフ（0→1→2→3）
	struct LinearGraph
	{
		using NodeType = int;
		int size = 4;

		std::vector<int> neighbors(int n) const
		{
			std::vector<int> result;
			if (n > 0) result.push_back(n - 1);
			if (n < size - 1) result.push_back(n + 1);
			return result;
		}

		float cost(int, int) const { return 1.0f; }
		float heuristic(int from, int to) const
		{
			return static_cast<float>(std::abs(to - from));
		}
	};

	const LinearGraph graph{4};
	const auto result = sgc::ai::findPath(graph, 0, 3);
	REQUIRE(result.has_value());
	REQUIRE(result->path.size() == 4);
	REQUIRE(result->path[0] == 0);
	REQUIRE(result->path[3] == 3);
	REQUIRE(result->totalCost == Catch::Approx(3.0f));
}
