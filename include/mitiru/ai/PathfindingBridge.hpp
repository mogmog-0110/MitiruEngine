#pragma once

/// @file PathfindingBridge.hpp
/// @brief 経路探索統合ブリッジ
///
/// A*アルゴリズムによるグリッドベースの経路探索を提供する。
/// NavGridで歩行可能領域を定義し、PathRequestで経路を検索する。
///
/// @code
/// mitiru::ai::NavGrid grid{10, 10, std::vector<bool>(100, true), 1.0f};
/// mitiru::ai::PathfindingBridge pathfinder;
/// pathfinder.setNavGrid(grid);
///
/// auto result = pathfinder.findPath({0, 0, 9, 9});
/// if (result.found) { /* result.path にセル座標列 */ }
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mitiru::ai
{

/// @brief ナビゲーショングリッド
struct NavGrid
{
	int32_t width{0};                  ///< グリッド幅（セル数）
	int32_t height{0};                 ///< グリッド高さ（セル数）
	std::vector<bool> walkable;        ///< 各セルの歩行可能フラグ
	float cellSize{1.0f};              ///< セルのワールド座標サイズ
};

/// @brief 経路探索リクエスト
struct PathRequest
{
	int32_t startX{0};   ///< 開始セルX
	int32_t startY{0};   ///< 開始セルY
	int32_t goalX{0};    ///< 目標セルX
	int32_t goalY{0};    ///< 目標セルY
};

/// @brief 経路探索結果
struct PathResult
{
	std::vector<std::pair<int32_t, int32_t>> path;   ///< 経路セル座標列
	bool found{false};                                ///< 経路が見つかったか
	float cost{0.0f};                                 ///< 経路コスト
};

/// @brief グリッドベース経路探索ブリッジ
class PathfindingBridge
{
public:
	/// @brief ナビゲーショングリッドを設定する
	/// @param grid ナビグリッド
	void setNavGrid(NavGrid grid)
	{
		m_grid = std::move(grid);
	}

	/// @brief グリッドを取得する（const参照）
	/// @return 現在のナビグリッド
	[[nodiscard]] const NavGrid& navGrid() const noexcept
	{
		return m_grid;
	}

	/// @brief セルが歩行可能か確認する
	/// @param x セルX座標
	/// @param y セルY座標
	/// @return 歩行可能ならtrue
	[[nodiscard]] bool isWalkable(int32_t x, int32_t y) const noexcept
	{
		if (!isInBounds(x, y)) return false;
		return m_grid.walkable[static_cast<size_t>(y * m_grid.width + x)];
	}

	/// @brief セルの歩行可能フラグを設定する
	/// @param x セルX座標
	/// @param y セルY座標
	/// @param value 歩行可能か
	void setWalkable(int32_t x, int32_t y, bool value)
	{
		if (!isInBounds(x, y)) return;
		m_grid.walkable[static_cast<size_t>(y * m_grid.width + x)] = value;
	}

	/// @brief A*アルゴリズムで経路を探索する
	/// @param request 経路探索リクエスト
	/// @return 経路探索結果
	[[nodiscard]] PathResult findPath(const PathRequest& request) const
	{
		PathResult result;
		result.found = false;
		result.cost = 0.0f;

		// 範囲外チェック
		if (!isInBounds(request.startX, request.startY) ||
			!isInBounds(request.goalX, request.goalY))
		{
			return result;
		}

		// 歩行不可チェック
		if (!isWalkable(request.startX, request.startY) ||
			!isWalkable(request.goalX, request.goalY))
		{
			return result;
		}

		// 同一セル
		if (request.startX == request.goalX && request.startY == request.goalY)
		{
			result.path.emplace_back(request.startX, request.startY);
			result.found = true;
			result.cost = 0.0f;
			return result;
		}

		// A* 実装
		struct Node
		{
			int32_t x;
			int32_t y;
			float g;   ///< スタートからのコスト
			float f;   ///< g + ヒューリスティック
		};

		// 優先度キュー（fが小さい順）
		auto cmp = [](const Node& a, const Node& b) { return a.f > b.f; };
		std::priority_queue<Node, std::vector<Node>, decltype(cmp)> openSet(cmp);

		const int32_t w = m_grid.width;

		// コストマップと親マップ
		std::unordered_map<int32_t, float> gScore;
		std::unordered_map<int32_t, int32_t> cameFrom;

		auto toIndex = [w](int32_t x, int32_t y) -> int32_t
		{
			return y * w + x;
		};

		auto heuristic = [](int32_t x1, int32_t y1, int32_t x2, int32_t y2) -> float
		{
			// マンハッタン距離
			return static_cast<float>(std::abs(x2 - x1) + std::abs(y2 - y1));
		};

		const int32_t startIdx = toIndex(request.startX, request.startY);
		const int32_t goalIdx = toIndex(request.goalX, request.goalY);

		gScore[startIdx] = 0.0f;
		openSet.push({request.startX, request.startY, 0.0f,
			heuristic(request.startX, request.startY, request.goalX, request.goalY)});

		// 4方向移動
		constexpr int32_t dx[] = {0, 1, 0, -1};
		constexpr int32_t dy[] = {-1, 0, 1, 0};

		std::unordered_map<int32_t, bool> closedSet;

		while (!openSet.empty())
		{
			const Node current = openSet.top();
			openSet.pop();

			const int32_t curIdx = toIndex(current.x, current.y);

			// ゴール到達
			if (curIdx == goalIdx)
			{
				// 経路を復元
				result.found = true;
				result.cost = current.g;

				std::vector<std::pair<int32_t, int32_t>> reversedPath;
				int32_t traceIdx = goalIdx;
				while (traceIdx != startIdx)
				{
					const int32_t ty = traceIdx / w;
					const int32_t tx = traceIdx % w;
					reversedPath.emplace_back(tx, ty);
					traceIdx = cameFrom[traceIdx];
				}
				reversedPath.emplace_back(request.startX, request.startY);

				result.path.reserve(reversedPath.size());
				for (auto it = reversedPath.rbegin(); it != reversedPath.rend(); ++it)
				{
					result.path.push_back(*it);
				}
				return result;
			}

			if (closedSet[curIdx]) continue;
			closedSet[curIdx] = true;

			// 隣接セルを展開
			for (int32_t dir = 0; dir < 4; ++dir)
			{
				const int32_t nx = current.x + dx[dir];
				const int32_t ny = current.y + dy[dir];

				if (!isInBounds(nx, ny)) continue;
				if (!isWalkable(nx, ny)) continue;

				const int32_t nIdx = toIndex(nx, ny);
				if (closedSet[nIdx]) continue;

				const float tentativeG = current.g + 1.0f;

				auto gIt = gScore.find(nIdx);
				if (gIt != gScore.end() && tentativeG >= gIt->second) continue;

				gScore[nIdx] = tentativeG;
				cameFrom[nIdx] = curIdx;

				const float f = tentativeG + heuristic(nx, ny, request.goalX, request.goalY);
				openSet.push({nx, ny, tentativeG, f});
			}
		}

		// 経路なし
		return result;
	}

	/// @brief 経路を平滑化する（角のカット）
	/// @param input 元の経路結果
	/// @return 平滑化された経路結果
	[[nodiscard]] PathResult smoothPath(const PathResult& input) const
	{
		PathResult result;
		result.found = input.found;
		result.cost = input.cost;

		if (!input.found || input.path.size() <= 2)
		{
			result.path = input.path;
			return result;
		}

		// 簡易コーナーカット: 直線で到達可能なら中間点をスキップ
		result.path.push_back(input.path.front());

		size_t current = 0;
		while (current < input.path.size() - 1)
		{
			size_t farthest = current + 1;

			// 現在地点からできるだけ遠くまで直線到達可能か確認
			for (size_t candidate = current + 2; candidate < input.path.size(); ++candidate)
			{
				if (hasLineOfSight(
					input.path[current].first, input.path[current].second,
					input.path[candidate].first, input.path[candidate].second))
				{
					farthest = candidate;
				}
			}

			result.path.push_back(input.path[farthest]);
			current = farthest;
		}

		return result;
	}

private:
	/// @brief 座標がグリッド範囲内か確認する
	/// @param x セルX座標
	/// @param y セルY座標
	/// @return 範囲内ならtrue
	[[nodiscard]] bool isInBounds(int32_t x, int32_t y) const noexcept
	{
		return x >= 0 && x < m_grid.width && y >= 0 && y < m_grid.height;
	}

	/// @brief 2点間の直線見通しを確認する（Bresenhamアルゴリズム）
	/// @param x0 始点X
	/// @param y0 始点Y
	/// @param x1 終点X
	/// @param y1 終点Y
	/// @return 全セルが歩行可能ならtrue
	[[nodiscard]] bool hasLineOfSight(int32_t x0, int32_t y0, int32_t x1, int32_t y1) const
	{
		int32_t dxAbs = std::abs(x1 - x0);
		int32_t dyAbs = std::abs(y1 - y0);
		int32_t sx = (x0 < x1) ? 1 : -1;
		int32_t sy = (y0 < y1) ? 1 : -1;
		int32_t err = dxAbs - dyAbs;

		int32_t cx = x0;
		int32_t cy = y0;

		while (true)
		{
			if (!isWalkable(cx, cy)) return false;
			if (cx == x1 && cy == y1) break;

			const int32_t e2 = 2 * err;
			if (e2 > -dyAbs)
			{
				err -= dyAbs;
				cx += sx;
			}
			if (e2 < dxAbs)
			{
				err += dxAbs;
				cy += sy;
			}
		}

		return true;
	}

	NavGrid m_grid;  ///< ナビゲーショングリッド
};

} // namespace mitiru::ai
