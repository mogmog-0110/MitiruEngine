#pragma once

/// @file DungeonGenerator.hpp
/// @brief BSPアルゴリズムによるダンジョン自動生成
///
/// Binary Space Partitioning（BSP）を用いて、
/// 部屋と通路からなるランダムなダンジョンマップを生成する。
///
/// @code
/// sgc::procedural::DungeonConfig config;
/// config.mapWidth = 80;
/// config.mapHeight = 50;
/// config.maxDepth = 5;
/// auto result = sgc::procedural::generateDungeon(config, 42);
/// // result.rooms, result.corridors, result.tiles が利用可能
/// @endcode

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

namespace sgc::procedural
{

/// @brief タイル種別
enum class TileType : uint8_t
{
	Wall = 0,  ///< 壁
	Floor,     ///< 床
	Corridor,  ///< 通路
};

/// @brief 部屋を表す矩形
struct Room
{
	int x = 0;       ///< 左上X座標
	int y = 0;       ///< 左上Y座標
	int width = 0;   ///< 幅
	int height = 0;  ///< 高さ

	/// @brief 部屋の中心X座標
	[[nodiscard]] constexpr int centerX() const noexcept { return x + width / 2; }

	/// @brief 部屋の中心Y座標
	[[nodiscard]] constexpr int centerY() const noexcept { return y + height / 2; }
};

/// @brief 通路（2点間の直線パス）
struct Corridor
{
	int startX = 0;  ///< 開始X座標
	int startY = 0;  ///< 開始Y座標
	int endX = 0;    ///< 終了X座標
	int endY = 0;    ///< 終了Y座標
};

/// @brief ダンジョン生成の設定
struct DungeonConfig
{
	int mapWidth = 80;      ///< マップ幅
	int mapHeight = 50;     ///< マップ高さ
	int minRoomSize = 5;    ///< 部屋の最小サイズ
	int maxRoomSize = 15;   ///< 部屋の最大サイズ
	int maxDepth = 5;       ///< BSP分割の最大深度
	int padding = 1;        ///< 部屋と領域端の間隔
};

/// @brief ダンジョン生成結果
struct DungeonResult
{
	std::vector<Room> rooms;            ///< 生成された部屋一覧
	std::vector<Corridor> corridors;    ///< 生成された通路一覧
	std::vector<std::vector<TileType>> tiles;  ///< 2Dタイルグリッド
	int width = 0;   ///< マップ幅
	int height = 0;  ///< マップ高さ
};

namespace detail
{

/// @brief BSPノード（内部用）
struct BspNode
{
	int x, y, w, h;
	int roomIdx = -1;      ///< この葉に割り当てられた部屋のインデックス（-1=なし）
	int leftChild = -1;    ///< 左子ノードのインデックス
	int rightChild = -1;   ///< 右子ノードのインデックス

	[[nodiscard]] constexpr bool isLeaf() const noexcept
	{
		return leftChild == -1 && rightChild == -1;
	}
};

/// @brief BSPツリーを分割し部屋と通路を生成する
inline void splitBsp(
	std::vector<BspNode>& nodes,
	int nodeIdx,
	int depth,
	const DungeonConfig& config,
	std::mt19937& rng,
	DungeonResult& result)
{
	auto& node = nodes[nodeIdx];

	if (depth >= config.maxDepth || node.w < config.minRoomSize * 2 + 1 || node.h < config.minRoomSize * 2 + 1)
	{
		// 葉ノード：部屋を配置
		const int pad = config.padding;
		const int maxW = std::min(node.w - pad * 2, config.maxRoomSize);
		const int maxH = std::min(node.h - pad * 2, config.maxRoomSize);
		if (maxW < config.minRoomSize || maxH < config.minRoomSize)
		{
			return;
		}

		std::uniform_int_distribution<int> distW(config.minRoomSize, maxW);
		std::uniform_int_distribution<int> distH(config.minRoomSize, maxH);
		const int rw = distW(rng);
		const int rh = distH(rng);

		std::uniform_int_distribution<int> distX(node.x + pad, node.x + node.w - rw - pad);
		std::uniform_int_distribution<int> distY(node.y + pad, node.y + node.h - rh - pad);
		const int rx = distX(rng);
		const int ry = distY(rng);

		Room room{rx, ry, rw, rh};
		node.roomIdx = static_cast<int>(result.rooms.size());
		result.rooms.push_back(room);

		// タイルに床を描画
		for (int dy = 0; dy < rh; ++dy)
		{
			for (int dx = 0; dx < rw; ++dx)
			{
				const int ty = ry + dy;
				const int tx = rx + dx;
				if (ty >= 0 && ty < config.mapHeight && tx >= 0 && tx < config.mapWidth)
				{
					result.tiles[static_cast<size_t>(ty)][static_cast<size_t>(tx)] = TileType::Floor;
				}
			}
		}
		return;
	}

	// 分割方向を決定
	const bool splitHorizontal = [&]()
	{
		if (node.w > node.h * 1.25)
		{
			return false; // 横長なら縦分割
		}
		if (node.h > node.w * 1.25)
		{
			return true; // 縦長なら横分割
		}
		return std::uniform_int_distribution<int>(0, 1)(rng) == 0;
	}();

	// ノード情報をコピー（push_back後に参照が無効化されるため）
	const int nx = node.x;
	const int ny = node.y;
	const int nw = node.w;
	const int nh = node.h;

	int leftIdx = -1;
	int rightIdx = -1;

	if (splitHorizontal)
	{
		const int minSplit = config.minRoomSize + config.padding;
		const int maxSplit = nh - config.minRoomSize - config.padding;
		if (minSplit >= maxSplit)
		{
			return;
		}
		const int split = std::uniform_int_distribution<int>(minSplit, maxSplit)(rng);

		BspNode left{nx, ny, nw, split};
		BspNode right{nx, ny + split, nw, nh - split};

		leftIdx = static_cast<int>(nodes.size());
		nodes.push_back(left);
		rightIdx = static_cast<int>(nodes.size());
		nodes.push_back(right);
	}
	else
	{
		const int minSplit = config.minRoomSize + config.padding;
		const int maxSplit = nw - config.minRoomSize - config.padding;
		if (minSplit >= maxSplit)
		{
			return;
		}
		const int split = std::uniform_int_distribution<int>(minSplit, maxSplit)(rng);

		BspNode left{nx, ny, split, nh};
		BspNode right{nx + split, ny, nw - split, nh};

		leftIdx = static_cast<int>(nodes.size());
		nodes.push_back(left);
		rightIdx = static_cast<int>(nodes.size());
		nodes.push_back(right);
	}

	// push_back後にインデックスで安全にアクセス
	nodes[static_cast<size_t>(nodeIdx)].leftChild = leftIdx;
	nodes[static_cast<size_t>(nodeIdx)].rightChild = rightIdx;
	splitBsp(nodes, leftIdx, depth + 1, config, rng, result);
	splitBsp(nodes, rightIdx, depth + 1, config, rng, result);
}

/// @brief 指定ノード配下の最初の部屋インデックスを取得する
[[nodiscard]] inline int findRoom(const std::vector<BspNode>& nodes, int nodeIdx) noexcept
{
	if (nodeIdx < 0 || nodeIdx >= static_cast<int>(nodes.size()))
	{
		return -1;
	}
	const auto& node = nodes[static_cast<size_t>(nodeIdx)];
	if (node.roomIdx >= 0)
	{
		return node.roomIdx;
	}
	const int left = findRoom(nodes, node.leftChild);
	if (left >= 0)
	{
		return left;
	}
	return findRoom(nodes, node.rightChild);
}

/// @brief 通路をタイルに描画する
inline void carveCorridor(
	int x1, int y1, int x2, int y2,
	const DungeonConfig& config,
	DungeonResult& result)
{
	// L字型の通路を描画
	const int midX = x1;
	const int midY = y2;

	// 水平セグメント
	const int startX = std::min(midX, x2);
	const int endX = std::max(midX, x2);
	for (int x = startX; x <= endX; ++x)
	{
		if (midY >= 0 && midY < config.mapHeight && x >= 0 && x < config.mapWidth)
		{
			auto& tile = result.tiles[static_cast<size_t>(midY)][static_cast<size_t>(x)];
			if (tile == TileType::Wall)
			{
				tile = TileType::Corridor;
			}
		}
	}

	// 垂直セグメント
	const int startY = std::min(y1, midY);
	const int endY = std::max(y1, midY);
	for (int y = startY; y <= endY; ++y)
	{
		if (y >= 0 && y < config.mapHeight && x1 >= 0 && x1 < config.mapWidth)
		{
			auto& tile = result.tiles[static_cast<size_t>(y)][static_cast<size_t>(x1)];
			if (tile == TileType::Wall)
			{
				tile = TileType::Corridor;
			}
		}
	}
}

/// @brief BSPノード間に通路を接続する
inline void connectNodes(
	const std::vector<BspNode>& nodes,
	int nodeIdx,
	const DungeonConfig& config,
	DungeonResult& result)
{
	if (nodeIdx < 0 || nodeIdx >= static_cast<int>(nodes.size()))
	{
		return;
	}
	const auto& node = nodes[static_cast<size_t>(nodeIdx)];
	if (node.isLeaf())
	{
		return;
	}

	connectNodes(nodes, node.leftChild, config, result);
	connectNodes(nodes, node.rightChild, config, result);

	// 左右の子ノードそれぞれから部屋を1つ見つけて接続
	const int leftRoom = findRoom(nodes, node.leftChild);
	const int rightRoom = findRoom(nodes, node.rightChild);

	if (leftRoom >= 0 && rightRoom >= 0)
	{
		const auto& lr = result.rooms[static_cast<size_t>(leftRoom)];
		const auto& rr = result.rooms[static_cast<size_t>(rightRoom)];
		const int x1 = lr.centerX();
		const int y1 = lr.centerY();
		const int x2 = rr.centerX();
		const int y2 = rr.centerY();

		result.corridors.push_back(Corridor{x1, y1, x2, y2});
		carveCorridor(x1, y1, x2, y2, config, result);
	}
}

} // namespace detail

/// @brief BSPアルゴリズムでダンジョンを生成する
/// @param config 生成設定
/// @param seed 乱数シード
/// @return 生成結果（部屋、通路、タイルグリッド）
[[nodiscard]] inline DungeonResult generateDungeon(const DungeonConfig& config, uint32_t seed = 0)
{
	DungeonResult result;
	result.width = config.mapWidth;
	result.height = config.mapHeight;
	result.tiles.assign(
		static_cast<size_t>(config.mapHeight),
		std::vector<TileType>(static_cast<size_t>(config.mapWidth), TileType::Wall));

	std::mt19937 rng(seed);

	// BSPツリーを構築
	std::vector<detail::BspNode> nodes;
	nodes.push_back(detail::BspNode{0, 0, config.mapWidth, config.mapHeight});
	detail::splitBsp(nodes, 0, 0, config, rng, result);

	// ノード間を通路で接続
	detail::connectNodes(nodes, 0, config, result);

	return result;
}

} // namespace sgc::procedural
