#pragma once

/// @file Pathfinder.hpp
/// @brief Grid2D上のA*経路探索（4方向）
/// @details static findPath()で最短経路を計算する。

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <unordered_map>
#include <vector>

#include <mitiru/util/Grid2D.hpp>

namespace mitiru::util
{

/// @brief Grid2D上のA*経路探索
struct Pathfinder
{
	/// @brief 座標ペア型
	using Pos = std::pair<int, int>;

	/// @brief 歩行可能判定関数型（セル値→通行可能か）
	using WalkableFunc = std::function<bool(int cellValue)>;

	/// @brief Grid2D上でA*探索を行い最短経路を返す（4方向移動）
	/// @param grid 探索対象のグリッド
	/// @param start 開始座標
	/// @param goal 目標座標
	/// @param isWalkable セル値が通行可能かを判定する関数
	/// @return 経路（start含む、goal含む）。経路が見つからない場合は空ベクタ
	[[nodiscard]] static std::vector<Pos> findPath(
		const Grid2D<int>& grid,
		Pos start,
		Pos goal,
		const WalkableFunc& isWalkable)
	{
		/// start==goalの特殊ケース
		if (start == goal)
		{
			return {start};
		}

		/// startまたはgoalが範囲外の場合
		if (!grid.inBounds(start.first, start.second) ||
		    !grid.inBounds(goal.first, goal.second))
		{
			return {};
		}

		/// goalが通行不可の場合
		if (!isWalkable(grid.at(goal.first, goal.second)))
		{
			return {};
		}

		/// ノード情報
		struct Node
		{
			Pos pos;
			float f = 0.0f;
			bool operator>(const Node& other) const { return f > other.f; }
		};

		/// マンハッタン距離ヒューリスティック
		auto heuristic = [](Pos a, Pos b) -> float
		{
			return static_cast<float>(std::abs(a.first - b.first) + std::abs(a.second - b.second));
		};

		/// 座標→ハッシュ
		auto posHash = [w = grid.width()](Pos p) -> std::size_t
		{
			return static_cast<std::size_t>(p.second * w + p.first);
		};

		std::priority_queue<Node, std::vector<Node>, std::greater<Node>> openSet;
		std::unordered_map<std::size_t, float> gScore;
		std::unordered_map<std::size_t, Pos> cameFrom;

		const auto startKey = posHash(start);
		gScore[startKey] = 0.0f;
		openSet.push({start, heuristic(start, goal)});

		/// 4方向の隣接オフセット
		static constexpr int DX[] = {0, 0, -1, 1};
		static constexpr int DY[] = {-1, 1, 0, 0};

		while (!openSet.empty())
		{
			const auto current = openSet.top();
			openSet.pop();

			if (current.pos == goal)
			{
				/// 経路を復元する
				std::vector<Pos> path;
				Pos cur = goal;
				while (cur != start)
				{
					path.push_back(cur);
					cur = cameFrom[posHash(cur)];
				}
				path.push_back(start);
				std::reverse(path.begin(), path.end());
				return path;
			}

			const auto currentKey = posHash(current.pos);
			const float currentG = gScore[currentKey];

			for (int d = 0; d < 4; ++d)
			{
				const Pos neighbor{current.pos.first + DX[d], current.pos.second + DY[d]};

				if (!grid.inBounds(neighbor.first, neighbor.second)) continue;
				if (!isWalkable(grid.at(neighbor.first, neighbor.second))) continue;

				const float tentativeG = currentG + 1.0f;
				const auto neighborKey = posHash(neighbor);

				auto it = gScore.find(neighborKey);
				if (it == gScore.end() || tentativeG < it->second)
				{
					gScore[neighborKey] = tentativeG;
					cameFrom[neighborKey] = current.pos;
					openSet.push({neighbor, tentativeG + heuristic(neighbor, goal)});
				}
			}
		}

		/// 経路が見つからなかった
		return {};
	}
};

} // namespace mitiru::util
