#pragma once

/// @file TiledMapLoader.hpp
/// @brief Tiled (.tmj) JSON形式タイルマップローダー
/// @details nlohmann/json を使用してTiledエディタの.tmj形式を
///          TilemapRenderer用のTilemap構造体に変換する。
///          タイルレイヤー・オブジェクトレイヤー（衝突矩形）・
///          複数タイルセットをサポートする。

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sgc/math/Rect.hpp>

#include <mitiru/render/TilemapRenderer.hpp>

/// @note nlohmann/json が利用可能な場合のみ有効
#if __has_include(<nlohmann/json.hpp>)
#include <nlohmann/json.hpp>
#define MITIRU_HAS_NLOHMANN_JSON 1
#else
#define MITIRU_HAS_NLOHMANN_JSON 0
#endif

namespace mitiru::render
{

// ─── Tiled Object (衝突矩形等) ─────────────────────────────

/// @brief Tiledオブジェクトレイヤー内のオブジェクト
/// @details 衝突矩形・トリガー領域・スポーン地点などを表現する。
struct TiledObject
{
	int id = 0;              ///< オブジェクトID
	std::string name;        ///< オブジェクト名
	std::string type;        ///< オブジェクト型（クラス）
	float x = 0.0f;          ///< X座標（ピクセル）
	float y = 0.0f;          ///< Y座標（ピクセル）
	float width = 0.0f;      ///< 幅（ピクセル）
	float height = 0.0f;     ///< 高さ（ピクセル）
	bool visible = true;     ///< 表示フラグ

	/// @brief 矩形として取得する
	[[nodiscard]] sgc::Rectf toRect() const noexcept
	{
		return {x, y, width, height};
	}
};

/// @brief Tiledオブジェクトレイヤー
struct TiledObjectLayer
{
	std::string name;                    ///< レイヤー名
	std::vector<TiledObject> objects;    ///< オブジェクトリスト
	bool visible = true;                 ///< 表示フラグ
};

// ─── TiledMapResult ─────────────────────────────────────────

/// @brief Tiledマップ読み込み結果
/// @details タイルマップ本体に加え、オブジェクトレイヤーを保持する。
struct TiledMapResult
{
	Tilemap tilemap;                              ///< タイルマップ本体
	std::vector<TiledObjectLayer> objectLayers;   ///< オブジェクトレイヤー

	/// @brief 名前でオブジェクトレイヤーを検索する
	/// @param name レイヤー名
	/// @return レイヤーへのポインタ（見つからなければnullptr）
	[[nodiscard]] const TiledObjectLayer* findObjectLayer(
		const std::string& name) const
	{
		for (const auto& layer : objectLayers)
		{
			if (layer.name == name)
			{
				return &layer;
			}
		}
		return nullptr;
	}

	/// @brief 全オブジェクトレイヤーから衝突矩形を収集する
	/// @return 衝突矩形リスト
	[[nodiscard]] std::vector<sgc::Rectf> collectCollisionRects() const
	{
		std::vector<sgc::Rectf> rects;
		for (const auto& layer : objectLayers)
		{
			for (const auto& obj : layer.objects)
			{
				rects.push_back(obj.toRect());
			}
		}
		return rects;
	}

	/// @brief 指定型のオブジェクトを全レイヤーから収集する
	/// @param typeName オブジェクト型名
	/// @return マッチするオブジェクトリスト
	[[nodiscard]] std::vector<TiledObject> findObjectsByType(
		const std::string& typeName) const
	{
		std::vector<TiledObject> result;
		for (const auto& layer : objectLayers)
		{
			for (const auto& obj : layer.objects)
			{
				if (obj.type == typeName)
				{
					result.push_back(obj);
				}
			}
		}
		return result;
	}
};

// ─── Tiled Flip Constants ───────────────────────────────────

/// @brief Tiled GIDフラグビット（上位3ビット）
namespace tiled_flags
{
	constexpr std::uint32_t FLIPPED_HORIZONTALLY = 0x80000000u;
	constexpr std::uint32_t FLIPPED_VERTICALLY   = 0x40000000u;
	constexpr std::uint32_t FLIPPED_DIAGONALLY   = 0x20000000u;
	constexpr std::uint32_t ALL_FLAGS            = FLIPPED_HORIZONTALLY
	                                              | FLIPPED_VERTICALLY
	                                              | FLIPPED_DIAGONALLY;
} // namespace tiled_flags

// ─── TiledMapLoader ─────────────────────────────────────────

/// @brief Tiled (.tmj) JSONマップローダー
/// @details Tiled Map Editorが出力するJSON形式のタイルマップを読み込み、
///          TilemapRenderer互換のTilemap構造体に変換する。
///
/// @code
/// auto result = mitiru::render::TiledMapLoader::loadTiledMap(jsonString);
/// if (result) {
///     renderer.drawTilemap(screen, result->tilemap, camX, camY);
/// }
/// @endcode
class TiledMapLoader
{
public:
#if MITIRU_HAS_NLOHMANN_JSON

	/// @brief Tiled JSON文字列からTilemapを読み込む
	/// @param jsonString .tmj形式のJSON文字列
	/// @return 読み込み結果（パース失敗時nullopt）
	[[nodiscard]] static std::optional<TiledMapResult> loadTiledMap(
		const std::string& jsonString)
	{
		try
		{
			const auto doc = nlohmann::json::parse(jsonString);
			return parseTiledDocument(doc);
		}
		catch (const nlohmann::json::exception&)
		{
			return std::nullopt;
		}
	}

private:
	/// @brief Tiledドキュメント全体をパースする
	[[nodiscard]] static std::optional<TiledMapResult> parseTiledDocument(
		const nlohmann::json& doc)
	{
		TiledMapResult result;
		auto& tilemap = result.tilemap;

		/// マップ基本情報を取得する
		tilemap.mapWidth = doc.value("width", 0);
		tilemap.mapHeight = doc.value("height", 0);
		const int tileW = doc.value("tilewidth", 16);
		const int tileH = doc.value("tileheight", 16);

		if (tilemap.mapWidth <= 0 || tilemap.mapHeight <= 0)
		{
			return std::nullopt;
		}

		/// タイルセットをパースする
		if (doc.contains("tilesets") && doc["tilesets"].is_array())
		{
			for (const auto& tsJson : doc["tilesets"])
			{
				tilemap.tilesets.push_back(parseTileset(tsJson, tileW, tileH));
			}
		}

		/// レイヤーをパースする
		if (doc.contains("layers") && doc["layers"].is_array())
		{
			for (const auto& layerJson : doc["layers"])
			{
				const auto layerType = layerJson.value("type", std::string{});

				if (layerType == "tilelayer")
				{
					auto layer = parseTileLayer(layerJson);
					if (layer.has_value())
					{
						tilemap.layers.push_back(std::move(*layer));
					}
				}
				else if (layerType == "objectgroup")
				{
					auto objLayer = parseObjectLayer(layerJson);
					if (objLayer.has_value())
					{
						result.objectLayers.push_back(std::move(*objLayer));
					}
				}
				/// group レイヤー（入れ子）は再帰パース
				else if (layerType == "group")
				{
					parseGroupLayer(layerJson, tilemap, result);
				}
			}
		}

		return result;
	}

	/// @brief タイルセットをパースする
	[[nodiscard]] static TilesetConfig parseTileset(
		const nlohmann::json& tsJson, int defaultTileW, int defaultTileH)
	{
		TilesetConfig ts;
		ts.firstGid = tsJson.value("firstgid", 1);
		ts.tileWidth = tsJson.value("tilewidth", defaultTileW);
		ts.tileHeight = tsJson.value("tileheight", defaultTileH);
		ts.columns = tsJson.value("columns", 1);
		ts.spacing = tsJson.value("spacing", 0);
		ts.margin = tsJson.value("margin", 0);

		/// テクスチャは外部で設定する（パス情報のみ保持）
		/// image フィールドはTileset内に含まれるがロードは呼び出し側の責務

		return ts;
	}

	/// @brief タイルレイヤーをパースする
	[[nodiscard]] static std::optional<TilemapLayer> parseTileLayer(
		const nlohmann::json& layerJson)
	{
		TilemapLayer layer;
		layer.name = layerJson.value("name", std::string{"unnamed"});
		layer.width = layerJson.value("width", 0);
		layer.height = layerJson.value("height", 0);
		layer.visible = layerJson.value("visible", true);
		layer.opacity = layerJson.value("opacity", 1.0f);
		layer.offsetX = layerJson.value("offsetx", 0.0f);
		layer.offsetY = layerJson.value("offsety", 0.0f);
		layer.parallaxX = layerJson.value("parallaxx", 1.0f);
		layer.parallaxY = layerJson.value("parallaxy", 1.0f);

		if (layer.width <= 0 || layer.height <= 0)
		{
			return std::nullopt;
		}

		/// タイルデータをパースする（GIDフラグ付き）
		if (layerJson.contains("data") && layerJson["data"].is_array())
		{
			const auto& dataArr = layerJson["data"];
			layer.data.reserve(dataArr.size());

			for (const auto& val : dataArr)
			{
				const auto rawGid = val.get<std::uint32_t>();

				/// 上位ビットから反転フラグを抽出する
				const bool flipH = (rawGid & tiled_flags::FLIPPED_HORIZONTALLY) != 0;
				const bool flipV = (rawGid & tiled_flags::FLIPPED_VERTICALLY) != 0;
				const bool flipD = (rawGid & tiled_flags::FLIPPED_DIAGONALLY) != 0;

				/// フラグを除去した純粋なタイルIDを取得する
				const int tileId = static_cast<int>(rawGid & ~tiled_flags::ALL_FLAGS);
				layer.data.push_back(tileId);

				/// 反転フラグを設定する
				if (flipH || flipV || flipD)
				{
					const auto idx = layer.data.size() - 1;
					const int tx = static_cast<int>(idx % layer.width);
					const int ty = static_cast<int>(idx / layer.width);
					TileFlip flip = TileFlip::None;
					if (flipH) flip = flip | TileFlip::Horizontal;
					if (flipV) flip = flip | TileFlip::Vertical;
					if (flipD) flip = flip | TileFlip::Diagonal;
					layer.setFlip(tx, ty, flip);
				}
			}
		}

		return layer;
	}

	/// @brief オブジェクトレイヤーをパースする
	[[nodiscard]] static std::optional<TiledObjectLayer> parseObjectLayer(
		const nlohmann::json& layerJson)
	{
		TiledObjectLayer objLayer;
		objLayer.name = layerJson.value("name", std::string{"objects"});
		objLayer.visible = layerJson.value("visible", true);

		if (layerJson.contains("objects") && layerJson["objects"].is_array())
		{
			for (const auto& objJson : layerJson["objects"])
			{
				TiledObject obj;
				obj.id = objJson.value("id", 0);
				obj.name = objJson.value("name", std::string{});
				obj.type = objJson.value("type", std::string{});
				obj.x = objJson.value("x", 0.0f);
				obj.y = objJson.value("y", 0.0f);
				obj.width = objJson.value("width", 0.0f);
				obj.height = objJson.value("height", 0.0f);
				obj.visible = objJson.value("visible", true);
				objLayer.objects.push_back(std::move(obj));
			}
		}

		return objLayer;
	}

	/// @brief グループレイヤーを再帰パースする
	static void parseGroupLayer(
		const nlohmann::json& groupJson,
		Tilemap& tilemap,
		TiledMapResult& result)
	{
		if (!groupJson.contains("layers") || !groupJson["layers"].is_array())
		{
			return;
		}

		for (const auto& layerJson : groupJson["layers"])
		{
			const auto layerType = layerJson.value("type", std::string{});

			if (layerType == "tilelayer")
			{
				auto layer = parseTileLayer(layerJson);
				if (layer.has_value())
				{
					tilemap.layers.push_back(std::move(*layer));
				}
			}
			else if (layerType == "objectgroup")
			{
				auto objLayer = parseObjectLayer(layerJson);
				if (objLayer.has_value())
				{
					result.objectLayers.push_back(std::move(*objLayer));
				}
			}
			else if (layerType == "group")
			{
				parseGroupLayer(layerJson, tilemap, result);
			}
		}
	}

#else // MITIRU_HAS_NLOHMANN_JSON == 0

	/// @brief nlohmann/json が未導入の場合のフォールバック
	/// @param jsonString JSON文字列（未使用）
	/// @return 常にnullopt
	[[nodiscard]] static std::optional<TiledMapResult> loadTiledMap(
		const std::string& jsonString)
	{
		static_cast<void>(jsonString);
		return std::nullopt;
	}

#endif // MITIRU_HAS_NLOHMANN_JSON
};

} // namespace mitiru::render
