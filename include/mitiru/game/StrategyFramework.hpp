#pragma once

/// @file StrategyFramework.hpp
/// @brief 戦略/SLGフレームワーク
/// @details ヘックスグリッド、ターン管理、A*パス検索を提供する。
///
/// @code
/// mitiru::game::HexGrid grid(10, 10);
/// grid.getCell(3, 4)->terrain = mitiru::game::TerrainType::Forest;
///
/// mitiru::game::PathfinderHex pathfinder;
/// auto path = pathfinder.findPath(grid, {0, 0}, {5, 3}, 10);
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mitiru::game
{

// ─── ヘックス座標 ───

/// @brief キューブ座標（ヘックス用）
struct HexCube
{
	int q = 0; ///< Q軸
	int r = 0; ///< R軸
	int s = 0; ///< S軸（q + r + s = 0）

	bool operator==(const HexCube& o) const noexcept { return q == o.q && r == o.r && s == o.s; }
	bool operator!=(const HexCube& o) const noexcept { return !(*this == o); }
};

/// @brief オフセット座標（偶数行オフセット）
struct HexOffset
{
	int col = 0; ///< 列
	int row = 0; ///< 行

	bool operator==(const HexOffset& o) const noexcept { return col == o.col && row == o.row; }
	bool operator!=(const HexOffset& o) const noexcept { return !(*this == o); }
};

/// @brief ヘックス座標変換ユーティリティ
struct HexCoordUtil
{
	/// @brief オフセット座標→キューブ座標（偶数行オフセット・flat-top）
	[[nodiscard]] static HexCube offsetToCube(const HexOffset& offset) noexcept
	{
		const int q = offset.col - (offset.row + (offset.row & 1)) / 2;
		const int r = offset.row;
		const int s = -q - r;
		return {q, r, s};
	}

	/// @brief キューブ座標→オフセット座標（偶数行オフセット・flat-top）
	[[nodiscard]] static HexOffset cubeToOffset(const HexCube& cube) noexcept
	{
		const int col = cube.q + (cube.r + (cube.r & 1)) / 2;
		const int row = cube.r;
		return {col, row};
	}

	/// @brief キューブ座標間のヘックス距離
	[[nodiscard]] static int cubeDistance(const HexCube& a, const HexCube& b) noexcept
	{
		return (std::abs(a.q - b.q) + std::abs(a.r - b.r) + std::abs(a.s - b.s)) / 2;
	}
};

// ─── 地形 ───

/// @brief 地形タイプ
enum class TerrainType : std::uint8_t
{
	Plain = 0,  ///< 平地（移動コスト1）
	Forest,     ///< 森（移動コスト2）
	Mountain,   ///< 山（移動コスト3）
	Water,      ///< 水（通行不可）
	Road        ///< 道（移動コスト0.5）
};

/// @brief 地形の移動コストを取得する
[[nodiscard]] inline float terrainMoveCost(TerrainType terrain) noexcept
{
	switch (terrain)
	{
	case TerrainType::Plain:    return 1.0f;
	case TerrainType::Forest:   return 2.0f;
	case TerrainType::Mountain: return 3.0f;
	case TerrainType::Water:    return 99.0f; // 通行不可
	case TerrainType::Road:     return 0.5f;
	default:                    return 1.0f;
	}
}

// ─── ヘックスセル ───

/// @brief ヘックスグリッドの1セル
struct HexCell
{
	int q = 0;                                   ///< オフセット座標・列
	int r = 0;                                   ///< オフセット座標・行
	TerrainType terrain = TerrainType::Plain;     ///< 地形タイプ
	std::uint32_t occupant = 0;                   ///< 占有ユニットID（0=なし）
	float moveCost = 1.0f;                        ///< 移動コスト（地形依存）
};

// ─── ヘックスグリッド ───

/// @brief ヘックスグリッド
/// @details 偶数行オフセット方式のヘックスマップを管理する。
class HexGrid
{
public:
	/// @brief コンストラクタ
	/// @param width グリッド幅（列数）
	/// @param height グリッド高さ（行数）
	HexGrid(int width, int height)
		: m_width(width)
		, m_height(height)
		, m_cells(static_cast<std::size_t>(width * height))
	{
		for (int r = 0; r < height; ++r)
		{
			for (int q = 0; q < width; ++q)
			{
				auto& cell = m_cells[static_cast<std::size_t>(r * width + q)];
				cell.q = q;
				cell.r = r;
			}
		}
	}

	/// @brief デフォルトコンストラクタ
	HexGrid() = default;

	/// @brief グリッド幅
	[[nodiscard]] int width() const noexcept { return m_width; }

	/// @brief グリッド高さ
	[[nodiscard]] int height() const noexcept { return m_height; }

	/// @brief セルを取得する
	/// @param col 列
	/// @param row 行
	/// @return セルへのポインタ（範囲外はnullptr）
	[[nodiscard]] HexCell* getCell(int col, int row) noexcept
	{
		if (!isValid(col, row)) return nullptr;
		return &m_cells[static_cast<std::size_t>(row * m_width + col)];
	}

	/// @brief セルを取得する（const版）
	[[nodiscard]] const HexCell* getCell(int col, int row) const noexcept
	{
		if (!isValid(col, row)) return nullptr;
		return &m_cells[static_cast<std::size_t>(row * m_width + col)];
	}

	/// @brief 座標が有効か
	[[nodiscard]] bool isValid(int col, int row) const noexcept
	{
		return col >= 0 && col < m_width && row >= 0 && row < m_height;
	}

	/// @brief ヘックス距離を計算する
	[[nodiscard]] int hexDistance(const HexOffset& a, const HexOffset& b) const noexcept
	{
		return HexCoordUtil::cubeDistance(
			HexCoordUtil::offsetToCube(a),
			HexCoordUtil::offsetToCube(b));
	}

	/// @brief 隣接セルを取得する
	/// @param col 列
	/// @param row 行
	/// @return 隣接セルのリスト
	[[nodiscard]] std::vector<HexCell*> hexNeighbors(int col, int row) noexcept
	{
		std::vector<HexCell*> neighbors;
		neighbors.reserve(6);

		// 偶数行・奇数行で隣接オフセットが異なる
		static constexpr int evenOffsets[6][2] = {
			{+1, 0}, {0, -1}, {-1, -1}, {-1, 0}, {-1, +1}, {0, +1}
		};
		static constexpr int oddOffsets[6][2] = {
			{+1, 0}, {+1, -1}, {0, -1}, {-1, 0}, {0, +1}, {+1, +1}
		};

		const auto& offsets = (row & 1) == 0 ? evenOffsets : oddOffsets;

		for (const auto& off : offsets)
		{
			const int nc = col + off[0];
			const int nr = row + off[1];
			if (auto* cell = getCell(nc, nr))
			{
				neighbors.push_back(cell);
			}
		}
		return neighbors;
	}

	/// @brief 2点間のヘックスライン（線形補間）
	/// @param from 始点
	/// @param to 終点
	/// @return ライン上のセル
	[[nodiscard]] std::vector<const HexCell*> hexLine(const HexOffset& from, const HexOffset& to) const
	{
		std::vector<const HexCell*> line;
		const auto cubeA = HexCoordUtil::offsetToCube(from);
		const auto cubeB = HexCoordUtil::offsetToCube(to);
		const int dist = HexCoordUtil::cubeDistance(cubeA, cubeB);

		if (dist == 0)
		{
			if (auto* cell = getCell(from.col, from.row))
			{
				line.push_back(cell);
			}
			return line;
		}

		line.reserve(static_cast<std::size_t>(dist + 1));

		for (int i = 0; i <= dist; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(dist);
			const int q = static_cast<int>(std::round(cubeA.q + (cubeB.q - cubeA.q) * t));
			const int r = static_cast<int>(std::round(cubeA.r + (cubeB.r - cubeA.r) * t));
			const int s = -q - r;
			const auto off = HexCoordUtil::cubeToOffset({q, r, s});

			if (auto* cell = getCell(off.col, off.row))
			{
				line.push_back(cell);
			}
		}
		return line;
	}

	/// @brief 指定半径のヘックスリング
	/// @param center 中心座標
	/// @param radius 半径
	/// @return リング上のセル
	[[nodiscard]] std::vector<const HexCell*> hexRing(const HexOffset& center, int radius) const
	{
		if (radius <= 0)
		{
			std::vector<const HexCell*> result;
			if (auto* cell = getCell(center.col, center.row))
			{
				result.push_back(cell);
			}
			return result;
		}

		std::vector<const HexCell*> ring;
		ring.reserve(static_cast<std::size_t>(6 * radius));

		// キューブ座標の6方向
		static constexpr int directions[6][3] = {
			{+1, -1, 0}, {+1, 0, -1}, {0, +1, -1},
			{-1, +1, 0}, {-1, 0, +1}, {0, -1, +1}
		};

		const auto centerCube = HexCoordUtil::offsetToCube(center);
		// 開始位置：direction[4] * radius
		HexCube current = {
			centerCube.q + directions[4][0] * radius,
			centerCube.r + directions[4][1] * radius,
			centerCube.s + directions[4][2] * radius
		};

		for (int dir = 0; dir < 6; ++dir)
		{
			for (int step = 0; step < radius; ++step)
			{
				const auto off = HexCoordUtil::cubeToOffset(current);
				if (auto* cell = getCell(off.col, off.row))
				{
					ring.push_back(cell);
				}
				current.q += directions[dir][0];
				current.r += directions[dir][1];
				current.s += directions[dir][2];
			}
		}

		return ring;
	}

private:
	int m_width = 0;
	int m_height = 0;
	std::vector<HexCell> m_cells;
};

// ─── ターン管理 ───

/// @brief ストラテジーゲームのターン管理
class TurnManager
{
public:
	/// @brief プレイヤーリストを設定する
	/// @param playerIds プレイヤーIDリスト
	void setPlayers(std::vector<std::uint32_t> playerIds)
	{
		m_playerOrder = std::move(playerIds);
		m_currentIndex = 0;
		m_turnNumber = 1;
		m_gameOver = false;
	}

	/// @brief 現在のプレイヤーIDを返す
	[[nodiscard]] std::uint32_t currentPlayer() const noexcept
	{
		if (m_playerOrder.empty()) return 0;
		return m_playerOrder[static_cast<std::size_t>(m_currentIndex)];
	}

	/// @brief 現在のターン番号
	[[nodiscard]] int currentTurn() const noexcept { return m_turnNumber; }

	/// @brief 次のターンに進める
	void nextTurn()
	{
		if (m_gameOver || m_playerOrder.empty()) return;

		++m_currentIndex;
		if (m_currentIndex >= static_cast<int>(m_playerOrder.size()))
		{
			m_currentIndex = 0;
			++m_turnNumber;
		}
	}

	/// @brief ゲーム終了かどうか
	[[nodiscard]] bool isGameOver() const noexcept { return m_gameOver; }

	/// @brief ゲーム終了を設定する
	void setGameOver() noexcept { m_gameOver = true; }

	/// @brief プレイヤー順序を取得する
	[[nodiscard]] const std::vector<std::uint32_t>& playerOrder() const noexcept { return m_playerOrder; }

private:
	std::vector<std::uint32_t> m_playerOrder;
	int m_currentIndex = 0;
	int m_turnNumber = 1;
	bool m_gameOver = false;
};

// ─── ヘックスA*パスファインダー ───

/// @brief ヘックスグリッド上のA*パス検索
class PathfinderHex
{
public:
	/// @brief 最短経路を検索する
	/// @param grid ヘックスグリッド
	/// @param from 始点（オフセット座標）
	/// @param to 終点（オフセット座標）
	/// @param maxMovePoints 最大移動ポイント
	/// @return パス上のセル列（始点含む、到達不能なら空）
	[[nodiscard]] static std::vector<const HexCell*> findPath(
		const HexGrid& grid,
		const HexOffset& from,
		const HexOffset& to,
		float maxMovePoints)
	{
		// A*実装（非const版のgridが必要なため、内部でキャスト）
		auto& mutableGrid = const_cast<HexGrid&>(grid);

		struct Node
		{
			int col, row;
			float gCost;  // 始点からの実コスト
			float fCost;  // gCost + ヒューリスティック

			bool operator>(const Node& o) const noexcept { return fCost > o.fCost; }
		};

		auto key = [&](int col, int row) -> std::int64_t
		{
			return static_cast<std::int64_t>(row) * 100000 + col;
		};

		std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
		std::unordered_map<std::int64_t, float> gScore;
		std::unordered_map<std::int64_t, std::int64_t> cameFrom;

		const auto startKey = key(from.col, from.row);
		const auto goalKey = key(to.col, to.row);

		open.push({from.col, from.row, 0.0f,
			static_cast<float>(grid.hexDistance(from, to))});
		gScore[startKey] = 0.0f;

		while (!open.empty())
		{
			const auto current = open.top();
			open.pop();

			const auto currentKey = key(current.col, current.row);

			if (currentKey == goalKey)
			{
				// パス復元
				return reconstructPath(grid, cameFrom, key, from, to);
			}

			// 既により良い経路で訪問済みならスキップ
			auto gIt = gScore.find(currentKey);
			if (gIt != gScore.end() && current.gCost > gIt->second)
			{
				continue;
			}

			auto neighbors = mutableGrid.hexNeighbors(current.col, current.row);
			for (const auto* neighbor : neighbors)
			{
				// 通行不能チェック
				const float cost = terrainMoveCost(neighbor->terrain);
				if (cost >= 90.0f) continue; // 水等は通行不可
				if (neighbor->occupant != 0) continue; // 占有済み

				const float tentativeG = current.gCost + cost;
				if (tentativeG > maxMovePoints) continue;

				const auto neighborKey = key(neighbor->q, neighbor->r);
				auto nIt = gScore.find(neighborKey);
				if (nIt != gScore.end() && tentativeG >= nIt->second)
				{
					continue;
				}

				gScore[neighborKey] = tentativeG;
				cameFrom[neighborKey] = currentKey;

				const float h = static_cast<float>(
					grid.hexDistance({neighbor->q, neighbor->r}, to));
				open.push({neighbor->q, neighbor->r, tentativeG, tentativeG + h});
			}
		}

		return {}; // 到達不能
	}

	/// @brief 指定移動ポイントで到達可能なセルを取得する
	/// @param grid ヘックスグリッド
	/// @param from 始点
	/// @param movePoints 移動ポイント
	/// @return 到達可能なセルの集合
	[[nodiscard]] static std::vector<const HexCell*> getReachableCells(
		const HexGrid& grid,
		const HexOffset& from,
		float movePoints)
	{
		auto& mutableGrid = const_cast<HexGrid&>(grid);

		struct Entry
		{
			int col, row;
			float cost;

			bool operator>(const Entry& o) const noexcept { return cost > o.cost; }
		};

		auto key = [](int col, int row) -> std::int64_t
		{
			return static_cast<std::int64_t>(row) * 100000 + col;
		};

		std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
		std::unordered_map<std::int64_t, float> visited;
		std::vector<const HexCell*> reachable;

		open.push({from.col, from.row, 0.0f});
		visited[key(from.col, from.row)] = 0.0f;

		while (!open.empty())
		{
			const auto current = open.top();
			open.pop();

			if (auto* cell = grid.getCell(current.col, current.row))
			{
				reachable.push_back(cell);
			}

			auto neighbors = mutableGrid.hexNeighbors(current.col, current.row);
			for (const auto* neighbor : neighbors)
			{
				const float cost = terrainMoveCost(neighbor->terrain);
				if (cost >= 90.0f) continue;

				const float totalCost = current.cost + cost;
				if (totalCost > movePoints) continue;

				const auto nk = key(neighbor->q, neighbor->r);
				auto it = visited.find(nk);
				if (it != visited.end() && totalCost >= it->second)
				{
					continue;
				}

				visited[nk] = totalCost;
				open.push({neighbor->q, neighbor->r, totalCost});
			}
		}

		return reachable;
	}

private:
	/// @brief A*パスを復元する
	[[nodiscard]] static std::vector<const HexCell*> reconstructPath(
		const HexGrid& grid,
		const std::unordered_map<std::int64_t, std::int64_t>& cameFrom,
		const std::function<std::int64_t(int, int)>& key,
		const HexOffset& from,
		const HexOffset& to)
	{
		std::vector<const HexCell*> path;

		auto currentKey = key(to.col, to.row);
		const auto startKey = key(from.col, from.row);

		while (currentKey != startKey)
		{
			const int row = static_cast<int>(currentKey / 100000);
			const int col = static_cast<int>(currentKey % 100000);
			if (auto* cell = grid.getCell(col, row))
			{
				path.push_back(cell);
			}

			auto it = cameFrom.find(currentKey);
			if (it == cameFrom.end()) break;
			currentKey = it->second;
		}

		// 始点を追加
		if (auto* cell = grid.getCell(from.col, from.row))
		{
			path.push_back(cell);
		}

		std::reverse(path.begin(), path.end());
		return path;
	}
};

} // namespace mitiru::game
