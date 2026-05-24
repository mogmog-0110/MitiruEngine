#pragma once

/// @file AStar.hpp
/// @brief 汎用A*経路探索アルゴリズム
///
/// テンプレートベースの汎用A*探索と、具象実装として2Dグリッドグラフを提供する。
/// コンセプトによりグラフ型を制約し、任意のグラフ構造に適用可能。
///
/// @code
/// sgc::ai::GridGraph grid(10, 10);
/// grid.setWalkable(3, 3, false); // 障害物
/// auto result = sgc::ai::findPath(grid, {0, 0}, {9, 9});
/// if (result)
/// {
///     for (const auto& node : result->path) { /* ... */ }
/// }
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sgc::ai
{

/// @brief A*探索結果
/// @tparam NodeType ノード型
template <typename NodeType>
struct AStarResult
{
	/// @brief 開始ノードからゴールノードまでのパス
	std::vector<NodeType> path;

	/// @brief パスの合計コスト
	float totalCost = 0.0f;
};

/// @brief グラフ型のコンセプト
///
/// A*探索に必要なインターフェースを定義する。
template <typename G>
concept AStarGraph = requires(const G& g, typename G::NodeType n)
{
	/// ノード型
	typename G::NodeType;

	/// 隣接ノード一覧
	{ g.neighbors(n) } -> std::convertible_to<std::vector<typename G::NodeType>>;

	/// 2ノード間の移動コスト
	{ g.cost(n, n) } -> std::convertible_to<float>;

	/// ヒューリスティック関数（推定コスト）
	{ g.heuristic(n, n) } -> std::convertible_to<float>;
};

/// @brief ノードのハッシュを提供するコンセプト
template <typename N>
concept Hashable = requires(N n)
{
	{ std::hash<N>{}(n) } -> std::convertible_to<std::size_t>;
};

/// @brief 汎用A*経路探索
/// @tparam Graph AStarGraphコンセプトを満たすグラフ型
/// @param graph グラフ
/// @param start 開始ノード
/// @param goal ゴールノード
/// @param maxIterations 最大探索回数（デフォルト: 10000）
/// @return 探索結果（パスが見つからない場合はnullopt）
template <AStarGraph Graph>
	requires Hashable<typename Graph::NodeType>
[[nodiscard]] std::optional<AStarResult<typename Graph::NodeType>> findPath(
	const Graph& graph,
	const typename Graph::NodeType& start,
	const typename Graph::NodeType& goal,
	std::size_t maxIterations = 10000)
{
	using Node = typename Graph::NodeType;

	struct OpenNode
	{
		Node node;
		float fCost = 0.0f;

		bool operator>(const OpenNode& other) const
		{
			return fCost > other.fCost;
		}
	};

	std::priority_queue<OpenNode, std::vector<OpenNode>, std::greater<OpenNode>> open;
	std::unordered_map<Node, float, std::hash<Node>> gScore;
	std::unordered_map<Node, Node, std::hash<Node>> cameFrom;
	std::unordered_set<Node, std::hash<Node>> closed;

	gScore[start] = 0.0f;
	open.push({start, graph.heuristic(start, goal)});

	std::size_t iterations = 0;

	while (!open.empty() && iterations < maxIterations)
	{
		++iterations;
		auto current = open.top();
		open.pop();

		if (current.node == goal)
		{
			// パスを復元
			AStarResult<Node> result;
			result.totalCost = gScore[goal];

			Node n = goal;
			while (!(n == start))
			{
				result.path.push_back(n);
				n = cameFrom[n];
			}
			result.path.push_back(start);
			std::reverse(result.path.begin(), result.path.end());
			return result;
		}

		if (closed.contains(current.node))
		{
			continue;
		}
		closed.insert(current.node);

		for (const auto& neighbor : graph.neighbors(current.node))
		{
			if (closed.contains(neighbor))
			{
				continue;
			}

			const float tentativeG = gScore[current.node] + graph.cost(current.node, neighbor);
			auto it = gScore.find(neighbor);

			if (it == gScore.end() || tentativeG < it->second)
			{
				gScore[neighbor] = tentativeG;
				cameFrom[neighbor] = current.node;
				const float f = tentativeG + graph.heuristic(neighbor, goal);
				open.push({neighbor, f});
			}
		}
	}

	return std::nullopt;
}

// ── GridGraph: 2Dグリッド実装 ──────────────────────────────────

/// @brief 2Dグリッド上の座標
struct GridPos
{
	int x = 0;  ///< X座標
	int y = 0;  ///< Y座標

	[[nodiscard]] bool operator==(const GridPos& other) const noexcept
	{
		return x == other.x && y == other.y;
	}
};

} // namespace sgc::ai

/// @brief GridPos用のハッシュ特殊化
template <>
struct std::hash<sgc::ai::GridPos>
{
	[[nodiscard]] std::size_t operator()(const sgc::ai::GridPos& p) const noexcept
	{
		const auto h1 = std::hash<int>{}(p.x);
		const auto h2 = std::hash<int>{}(p.y);
		return h1 ^ (h2 << 16);
	}
};

namespace sgc::ai
{

/// @brief 2Dグリッドグラフ
///
/// 4方向または8方向移動をサポートする。
/// 各セルは通行可能（walkable）か障害物かを設定できる。
class GridGraph
{
public:
	using NodeType = GridPos;

	/// @brief グリッドを構築する
	/// @param width 幅
	/// @param height 高さ
	/// @param allowDiagonal 斜め移動を許可するか（デフォルト: false）
	GridGraph(int width, int height, bool allowDiagonal = false)
		: m_width(width)
		, m_height(height)
		, m_allowDiagonal(allowDiagonal)
		, m_walkable(static_cast<std::size_t>(width * height), true)
	{
	}

	/// @brief セルの通行可否を設定する
	/// @param x X座標
	/// @param y Y座標
	/// @param walkable 通行可能ならtrue
	void setWalkable(int x, int y, bool walkable)
	{
		if (isInBounds(x, y))
		{
			m_walkable[toIndex(x, y)] = walkable;
		}
	}

	/// @brief セルが通行可能か判定する
	/// @param x X座標
	/// @param y Y座標
	/// @return 通行可能ならtrue
	[[nodiscard]] bool isWalkable(int x, int y) const
	{
		if (!isInBounds(x, y))
		{
			return false;
		}
		return m_walkable[toIndex(x, y)];
	}

	/// @brief 斜め移動の許可を設定する
	/// @param allow 斜め移動を許可するか
	void setAllowDiagonal(bool allow) noexcept
	{
		m_allowDiagonal = allow;
	}

	/// @brief 隣接ノード一覧を取得する
	/// @param pos 現在位置
	/// @return 通行可能な隣接ノード
	[[nodiscard]] std::vector<GridPos> neighbors(const GridPos& pos) const
	{
		static constexpr int dx4[] = {0, 1, 0, -1};
		static constexpr int dy4[] = {-1, 0, 1, 0};
		static constexpr int dx8[] = {0, 1, 1, 1, 0, -1, -1, -1};
		static constexpr int dy8[] = {-1, -1, 0, 1, 1, 1, 0, -1};

		const int count = m_allowDiagonal ? 8 : 4;
		const int* dx = m_allowDiagonal ? dx8 : dx4;
		const int* dy = m_allowDiagonal ? dy8 : dy4;

		std::vector<GridPos> result;
		result.reserve(static_cast<std::size_t>(count));

		for (int i = 0; i < count; ++i)
		{
			const int nx = pos.x + dx[i];
			const int ny = pos.y + dy[i];
			if (isWalkable(nx, ny))
			{
				result.push_back({nx, ny});
			}
		}

		return result;
	}

	/// @brief 2ノード間の移動コスト
	/// @param from 移動元
	/// @param to 移動先
	/// @return 移動コスト（直線:1.0、斜め:√2）
	[[nodiscard]] float cost(const GridPos& from, const GridPos& to) const
	{
		const int dx = std::abs(to.x - from.x);
		const int dy = std::abs(to.y - from.y);
		if (dx + dy == 2)
		{
			return 1.41421356f; // √2
		}
		return 1.0f;
	}

	/// @brief マンハッタン距離またはオクタイル距離のヒューリスティック
	/// @param from 現在位置
	/// @param to ゴール位置
	/// @return 推定コスト
	[[nodiscard]] float heuristic(const GridPos& from, const GridPos& to) const
	{
		const float dx = static_cast<float>(std::abs(to.x - from.x));
		const float dy = static_cast<float>(std::abs(to.y - from.y));
		if (m_allowDiagonal)
		{
			// オクタイル距離
			const float minD = std::min(dx, dy);
			const float maxD = std::max(dx, dy);
			return minD * 1.41421356f + (maxD - minD);
		}
		// マンハッタン距離
		return dx + dy;
	}

	/// @brief グリッド幅を取得する
	[[nodiscard]] int width() const noexcept { return m_width; }

	/// @brief グリッド高さを取得する
	[[nodiscard]] int height() const noexcept { return m_height; }

private:
	/// @brief 座標が範囲内か判定する
	[[nodiscard]] bool isInBounds(int x, int y) const noexcept
	{
		return x >= 0 && x < m_width && y >= 0 && y < m_height;
	}

	/// @brief 2D座標を1Dインデックスに変換する
	[[nodiscard]] std::size_t toIndex(int x, int y) const noexcept
	{
		return static_cast<std::size_t>(y * m_width + x);
	}

	int m_width;
	int m_height;
	bool m_allowDiagonal;
	std::vector<bool> m_walkable;
};

} // namespace sgc::ai
