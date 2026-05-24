#pragma once

/// @file DungeonLayout.hpp
/// @brief ダンジョンレイアウト管理
///
/// ローグライクゲームのダンジョン構造を表現する。
/// 部屋・通路・接続関係を管理し、BSPベースの自動生成を提供する。
///
/// @code
/// using namespace sgc::roguelike;
/// auto layout = generateLayout(80, 50, 8, 42);
/// for (const auto& room : layout.rooms())
/// {
///     // room.bounds で矩形取得
/// }
/// @endcode

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

#include "sgc/math/Rect.hpp"
#include "sgc/math/Vec2.hpp"

namespace sgc::roguelike
{

/// @brief ダンジョンの部屋
struct Room
{
	int id = 0;                                ///< 部屋ID
	sgc::Rect<int> bounds{};                   ///< 部屋の矩形範囲
	std::vector<int> connections;              ///< 接続先の部屋ID一覧

	/// @brief 部屋の中心座標を取得する
	/// @return 中心座標
	[[nodiscard]] sgc::Vec2<int> center() const noexcept
	{
		return {
			bounds.position.x + bounds.size.x / 2,
			bounds.position.y + bounds.size.y / 2
		};
	}
};

/// @brief ダンジョンの通路
struct Corridor
{
	int fromRoomId = 0;                       ///< 接続元の部屋ID
	int toRoomId = 0;                         ///< 接続先の部屋ID
	std::vector<sgc::Vec2<int>> path;         ///< 通路のパス（タイル座標列）
};

/// @brief ダンジョンレイアウト
///
/// 部屋と通路の構造を保持する。
/// 部屋間の接続関係の問い合わせを提供する。
class DungeonLayout
{
public:
	/// @brief 部屋を追加する
	/// @param room 追加する部屋
	void addRoom(Room room)
	{
		m_rooms.push_back(std::move(room));
	}

	/// @brief 通路を追加する
	/// @param corridor 追加する通路
	void addCorridor(Corridor corridor)
	{
		m_corridors.push_back(std::move(corridor));
	}

	/// @brief 全部屋を取得する
	/// @return 部屋一覧への参照
	[[nodiscard]] const std::vector<Room>& rooms() const noexcept
	{
		return m_rooms;
	}

	/// @brief 全通路を取得する
	/// @return 通路一覧への参照
	[[nodiscard]] const std::vector<Corridor>& corridors() const noexcept
	{
		return m_corridors;
	}

	/// @brief IDで部屋を取得する
	/// @param roomId 部屋ID
	/// @return 部屋へのポインタ。存在しない場合はnullptr
	[[nodiscard]] const Room* getRoom(int roomId) const noexcept
	{
		for (const auto& room : m_rooms)
		{
			if (room.id == roomId)
			{
				return &room;
			}
		}
		return nullptr;
	}

	/// @brief 2つの部屋が接続されているか判定する
	/// @param roomA 部屋AのID
	/// @param roomB 部屋BのID
	/// @return 接続されていればtrue
	[[nodiscard]] bool isConnected(int roomA, int roomB) const
	{
		for (const auto& corridor : m_corridors)
		{
			if ((corridor.fromRoomId == roomA && corridor.toRoomId == roomB) ||
				(corridor.fromRoomId == roomB && corridor.toRoomId == roomA))
			{
				return true;
			}
		}
		return false;
	}

	/// @brief 部屋数を取得する
	/// @return 部屋の数
	[[nodiscard]] std::size_t roomCount() const noexcept
	{
		return m_rooms.size();
	}

	/// @brief 通路数を取得する
	/// @return 通路の数
	[[nodiscard]] std::size_t corridorCount() const noexcept
	{
		return m_corridors.size();
	}

private:
	std::vector<Room> m_rooms;          ///< 部屋一覧
	std::vector<Corridor> m_corridors;  ///< 通路一覧
};

namespace detail
{

/// @brief BSPノード（内部用）
struct LayoutBspNode
{
	int x, y, w, h;
	int left = -1;
	int right = -1;
	int roomId = -1;

	[[nodiscard]] constexpr bool isLeaf() const noexcept
	{
		return left == -1 && right == -1;
	}
};

/// @brief L字型の通路パスを生成する
/// @param from 始点座標
/// @param to 終点座標
/// @return タイル座標のパス
[[nodiscard]] inline std::vector<sgc::Vec2<int>> buildCorridorPath(
	const sgc::Vec2<int>& from, const sgc::Vec2<int>& to)
{
	std::vector<sgc::Vec2<int>> path;

	// 水平セグメント
	const int startX = std::min(from.x, to.x);
	const int endX = std::max(from.x, to.x);
	for (int x = startX; x <= endX; ++x)
	{
		path.push_back({x, from.y});
	}

	// 垂直セグメント
	const int startY = std::min(from.y, to.y);
	const int endY = std::max(from.y, to.y);
	for (int y = startY; y <= endY; ++y)
	{
		path.push_back({to.x, y});
	}

	return path;
}

} // namespace detail

/// @brief BSPアルゴリズムでダンジョンレイアウトを生成する
///
/// Binary Space Partitioningで空間を分割し、
/// 各リーフに部屋を配置して通路で接続する。
///
/// @param width マップ幅（タイル数）
/// @param height マップ高さ（タイル数）
/// @param roomCount 目標部屋数（BSP深度で近似）
/// @param seed 乱数シード
/// @return 生成されたダンジョンレイアウト
[[nodiscard]] inline DungeonLayout generateLayout(
	int width, int height, int roomCount, uint32_t seed)
{
	DungeonLayout layout;
	std::mt19937 rng(seed);

	// BSP深度をroomCountから推定（2^depth ≈ roomCount）
	int maxDepth = 0;
	{
		int count = 1;
		while (count < roomCount)
		{
			count *= 2;
			++maxDepth;
		}
	}
	maxDepth = std::max(maxDepth, 2);

	const int minRoomSize = 4;
	const int padding = 1;

	// BSPツリー構築
	std::vector<detail::LayoutBspNode> nodes;
	nodes.push_back({0, 0, width, height});

	// 分割キュー
	struct SplitTask { int nodeIdx; int depth; };
	std::vector<SplitTask> tasks;
	tasks.push_back({0, 0});

	int nextRoomId = 0;

	while (!tasks.empty())
	{
		const auto task = tasks.back();
		tasks.pop_back();

		auto& node = nodes[static_cast<std::size_t>(task.nodeIdx)];

		if (task.depth >= maxDepth ||
			node.w < minRoomSize * 2 + 1 ||
			node.h < minRoomSize * 2 + 1)
		{
			// リーフ: 部屋を配置
			const int maxW = std::min(node.w - padding * 2, node.w * 3 / 4);
			const int maxH = std::min(node.h - padding * 2, node.h * 3 / 4);

			if (maxW < minRoomSize || maxH < minRoomSize)
			{
				continue;
			}

			std::uniform_int_distribution<int> distW(minRoomSize, maxW);
			std::uniform_int_distribution<int> distH(minRoomSize, maxH);
			const int rw = distW(rng);
			const int rh = distH(rng);

			std::uniform_int_distribution<int> distX(node.x + padding, node.x + node.w - rw - padding);
			std::uniform_int_distribution<int> distY(node.y + padding, node.y + node.h - rh - padding);
			const int rx = distX(rng);
			const int ry = distY(rng);

			Room room;
			room.id = nextRoomId++;
			room.bounds = {rx, ry, rw, rh};
			node.roomId = room.id;
			layout.addRoom(std::move(room));
			continue;
		}

		// 分割
		const bool splitH = (node.h > node.w) ||
			(node.h == node.w && std::uniform_int_distribution<int>(0, 1)(rng) == 0);

		const int nx = node.x;
		const int ny = node.y;
		const int nw = node.w;
		const int nh = node.h;

		if (splitH)
		{
			const int minS = minRoomSize + padding;
			const int maxS = nh - minRoomSize - padding;
			if (minS >= maxS) continue;
			const int split = std::uniform_int_distribution<int>(minS, maxS)(rng);

			const int leftIdx = static_cast<int>(nodes.size());
			nodes.push_back({nx, ny, nw, split});
			const int rightIdx = static_cast<int>(nodes.size());
			nodes.push_back({nx, ny + split, nw, nh - split});

			nodes[static_cast<std::size_t>(task.nodeIdx)].left = leftIdx;
			nodes[static_cast<std::size_t>(task.nodeIdx)].right = rightIdx;
			tasks.push_back({leftIdx, task.depth + 1});
			tasks.push_back({rightIdx, task.depth + 1});
		}
		else
		{
			const int minS = minRoomSize + padding;
			const int maxS = nw - minRoomSize - padding;
			if (minS >= maxS) continue;
			const int split = std::uniform_int_distribution<int>(minS, maxS)(rng);

			const int leftIdx = static_cast<int>(nodes.size());
			nodes.push_back({nx, ny, split, nh});
			const int rightIdx = static_cast<int>(nodes.size());
			nodes.push_back({nx + split, ny, nw - split, nh});

			nodes[static_cast<std::size_t>(task.nodeIdx)].left = leftIdx;
			nodes[static_cast<std::size_t>(task.nodeIdx)].right = rightIdx;
			tasks.push_back({leftIdx, task.depth + 1});
			tasks.push_back({rightIdx, task.depth + 1});
		}
	}

	// 通路接続: 隣接BSPノードの部屋同士を接続
	// ノードからルーム検索用のヘルパー
	std::function<int(int)> findRoomInNode = [&](int idx) -> int
	{
		if (idx < 0 || idx >= static_cast<int>(nodes.size())) return -1;
		const auto& n = nodes[static_cast<std::size_t>(idx)];
		if (n.roomId >= 0) return n.roomId;
		const int l = findRoomInNode(n.left);
		if (l >= 0) return l;
		return findRoomInNode(n.right);
	};

	// 全内部ノードの左右子を接続
	for (const auto& node : nodes)
	{
		if (node.isLeaf()) continue;

		const int leftRoom = findRoomInNode(node.left);
		const int rightRoom = findRoomInNode(node.right);
		if (leftRoom < 0 || rightRoom < 0) continue;

		const auto* roomA = layout.getRoom(leftRoom);
		const auto* roomB = layout.getRoom(rightRoom);
		if (!roomA || !roomB) continue;

		Corridor corridor;
		corridor.fromRoomId = leftRoom;
		corridor.toRoomId = rightRoom;
		corridor.path = detail::buildCorridorPath(roomA->center(), roomB->center());
		layout.addCorridor(std::move(corridor));

		// 接続情報を部屋にも記録（mutableアクセスが必要なので直接操作）
	}

	// 部屋の接続情報を通路から構築
	auto& rooms = const_cast<std::vector<Room>&>(layout.rooms());
	for (const auto& c : layout.corridors())
	{
		for (auto& room : rooms)
		{
			if (room.id == c.fromRoomId)
			{
				room.connections.push_back(c.toRoomId);
			}
			if (room.id == c.toRoomId)
			{
				room.connections.push_back(c.fromRoomId);
			}
		}
	}

	return layout;
}

} // namespace sgc::roguelike
