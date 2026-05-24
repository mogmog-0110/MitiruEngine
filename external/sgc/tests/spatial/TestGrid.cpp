/// @file TestGrid.cpp
/// @brief Grid.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <functional>

#include "sgc/spatial/Grid.hpp"

using Catch::Approx;

// ── Grid basics ──────────────────────────────────────────────────

TEST_CASE("Grid construction and dimensions", "[spatial][grid]")
{
	sgc::Grid<int> grid(10, 8, 0);
	REQUIRE(grid.width() == 10);
	REQUIRE(grid.height() == 8);
}

TEST_CASE("Grid at and fill", "[spatial][grid]")
{
	sgc::Grid<int> grid(5, 5, 0);
	grid.fill(42);
	REQUIRE(grid.at(0, 0) == 42);
	REQUIRE(grid.at(4, 4) == 42);
}

TEST_CASE("Grid at with GridCoord", "[spatial][grid]")
{
	sgc::Grid<int> grid(5, 5, 0);
	grid.at(sgc::GridCoord{2, 3}) = 99;
	REQUIRE(grid.at(2, 3) == 99);
}

TEST_CASE("Grid isValid", "[spatial][grid]")
{
	sgc::Grid<int> grid(10, 10);
	REQUIRE(grid.isValid(0, 0));
	REQUIRE(grid.isValid(9, 9));
	REQUIRE_FALSE(grid.isValid(-1, 0));
	REQUIRE_FALSE(grid.isValid(10, 0));
	REQUIRE_FALSE(grid.isValid(0, 10));
}

TEST_CASE("Grid clear resets to default", "[spatial][grid]")
{
	sgc::Grid<int> grid(5, 5, 99);
	grid.clear();
	REQUIRE(grid.at(0, 0) == 0);
}

// ── Neighbors ────────────────────────────────────────────────────

TEST_CASE("Grid forEachNeighbor4 visits 4 cells", "[spatial][grid]")
{
	sgc::Grid<int> grid(5, 5, 0);
	int count = 0;
	grid.forEachNeighbor4({2, 2}, [&count](int, int, int&) { ++count; });
	REQUIRE(count == 4);
}

TEST_CASE("Grid forEachNeighbor4 at corner visits 2 cells", "[spatial][grid]")
{
	sgc::Grid<int> grid(5, 5, 0);
	int count = 0;
	grid.forEachNeighbor4({0, 0}, [&count](int, int, int&) { ++count; });
	REQUIRE(count == 2);
}

TEST_CASE("Grid forEachNeighbor8 visits 8 cells", "[spatial][grid]")
{
	sgc::Grid<int> grid(5, 5, 0);
	int count = 0;
	grid.forEachNeighbor8({2, 2}, [&count](int, int, int&) { ++count; });
	REQUIRE(count == 8);
}

// ── Coordinate conversion ────────────────────────────────────────

TEST_CASE("Grid worldToGrid converts correctly", "[spatial][grid]")
{
	const auto coord = sgc::Grid<int>::worldToGrid(sgc::Vec2f{25.0f, 35.0f}, 10.0f);
	REQUIRE(coord.x == 2);
	REQUIRE(coord.y == 3);
}

TEST_CASE("Grid gridToWorld returns cell center", "[spatial][grid]")
{
	const auto world = sgc::Grid<int>::gridToWorld<float>({2, 3}, 10.0f);
	REQUIRE(world.x == Approx(25.0f));
	REQUIRE(world.y == Approx(35.0f));
}

// ── Bresenham line ───────────────────────────────────────────────

TEST_CASE("Grid line horizontal", "[spatial][grid]")
{
	const auto line = sgc::Grid<int>::line({0, 0}, {5, 0});
	REQUIRE(line.size() == 6);
	REQUIRE(line.front() == sgc::GridCoord{0, 0});
	REQUIRE(line.back() == sgc::GridCoord{5, 0});
}

TEST_CASE("Grid line diagonal", "[spatial][grid]")
{
	const auto line = sgc::Grid<int>::line({0, 0}, {3, 3});
	REQUIRE(line.size() == 4);
	REQUIRE(line[0] == sgc::GridCoord{0, 0});
	REQUIRE(line[3] == sgc::GridCoord{3, 3});
}

// ── Flood fill ───────────────────────────────────────────────────

TEST_CASE("Grid floodFill fills connected region", "[spatial][grid]")
{
	sgc::Grid<int> grid(5, 5, 0);
	// 壁で囲む
	grid.at(1, 0) = 1;
	grid.at(1, 1) = 1;
	grid.at(1, 2) = 1;
	grid.at(0, 2) = 1;

	grid.floodFill({0, 0}, [](const int& cell) { return cell == 0; }, 2);

	// 囲まれた領域だけ埋まる
	REQUIRE(grid.at(0, 0) == 2);
	REQUIRE(grid.at(0, 1) == 2);
	// 壁の向こうは埋まらない
	REQUIRE(grid.at(2, 0) == 0);
}

// ── A* pathfinding ───────────────────────────────────────────────

TEST_CASE("findPathAStar finds simple path", "[spatial][grid]")
{
	sgc::Grid<int> grid(10, 10, 0);

	std::function<float(sgc::GridCoord, sgc::GridCoord, const int&)> cost =
		[](sgc::GridCoord, sgc::GridCoord, const int& cell) -> float {
		return (cell == 1) ? -1.0f : 1.0f;  // 1 = wall
	};

	const auto path = sgc::findPathAStar(grid, sgc::GridCoord{0, 0}, sgc::GridCoord{9, 9}, cost);
	REQUIRE_FALSE(path.empty());
	REQUIRE(path.front() == sgc::GridCoord{0, 0});
	REQUIRE(path.back() == sgc::GridCoord{9, 9});
}

TEST_CASE("findPathAStar returns empty when blocked", "[spatial][grid]")
{
	sgc::Grid<int> grid(5, 5, 0);
	// 完全な壁
	for (int y = 0; y < 5; ++y) grid.at(2, y) = 1;

	std::function<float(sgc::GridCoord, sgc::GridCoord, const int&)> cost =
		[](sgc::GridCoord, sgc::GridCoord, const int& cell) -> float {
		return (cell == 1) ? -1.0f : 1.0f;
	};

	const auto path = sgc::findPathAStar(grid, sgc::GridCoord{0, 0}, sgc::GridCoord{4, 4}, cost);
	REQUIRE(path.empty());
}

TEST_CASE("findPathAStar avoids obstacles", "[spatial][grid]")
{
	sgc::Grid<int> grid(5, 5, 0);
	grid.at(1, 0) = 1;
	grid.at(1, 1) = 1;
	grid.at(1, 2) = 1;
	// 通り道は (1,3) か (1,4)

	std::function<float(sgc::GridCoord, sgc::GridCoord, const int&)> cost =
		[](sgc::GridCoord, sgc::GridCoord, const int& cell) -> float {
		return (cell == 1) ? -1.0f : 1.0f;
	};

	const auto path = sgc::findPathAStar(grid, sgc::GridCoord{0, 0}, sgc::GridCoord{2, 0}, cost);
	REQUIRE_FALSE(path.empty());
	// パスは壁を通らない
	for (const auto& coord : path)
	{
		const int val = grid.at(coord);
		REQUIRE(val == 0);
	}
}

TEST_CASE("findPathAStar start equals goal", "[spatial][grid]")
{
	sgc::Grid<int> grid(5, 5, 0);
	std::function<float(sgc::GridCoord, sgc::GridCoord, const int&)> cost =
		[](sgc::GridCoord, sgc::GridCoord, const int&) -> float { return 1.0f; };
	const auto path = sgc::findPathAStar(grid, sgc::GridCoord{2, 2}, sgc::GridCoord{2, 2}, cost);
	REQUIRE(path.size() == 1);
	REQUIRE(path[0] == sgc::GridCoord{2, 2});
}

// ── Heuristics ───────────────────────────────────────────────────

TEST_CASE("heuristic manhattan distance", "[spatial][grid]")
{
	REQUIRE(sgc::heuristic::manhattan({0, 0}, {3, 4}) == Approx(7.0f));
}

TEST_CASE("heuristic euclidean distance", "[spatial][grid]")
{
	REQUIRE(sgc::heuristic::euclidean({0, 0}, {3, 4}) == Approx(5.0f));
}

TEST_CASE("heuristic chebyshev distance", "[spatial][grid]")
{
	REQUIRE(sgc::heuristic::chebyshev({0, 0}, {3, 4}) == Approx(4.0f));
}
