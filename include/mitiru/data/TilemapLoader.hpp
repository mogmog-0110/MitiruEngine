#pragma once

/// @file TilemapLoader.hpp
/// @brief タイルマップデータローダー（2D ゲーム向け）
///
/// JSON ベースのタイルマップ定義の読み書きを行う。
/// レイヤー構造・タイル回転・反転に対応。
///
/// @code
/// mitiru::data::TilemapLoader loader;
/// auto tilemap = loader.createEmpty("level1", 20, 15, 32, 32);
/// loader.setTile(tilemap.layers[0], 5, 3, {1, 5, 3, 0.0f, false, false});
/// auto json = loader.saveToJson(tilemap);
/// @endcode

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "mitiru/data/JsonFields.hpp"

namespace mitiru::data
{

/// @brief タイルデータ
struct TileData
{
	int tileId{0};         ///< タイルID（0は空タイル）
	int x{0};              ///< X座標（タイル単位）
	int y{0};              ///< Y座標（タイル単位）
	float rotation{0.0f};  ///< 回転角度（度）
	bool flipX{false};     ///< X軸反転
	bool flipY{false};     ///< Y軸反転
};

/// @brief タイルマップレイヤー
struct TilemapLayer
{
	std::string name;              ///< レイヤー名
	int width{0};                  ///< 幅（タイル数）
	int height{0};                 ///< 高さ（タイル数）
	std::vector<TileData> tiles;   ///< タイルデータ配列
};

/// @brief タイルマップ
struct Tilemap
{
	std::string name;                    ///< タイルマップ名
	int tileWidth{32};                   ///< タイル幅（ピクセル）
	int tileHeight{32};                  ///< タイル高さ（ピクセル）
	std::vector<TilemapLayer> layers;    ///< レイヤーリスト
};

/// @brief タイルマップローダー
class TilemapLoader
{
public:
	/// @brief JSON からタイルマップを読み込む
	/// @param json JSON 文字列
	/// @return タイルマップ（読めない・"name" が無いときは nullopt）
	[[nodiscard]] std::optional<Tilemap> loadFromJson(const std::string& json) const
	{
		const auto doc = nlohmann::json::parse(json, nullptr, false);
		const auto name = doc.is_object() ? doc.find("name") : doc.end();
		if (!doc.is_object() || name == doc.end() || !name->is_string()) return std::nullopt;

		Tilemap tilemap;
		tilemap.name = name->get<std::string>();
		tilemap.tileWidth = fieldOr(doc, "tileWidth", tilemap.tileWidth);
		tilemap.tileHeight = fieldOr(doc, "tileHeight", tilemap.tileHeight);
		if (const auto layers = doc.find("layers"); layers != doc.end() && layers->is_array())
		{
			for (const auto& layer : *layers)
			{
				if (layer.is_object()) { tilemap.layers.push_back(layerFromJson(layer)); }
			}
		}
		return tilemap;
	}

	/// @brief タイルマップを JSON 文字列に変換する
	/// @param tilemap タイルマップ
	/// @return JSON 文字列
	[[nodiscard]] std::string saveToJson(const Tilemap& tilemap) const
	{
		nlohmann::ordered_json layers = nlohmann::ordered_json::array();
		for (const auto& layer : tilemap.layers)
		{
			nlohmann::ordered_json tiles = nlohmann::ordered_json::array();
			for (const auto& tile : layer.tiles)
			{
				tiles.push_back({
					{"tileId", tile.tileId},
					{"x", tile.x},
					{"y", tile.y},
					{"rotation", jsonFloat(tile.rotation)},
					{"flipX", tile.flipX},
					{"flipY", tile.flipY},
				});
			}
			layers.push_back({
				{"name", layer.name},
				{"width", layer.width},
				{"height", layer.height},
				{"tiles", std::move(tiles)},
			});
		}
		return nlohmann::ordered_json{
			{"name", tilemap.name},
			{"tileWidth", tilemap.tileWidth},
			{"tileHeight", tilemap.tileHeight},
			{"layers", std::move(layers)},
		}.dump();
	}

	/// @brief レイヤー内の指定座標のタイルを取得する
	/// @param layer タイルマップレイヤー
	/// @param x X 座標（タイル単位）
	/// @param y Y 座標（タイル単位）
	/// @return タイルデータ（存在しない場合は nullopt）
	[[nodiscard]] std::optional<TileData> getTile(
		const TilemapLayer& layer, int x, int y) const
	{
		for (const auto& tile : layer.tiles)
		{
			if (tile.x == x && tile.y == y)
			{
				return tile;
			}
		}
		return std::nullopt;
	}

	/// @brief レイヤー内の指定座標にタイルを設定する
	/// @param layer タイルマップレイヤー
	/// @param x X 座標（タイル単位）
	/// @param y Y 座標（タイル単位）
	/// @param tileData タイルデータ
	void setTile(TilemapLayer& layer, int x, int y, const TileData& tileData)
	{
		for (auto& tile : layer.tiles)
		{
			if (tile.x == x && tile.y == y)
			{
				tile = tileData;
				return;
			}
		}
		/// 既存タイルが見つからない場合は新規追加
		TileData newTile = tileData;
		newTile.x = x;
		newTile.y = y;
		layer.tiles.push_back(newTile);
	}

	/// @brief 空のタイルマップを作成する
	/// @param name タイルマップ名
	/// @param width 幅（タイル数）
	/// @param height 高さ（タイル数）
	/// @param tileW タイル幅（ピクセル）
	/// @param tileH タイル高さ（ピクセル）
	/// @return タイルマップ
	[[nodiscard]] Tilemap createEmpty(
		const std::string& name,
		int width, int height,
		int tileW, int tileH) const
	{
		Tilemap tilemap;
		tilemap.name = name;
		tilemap.tileWidth = tileW;
		tilemap.tileHeight = tileH;

		/// デフォルトレイヤーを 1 つ追加
		TilemapLayer defaultLayer;
		defaultLayer.name = "default";
		defaultLayer.width = width;
		defaultLayer.height = height;
		tilemap.layers.push_back(std::move(defaultLayer));

		return tilemap;
	}

private:
	[[nodiscard]] static TilemapLayer layerFromJson(const nlohmann::json& obj)
	{
		TilemapLayer layer;
		layer.name = fieldOr(obj, "name", layer.name);
		layer.width = fieldOr(obj, "width", layer.width);
		layer.height = fieldOr(obj, "height", layer.height);
		if (const auto tiles = obj.find("tiles"); tiles != obj.end() && tiles->is_array())
		{
			for (const auto& t : *tiles)
			{
				if (!t.is_object()) continue;
				TileData tile;
				tile.tileId = fieldOr(t, "tileId", tile.tileId);
				tile.x = fieldOr(t, "x", tile.x);
				tile.y = fieldOr(t, "y", tile.y);
				tile.rotation = fieldOr(t, "rotation", tile.rotation);
				tile.flipX = fieldOr(t, "flipX", tile.flipX);
				tile.flipY = fieldOr(t, "flipY", tile.flipY);
				layer.tiles.push_back(tile);
			}
		}
		return layer;
	}
};

} // namespace mitiru::data
