#pragma once

/// @file TilemapBridge.hpp
/// @brief sgc タイルマップ統合ブリッジ
/// @details sgcのTilemapシステムをMitiruエンジンに統合する。
///          名前付きタイルマップの作成・操作を管理する。

#include <cstddef>
#include <string>
#include <unordered_map>

#include <sgc/tilemap/Tilemap.hpp>

namespace mitiru::bridge
{

/// @brief sgc タイルマップ統合ブリッジ
/// @details 名前付きタイルマップの作成・タイル設定・取得を一元管理する。
///
/// @code
/// mitiru::bridge::TilemapBridge tilemap;
/// tilemap.create("world", 32, 32, 16);
/// tilemap.setTile("world", 5, 3, 1);
/// int id = tilemap.getTile("world", 5, 3);  // 1
/// @endcode
class TilemapBridge
{
public:
	/// @brief タイルマップを作成する
	///
	/// 指定サイズの単一レイヤーを持つタイルマップを作成する。
	///
	/// @param name タイルマップ名
	/// @param width 横方向のタイル数
	/// @param height 縦方向のタイル数
	/// @param tileSize タイルサイズ（ピクセル）※メタデータとして保持
	void create(const std::string& name, int width, int height, int tileSize)
	{
		TilemapEntry entry;
		entry.tilemap.addLayer(static_cast<size_t>(width), static_cast<size_t>(height));
		entry.tileSize = tileSize;
		entry.width = width;
		entry.height = height;
		m_tilemaps[name] = std::move(entry);
	}

	/// @brief タイルを設定する
	/// @param name タイルマップ名
	/// @param x X座標
	/// @param y Y座標
	/// @param tileId 設定するタイルID
	void setTile(const std::string& name, int x, int y, int tileId)
	{
		const auto it = m_tilemaps.find(name);
		if (it != m_tilemaps.end())
		{
			it->second.tilemap.setTile(0, static_cast<size_t>(x), static_cast<size_t>(y),
				static_cast<sgc::tilemap::TileId>(tileId));
		}
	}

	/// @brief タイルを取得する
	/// @param name タイルマップ名
	/// @param x X座標
	/// @param y Y座標
	/// @return タイルID（未登録または範囲外なら0）
	[[nodiscard]] int getTile(const std::string& name, int x, int y) const
	{
		const auto it = m_tilemaps.find(name);
		if (it == m_tilemaps.end())
		{
			return 0;
		}
		const auto result = it->second.tilemap.getTile(
			0, static_cast<size_t>(x), static_cast<size_t>(y));
		return result.has_value() ? static_cast<int>(result.value()) : 0;
	}

	/// @brief タイルマップを削除する
	/// @param name タイルマップ名
	void removeTilemap(const std::string& name)
	{
		m_tilemaps.erase(name);
	}

	/// @brief タイルマップ数を取得する
	/// @return タイルマップ数
	[[nodiscard]] std::size_t tilemapCount() const noexcept
	{
		return m_tilemaps.size();
	}

	// ── シリアライズ ──────────────────────────────────────────

	/// @brief タイルマップ状態をJSON文字列として返す
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"tilemapCount\":" + std::to_string(m_tilemaps.size()) + ",";
		json += "\"tilemaps\":[";

		bool first = true;
		for (const auto& [name, entry] : m_tilemaps)
		{
			if (!first) json += ",";
			json += "{\"name\":\"" + name + "\"";
			json += ",\"width\":" + std::to_string(entry.width);
			json += ",\"height\":" + std::to_string(entry.height);
			json += ",\"tileSize\":" + std::to_string(entry.tileSize);
			json += "}";
			first = false;
		}

		json += "]";
		json += "}";
		return json;
	}

private:
	/// @brief タイルマップ内部エントリ
	struct TilemapEntry
	{
		sgc::tilemap::Tilemap tilemap;  ///< タイルマップ本体
		int tileSize = 16;              ///< タイルサイズ（ピクセル）
		int width = 0;                  ///< マップ幅
		int height = 0;                 ///< マップ高さ
	};

	std::unordered_map<std::string, TilemapEntry> m_tilemaps;  ///< 名前 → タイルマップ
};

} // namespace mitiru::bridge
