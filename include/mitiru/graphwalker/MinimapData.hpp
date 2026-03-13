#pragma once

/// @file MinimapData.hpp
/// @brief ミニマップのデータモデル
///
/// 訪問済みチャンクを管理し、探索率を計算する。
/// 描画ロジックは持たず、データのみを保持する。
///
/// @code
/// mitiru::gw::MinimapData minimap;
/// minimap.visitChunk({150.0f, 320.0f});
/// float pct = minimap.getExplorationPercent(100);
/// @endcode

#include <cmath>
#include <set>
#include <utility>

#include "sgc/math/Vec2.hpp"

namespace mitiru::gw
{

/// @brief チャンク座標ID
///
/// ワールドをグリッドに分割した際の整数座標。
/// set/mapのキーとして使用する。
struct ChunkId
{
	int x{}; ///< チャンクX座標
	int y{}; ///< チャンクY座標

	/// @brief 比較演算子（setで使用）
	[[nodiscard]] constexpr auto operator<=>(const ChunkId&) const noexcept = default;
};

/// @brief ChunkIdのハッシュ関数オブジェクト
///
/// unordered_set/unordered_mapで使用する場合のハッシュ。
struct ChunkIdHash
{
	/// @brief ハッシュ値を計算する
	/// @param id チャンクID
	/// @return ハッシュ値
	[[nodiscard]] std::size_t operator()(const ChunkId& id) const noexcept
	{
		const auto h1 = std::hash<int>{}(id.x);
		const auto h2 = std::hash<int>{}(id.y);
		return h1 ^ (h2 << 16) ^ (h2 >> 16);
	}
};

/// @brief ミニマップデータモデル
///
/// ワールドをチャンク単位で分割し、プレイヤーが
/// 訪問したチャンクを記録する。探索率の計算も行う。
class MinimapData
{
public:
	/// @brief デフォルトコンストラクタ
	MinimapData() = default;

	/// @brief ワールド座標からチャンクを訪問済みにする
	/// @param worldPos ワールド座標
	/// @param chunkSize チャンク1辺の大きさ（デフォルト200）
	void visitChunk(sgc::Vec2f worldPos, float chunkSize = 200.0f)
	{
		const ChunkId id{
			static_cast<int>(std::floor(worldPos.x / chunkSize)),
			static_cast<int>(std::floor(worldPos.y / chunkSize))
		};
		m_visited.insert(id);
	}

	/// @brief チャンクが訪問済みか判定する
	/// @param id チャンクID
	/// @return 訪問済みならtrue
	[[nodiscard]] bool isVisited(ChunkId id) const
	{
		return m_visited.count(id) > 0;
	}

	/// @brief 訪問済みチャンクの集合を取得する
	/// @return 訪問済みチャンクの集合への参照
	[[nodiscard]] const std::set<ChunkId>& getVisitedChunks() const noexcept
	{
		return m_visited;
	}

	/// @brief 探索率を計算する
	/// @param totalChunks 全チャンク数
	/// @return 探索率（0.0〜1.0）。totalChunksが0以下なら0
	[[nodiscard]] float getExplorationPercent(int totalChunks) const noexcept
	{
		if (totalChunks <= 0)
		{
			return 0.0f;
		}
		return static_cast<float>(m_visited.size())
			/ static_cast<float>(totalChunks);
	}

	/// @brief 訪問済み領域のバウンディングボックスを取得する
	/// @return {最小座標, 最大座標}のペア。空なら{0,0}
	[[nodiscard]] std::pair<sgc::Vec2f, sgc::Vec2f> getBounds() const noexcept
	{
		if (m_visited.empty())
		{
			return {{0.0f, 0.0f}, {0.0f, 0.0f}};
		}

		auto it = m_visited.begin();
		int minX = it->x, maxX = it->x;
		int minY = it->y, maxY = it->y;

		for (const auto& chunk : m_visited)
		{
			if (chunk.x < minX) minX = chunk.x;
			if (chunk.x > maxX) maxX = chunk.x;
			if (chunk.y < minY) minY = chunk.y;
			if (chunk.y > maxY) maxY = chunk.y;
		}

		return {
			{static_cast<float>(minX), static_cast<float>(minY)},
			{static_cast<float>(maxX + 1), static_cast<float>(maxY + 1)}
		};
	}

	/// @brief 全訪問データをクリアする
	void clear()
	{
		m_visited.clear();
	}

private:
	std::set<ChunkId> m_visited; ///< 訪問済みチャンクの集合
};

} // namespace mitiru::gw
