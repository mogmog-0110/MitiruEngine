#pragma once

/// @file AutoTile.hpp
/// @brief オートタイルシステム
///
/// 隣接タイルのビットマスクに基づいてタイルバリアントを自動選択する。
/// 8方向隣接（ビット0=北, 時計回り）のビットマスクからタイルIDを解決する。
///
/// @code
/// using namespace sgc::tilemap;
/// AutoTileSet tileSet;
/// tileSet.addRule(0b11111111, TileId{10}); // 全方向隣接
/// tileSet.addRule(0b00000000, TileId{1});  // 孤立タイル
/// auto resolved = resolveAutoTile(map, 0, 3, 4, TileId{5}, tileSet);
/// @endcode

#include "sgc/tilemap/Tilemap.hpp"

#include <array>
#include <unordered_map>

namespace sgc::tilemap
{

/// @brief 隣接方向のビットインデックス
/// ビット0=北、時計回りに1=北東, 2=東, ... 7=北西
enum class NeighborBit : uint8_t
{
	North     = 0, ///< 北（上）
	NorthEast = 1, ///< 北東
	East      = 2, ///< 東（右）
	SouthEast = 3, ///< 南東
	South     = 4, ///< 南（下）
	SouthWest = 5, ///< 南西
	West      = 6, ///< 西（左）
	NorthWest = 7, ///< 北西
};

/// @brief 8方向隣接ビットマスク型
using NeighborMask = uint8_t;

/// @brief オートタイルルール（ビットマスク→タイルIDの対応）
struct AutoTileRule
{
	NeighborMask mask{0};   ///< 隣接ビットマスク
	TileId tileId{0};       ///< 対応するタイルID
};

/// @brief オートタイルセット（ビットマスクからタイルIDへのマッピング）
///
/// 隣接タイルの配置パターンに基づき、適切なタイルバリアントを選択する。
/// 47パターン（blobタイル）または16パターン（4方向のみ）をサポート。
class AutoTileSet
{
public:
	/// @brief デフォルトタイルID（ルールにマッチしない場合に使用）
	/// @param id デフォルトタイルID
	void setDefaultTile(TileId id) noexcept { m_defaultTile = id; }

	/// @brief デフォルトタイルIDを取得
	/// @return デフォルトタイルID
	[[nodiscard]] TileId defaultTile() const noexcept { return m_defaultTile; }

	/// @brief ルールを追加する
	/// @param mask 隣接ビットマスク
	/// @param id 対応するタイルID
	void addRule(NeighborMask mask, TileId id)
	{
		m_rules[mask] = id;
	}

	/// @brief ビットマスクからタイルIDを解決する
	/// @param mask 隣接ビットマスク
	/// @return 対応するタイルID。ルールがなければデフォルト
	[[nodiscard]] TileId resolve(NeighborMask mask) const noexcept
	{
		const auto it = m_rules.find(mask);
		if (it != m_rules.end())
		{
			return it->second;
		}
		return m_defaultTile;
	}

	/// @brief 登録されたルール数を取得
	/// @return ルール数
	[[nodiscard]] size_t ruleCount() const noexcept { return m_rules.size(); }

	/// @brief 4方向のみのマスクに変換する（斜め方向を無視）
	/// @param fullMask 8ビットマスク
	/// @return 4ビットマスク（N,E,S,W）
	[[nodiscard]] static constexpr uint8_t toCardinalMask(NeighborMask fullMask) noexcept
	{
		uint8_t result = 0;
		if (fullMask & (1 << static_cast<uint8_t>(NeighborBit::North))) result |= 0b0001;
		if (fullMask & (1 << static_cast<uint8_t>(NeighborBit::East)))  result |= 0b0010;
		if (fullMask & (1 << static_cast<uint8_t>(NeighborBit::South))) result |= 0b0100;
		if (fullMask & (1 << static_cast<uint8_t>(NeighborBit::West)))  result |= 0b1000;
		return result;
	}

private:
	std::unordered_map<NeighborMask, TileId> m_rules;  ///< マスク→タイルIDマップ
	TileId m_defaultTile{EMPTY_TILE};                    ///< デフォルトタイルID
};

/// @brief 指定座標の隣接ビットマスクを計算する
/// @param layer 対象レイヤー
/// @param x X座標
/// @param y Y座標
/// @param targetId マッチ対象のタイルID
/// @return 8ビット隣接マスク
[[nodiscard]] inline NeighborMask computeNeighborMask(
	const TileLayer& layer, size_t x, size_t y, TileId targetId) noexcept
{
	/// 8方向のオフセット（北から時計回り）
	static constexpr std::array<std::pair<int, int>, 8> OFFSETS = {{
		{ 0, -1}, { 1, -1}, { 1,  0}, { 1,  1},
		{ 0,  1}, {-1,  1}, {-1,  0}, {-1, -1}
	}};

	NeighborMask mask = 0;
	for (uint8_t i = 0; i < 8; ++i)
	{
		const auto [dx, dy] = OFFSETS[i];
		const auto nx = static_cast<int>(x) + dx;
		const auto ny = static_cast<int>(y) + dy;
		if (nx < 0 || ny < 0)
		{
			continue;
		}
		const auto tile = layer.getTile(static_cast<size_t>(nx), static_cast<size_t>(ny));
		if (tile.has_value() && tile.value() == targetId)
		{
			mask |= static_cast<NeighborMask>(1 << i);
		}
	}
	return mask;
}

/// @brief 指定座標のオートタイルを解決する
/// @param tilemap タイルマップ
/// @param layerIndex 対象レイヤーインデックス
/// @param x X座標
/// @param y Y座標
/// @param targetId マッチ対象のタイルID
/// @param tileSet オートタイルセット
/// @return 解決されたタイルID。レイヤー範囲外ならEMPTY_TILE
[[nodiscard]] inline TileId resolveAutoTile(
	const Tilemap& tilemap, size_t layerIndex,
	size_t x, size_t y, TileId targetId,
	const AutoTileSet& tileSet) noexcept
{
	const auto* layer = tilemap.getLayer(layerIndex);
	if (!layer)
	{
		return EMPTY_TILE;
	}
	const auto currentTile = layer->getTile(x, y);
	if (!currentTile.has_value() || currentTile.value() != targetId)
	{
		return currentTile.value_or(EMPTY_TILE);
	}
	const auto mask = computeNeighborMask(*layer, x, y, targetId);
	return tileSet.resolve(mask);
}

/// @brief レイヤー全体にオートタイルを適用する
/// @param tilemap タイルマップ（変更される）
/// @param layerIndex 対象レイヤーインデックス
/// @param targetId マッチ対象のタイルID
/// @param tileSet オートタイルセット
/// @return 変更されたタイル数
inline size_t applyAutoTiling(
	Tilemap& tilemap, size_t layerIndex,
	TileId targetId, const AutoTileSet& tileSet) noexcept
{
	const auto* layer = tilemap.getLayer(layerIndex);
	if (!layer)
	{
		return 0;
	}

	const size_t w = layer->width();
	const size_t h = layer->height();

	// まず全マスクを計算（適用中にマスクが変わるのを防ぐ）
	std::vector<std::pair<size_t, TileId>> changes;
	for (size_t y = 0; y < h; ++y)
	{
		for (size_t x = 0; x < w; ++x)
		{
			const auto tile = layer->getTile(x, y);
			if (tile.has_value() && tile.value() == targetId)
			{
				const auto mask = computeNeighborMask(*layer, x, y, targetId);
				const auto resolved = tileSet.resolve(mask);
				changes.emplace_back(y * w + x, resolved);
			}
		}
	}

	// 変更を適用
	auto* mutableLayer = tilemap.getLayerMut(layerIndex);
	size_t count = 0;
	for (const auto& [index, newId] : changes)
	{
		const size_t cx = index % w;
		const size_t cy = index / w;
		mutableLayer->setTile(cx, cy, newId);
		++count;
	}
	return count;
}

} // namespace sgc::tilemap
