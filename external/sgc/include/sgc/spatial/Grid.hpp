#pragma once

/// @file Grid.hpp
/// @brief 2Dグリッドとパスファインディング
///
/// タイルベースゲーム用の2Dグリッド。
/// A*アルゴリズムによる最短経路探索を含む。

#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <queue>
#include <vector>

#include "sgc/math/Vec2.hpp"
#include "sgc/types/Concepts.hpp"

namespace sgc
{

/// @brief グリッド座標
struct GridCoord
{
	int x{};  ///< X座標
	int y{};  ///< Y座標

	[[nodiscard]] constexpr bool operator==(const GridCoord& rhs) const noexcept = default;
};

/// @brief 2Dグリッド
/// @tparam CellType セルの型
///
/// @code
/// sgc::Grid<int> grid(10, 10, 0);
/// grid.at(3, 4) = 1;  // 壁
/// grid.forEachNeighbor4({5, 5}, [](int x, int y, int& cell) {
///     // 4方向の隣接セルを処理
/// });
/// @endcode
template <typename CellType>
class Grid
{
public:
	/// @brief 幅・高さ・初期値を指定して構築する
	/// @param width 幅
	/// @param height 高さ
	/// @param defaultValue デフォルト値
	Grid(int width, int height, const CellType& defaultValue = CellType{})
		: m_width(width), m_height(height)
		, m_cells(static_cast<std::size_t>(width * height), defaultValue)
	{
	}

	/// @brief セルへの参照を返す
	/// @param x X座標
	/// @param y Y座標
	[[nodiscard]] CellType& at(int x, int y)
	{
		return m_cells[static_cast<std::size_t>(y * m_width + x)];
	}

	/// @brief セルへのconst参照を返す
	[[nodiscard]] const CellType& at(int x, int y) const
	{
		return m_cells[static_cast<std::size_t>(y * m_width + x)];
	}

	/// @brief GridCoordでアクセスする
	[[nodiscard]] CellType& at(const GridCoord& coord) { return at(coord.x, coord.y); }
	[[nodiscard]] const CellType& at(const GridCoord& coord) const { return at(coord.x, coord.y); }

	/// @brief 座標が有効かどうか判定する
	[[nodiscard]] constexpr bool isValid(int x, int y) const noexcept
	{
		return x >= 0 && x < m_width && y >= 0 && y < m_height;
	}

	/// @brief グリッドの幅を返す
	[[nodiscard]] int width() const noexcept { return m_width; }

	/// @brief グリッドの高さを返す
	[[nodiscard]] int height() const noexcept { return m_height; }

	/// @brief 全セルを指定値で埋める
	void fill(const CellType& value)
	{
		std::fill(m_cells.begin(), m_cells.end(), value);
	}

	/// @brief 全セルをデフォルト値でクリアする
	void clear()
	{
		fill(CellType{});
	}

	/// @brief 4方向の隣接セルに対してコールバックを実行する
	/// @param coord 中心座標
	/// @param func コールバック (int x, int y, CellType& cell)
	template <typename Func>
	void forEachNeighbor4(const GridCoord& coord, Func&& func)
	{
		constexpr int dx[] = {0, 0, -1, 1};
		constexpr int dy[] = {-1, 1, 0, 0};
		for (int i = 0; i < 4; ++i)
		{
			const int nx = coord.x + dx[i];
			const int ny = coord.y + dy[i];
			if (isValid(nx, ny))
			{
				func(nx, ny, at(nx, ny));
			}
		}
	}

	/// @brief 8方向の隣接セルに対してコールバックを実行する
	/// @param coord 中心座標
	/// @param func コールバック (int x, int y, CellType& cell)
	template <typename Func>
	void forEachNeighbor8(const GridCoord& coord, Func&& func)
	{
		for (int dy = -1; dy <= 1; ++dy)
		{
			for (int dx = -1; dx <= 1; ++dx)
			{
				if (dx == 0 && dy == 0) continue;
				const int nx = coord.x + dx;
				const int ny = coord.y + dy;
				if (isValid(nx, ny))
				{
					func(nx, ny, at(nx, ny));
				}
			}
		}
	}

	/// @brief ワールド座標をグリッド座標に変換する
	/// @param worldPos ワールド座標
	/// @param cellSize セルサイズ
	template <FloatingPoint T>
	[[nodiscard]] static GridCoord worldToGrid(const Vec2<T>& worldPos, T cellSize) noexcept
	{
		return {
			static_cast<int>(std::floor(worldPos.x / cellSize)),
			static_cast<int>(std::floor(worldPos.y / cellSize))
		};
	}

	/// @brief グリッド座標をワールド座標（セル中心）に変換する
	/// @param coord グリッド座標
	/// @param cellSize セルサイズ
	template <FloatingPoint T>
	[[nodiscard]] static Vec2<T> gridToWorld(const GridCoord& coord, T cellSize) noexcept
	{
		return {
			(static_cast<T>(coord.x) + T{0.5}) * cellSize,
			(static_cast<T>(coord.y) + T{0.5}) * cellSize
		};
	}

	/// @brief フラッドフィル
	/// @param start 開始座標
	/// @param predicate 充填可能かの判定関数
	/// @param fillValue 充填する値
	void floodFill(const GridCoord& start, std::function<bool(const CellType&)> predicate,
		const CellType& fillValue)
	{
		if (!isValid(start.x, start.y)) return;
		if (!predicate(at(start))) return;

		std::queue<GridCoord> queue;
		queue.push(start);

		while (!queue.empty())
		{
			const auto current = queue.front();
			queue.pop();

			if (!isValid(current.x, current.y)) continue;
			if (!predicate(at(current))) continue;

			at(current) = fillValue;

			queue.push({current.x + 1, current.y});
			queue.push({current.x - 1, current.y});
			queue.push({current.x, current.y + 1});
			queue.push({current.x, current.y - 1});
		}
	}

	/// @brief ブレゼンハムの直線アルゴリズム
	/// @param from 始点
	/// @param to 終点
	/// @return 直線上の座標リスト
	[[nodiscard]] static std::vector<GridCoord> line(const GridCoord& from, const GridCoord& to)
	{
		std::vector<GridCoord> result;

		int x0 = from.x, y0 = from.y;
		const int x1 = to.x, y1 = to.y;
		const int dx = std::abs(x1 - x0);
		const int dy = -std::abs(y1 - y0);
		const int sx = (x0 < x1) ? 1 : -1;
		const int sy = (y0 < y1) ? 1 : -1;
		int err = dx + dy;

		for (;;)
		{
			result.push_back({x0, y0});
			if (x0 == x1 && y0 == y1) break;
			const int e2 = 2 * err;
			if (e2 >= dy) { err += dy; x0 += sx; }
			if (e2 <= dx) { err += dx; y0 += sy; }
		}

		return result;
	}

private:
	int m_width;
	int m_height;
	std::vector<CellType> m_cells;
};

// ── ヒューリスティック関数 ──────────────────────────────────────

/// @brief A*用ヒューリスティック関数
namespace heuristic
{

/// @brief マンハッタン距離
[[nodiscard]] inline float manhattan(const GridCoord& a, const GridCoord& b) noexcept
{
	return static_cast<float>(std::abs(a.x - b.x) + std::abs(a.y - b.y));
}

/// @brief ユークリッド距離
[[nodiscard]] inline float euclidean(const GridCoord& a, const GridCoord& b) noexcept
{
	const float dx = static_cast<float>(a.x - b.x);
	const float dy = static_cast<float>(a.y - b.y);
	return std::sqrt(dx * dx + dy * dy);
}

/// @brief チェビシェフ距離
[[nodiscard]] inline float chebyshev(const GridCoord& a, const GridCoord& b) noexcept
{
	const int dx = std::abs(a.x - b.x);
	const int dy = std::abs(a.y - b.y);
	return static_cast<float>((dx > dy) ? dx : dy);
}

} // namespace heuristic

/// @brief A*パスファインディング
/// @tparam CellType セルの型
/// @param grid グリッド
/// @param start 開始座標
/// @param goal 目標座標
/// @param costFunc コスト関数 (GridCoord from, GridCoord to, const CellType& cell) → float
/// @param heuristicFunc ヒューリスティック関数
/// @return パス（見つからない場合は空）
///
/// @note costFunc が負を返すと通行不可として扱う
template <typename CellType>
[[nodiscard]] std::vector<GridCoord> findPathAStar(
	const Grid<CellType>& grid,
	const GridCoord& start,
	const GridCoord& goal,
	std::function<float(GridCoord, GridCoord, const CellType&)> costFunc,
	std::function<float(const GridCoord&, const GridCoord&)> heuristicFunc = heuristic::manhattan)
{
	if (!grid.isValid(start.x, start.y) || !grid.isValid(goal.x, goal.y))
	{
		return {};
	}

	struct Node
	{
		GridCoord coord;
		float f;  // g + h
		bool operator>(const Node& other) const noexcept { return f > other.f; }
	};

	const int w = grid.width();
	const int h = grid.height();
	const auto idx = [w](const GridCoord& c) { return static_cast<std::size_t>(c.y * w + c.x); };

	std::vector<float> gScore(static_cast<std::size_t>(w * h), std::numeric_limits<float>::max());
	std::vector<GridCoord> cameFrom(static_cast<std::size_t>(w * h), {-1, -1});
	std::vector<bool> closed(static_cast<std::size_t>(w * h), false);

	std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;

	gScore[idx(start)] = 0.0f;
	open.push({start, heuristicFunc(start, goal)});

	constexpr int dx[] = {0, 0, -1, 1};
	constexpr int dy[] = {-1, 1, 0, 0};

	while (!open.empty())
	{
		const auto current = open.top();
		open.pop();

		if (current.coord == goal)
		{
			// パスを再構築
			std::vector<GridCoord> path;
			GridCoord c = goal;
			while (!(c == start))
			{
				path.push_back(c);
				c = cameFrom[idx(c)];
			}
			path.push_back(start);
			std::reverse(path.begin(), path.end());
			return path;
		}

		if (closed[idx(current.coord)]) continue;
		closed[idx(current.coord)] = true;

		for (int i = 0; i < 4; ++i)
		{
			const GridCoord neighbor{current.coord.x + dx[i], current.coord.y + dy[i]};
			if (!grid.isValid(neighbor.x, neighbor.y)) continue;
			if (closed[idx(neighbor)]) continue;

			const float moveCost = costFunc(current.coord, neighbor, grid.at(neighbor));
			if (moveCost < 0.0f) continue;  // 通行不可

			const float tentativeG = gScore[idx(current.coord)] + moveCost;
			if (tentativeG < gScore[idx(neighbor)])
			{
				gScore[idx(neighbor)] = tentativeG;
				cameFrom[idx(neighbor)] = current.coord;
				open.push({neighbor, tentativeG + heuristicFunc(neighbor, goal)});
			}
		}
	}

	return {};  // パスが見つからない
}

} // namespace sgc
