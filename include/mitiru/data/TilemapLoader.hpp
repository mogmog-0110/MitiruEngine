#pragma once

/// @file TilemapLoader.hpp
/// @brief タイルマップデータローダー（2Dゲーム向け）
///
/// JSONベースのタイルマップ定義の読み書きを行う。
/// レイヤー構造・タイル回転・反転に対応。
///
/// @code
/// mitiru::data::TilemapLoader loader;
/// auto tilemap = loader.createEmpty("level1", 20, 15, 32, 32);
/// loader.setTile(tilemap.layers[0], 5, 3, {1, 5, 3, 0.0f, false, false});
/// auto json = loader.saveToJson(tilemap);
/// @endcode

#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "mitiru/data/JsonBuilder.hpp"

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
///
/// JSON形式のタイルマップデータの読み書き・編集を行う。
class TilemapLoader
{
public:
	/// @brief JSONからタイルマップを読み込む
	/// @param json JSON文字列
	/// @return タイルマップ（パース失敗時nullopt）
	[[nodiscard]] std::optional<Tilemap> loadFromJson(const std::string& json) const
	{
		JsonReader reader;
		if (!reader.parse(json)) return std::nullopt;

		Tilemap tilemap;

		auto name = reader.getString("name");
		if (!name.has_value()) return std::nullopt;
		tilemap.name = *name;

		auto tw = reader.getInt("tileWidth");
		auto th = reader.getInt("tileHeight");
		if (tw) tilemap.tileWidth = *tw;
		if (th) tilemap.tileHeight = *th;

		auto layersArr = reader.getArray("layers");
		if (layersArr.has_value())
		{
			for (const auto& rawLayer : *layersArr)
			{
				JsonReader layerReader;
				if (!layerReader.parse(rawLayer)) continue;

				TilemapLayer layer;
				auto ln = layerReader.getString("name");
				if (ln) layer.name = *ln;
				auto lw = layerReader.getInt("width");
				auto lh = layerReader.getInt("height");
				if (lw) layer.width = *lw;
				if (lh) layer.height = *lh;

				auto tilesArr = layerReader.getArray("tiles");
				if (tilesArr.has_value())
				{
					for (const auto& rawTile : *tilesArr)
					{
						JsonReader tileReader;
						if (!tileReader.parse(rawTile)) continue;

						TileData tile;
						auto id = tileReader.getInt("tileId");
						if (id) tile.tileId = *id;
						auto tx = tileReader.getInt("x");
						if (tx) tile.x = *tx;
						auto ty = tileReader.getInt("y");
						if (ty) tile.y = *ty;
						auto rot = tileReader.getFloat("rotation");
						if (rot) tile.rotation = *rot;
						auto fx = tileReader.getBool("flipX");
						if (fx) tile.flipX = *fx;
						auto fy = tileReader.getBool("flipY");
						if (fy) tile.flipY = *fy;

						layer.tiles.push_back(tile);
					}
				}

				tilemap.layers.push_back(std::move(layer));
			}
		}

		return tilemap;
	}

	/// @brief タイルマップをJSON文字列に変換する
	/// @param tilemap タイルマップ
	/// @return JSON文字列
	[[nodiscard]] std::string saveToJson(const Tilemap& tilemap) const
	{
		JsonBuilder builder;
		builder.beginObject();
		builder.key("name").value(tilemap.name);
		builder.key("tileWidth").value(tilemap.tileWidth);
		builder.key("tileHeight").value(tilemap.tileHeight);
		builder.key("layers");
		builder.beginArray();

		for (const auto& layer : tilemap.layers)
		{
			builder.beginObject();
			builder.key("name").value(layer.name);
			builder.key("width").value(layer.width);
			builder.key("height").value(layer.height);
			builder.key("tiles");
			builder.beginArray();

			for (const auto& tile : layer.tiles)
			{
				builder.beginObject();
				builder.key("tileId").value(tile.tileId);
				builder.key("x").value(tile.x);
				builder.key("y").value(tile.y);
				builder.key("rotation").value(tile.rotation);
				builder.key("flipX").value(tile.flipX);
				builder.key("flipY").value(tile.flipY);
				builder.endObject();
			}

			builder.endArray();
			builder.endObject();
		}

		builder.endArray();
		builder.endObject();
		return builder.build();
	}

	/// @brief レイヤー内の指定座標のタイルを取得する
	/// @param layer タイルマップレイヤー
	/// @param x X座標（タイル単位）
	/// @param y Y座標（タイル単位）
	/// @return タイルデータ（存在しない場合nullopt）
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
	/// @param x X座標（タイル単位）
	/// @param y Y座標（タイル単位）
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

		/// デフォルトレイヤーを1つ追加
		TilemapLayer defaultLayer;
		defaultLayer.name = "default";
		defaultLayer.width = width;
		defaultLayer.height = height;
		tilemap.layers.push_back(std::move(defaultLayer));

		return tilemap;
	}
};

} // namespace mitiru::data
