#pragma once

/// @file MapData.hpp
/// @brief 汎用マップデータ構造（レベルエディタ・ゲームマップ用）
///
/// ゲームのマップ/レベルデータを構造化して管理する。
/// エンティティ（配置物）のリストとメタデータを持ち、
/// JSON形式でのシリアライズ/デシリアライズに対応する。
///
/// @code
/// using namespace sgc::data;
/// // マップを構築
/// MapEntity platform;
/// platform.type = "platform";
/// platform.position = {10.0f, -1.0f};
/// platform.size = {28.0f, 2.0f};
/// platform.properties["color"] = DataNode("blue");
///
/// MapFile map;
/// map.metadata["name"] = DataNode("Tutorial Zone");
/// map.metadata["version"] = DataNode(1);
/// map.entities.push_back(platform);
///
/// // JSONに書き出し
/// std::string json = map.toJson(true);
///
/// // JSONから読み込み
/// auto loaded = MapFile::fromJson(json);
/// @endcode

#include "sgc/data/DataNode.hpp"
#include "sgc/math/Vec2.hpp"
#include <vector>
#include <string>

namespace sgc::data
{

/// @brief マップ上のエンティティ（配置物）定義
struct MapEntity
{
	std::string type;          ///< エンティティの種類（"platform", "checkpoint", "npc" 等）
	std::string id;            ///< 一意の識別子（オプション）
	Vec2f position{};          ///< 位置（論理座標）
	Vec2f size{};              ///< サイズ（論理座標）
	DataNode properties;       ///< カスタムプロパティ（型ごとに異なる）

	/// @brief DataNodeに変換する
	[[nodiscard]] DataNode toNode() const
	{
		auto node = DataNode::object();
		node["type"] = DataNode(type);
		if (!id.empty())
		{
			node["id"] = DataNode(id);
		}
		node["x"] = DataNode(static_cast<double>(position.x));
		node["y"] = DataNode(static_cast<double>(position.y));
		if (size.x != 0.0f || size.y != 0.0f)
		{
			node["w"] = DataNode(static_cast<double>(size.x));
			node["h"] = DataNode(static_cast<double>(size.y));
		}
		if (properties.isObject() && properties.size() > 0)
		{
			node["properties"] = properties;
		}
		return node;
	}

	/// @brief DataNodeから構築する
	[[nodiscard]] static MapEntity fromNode(const DataNode& node)
	{
		MapEntity e;
		e.type = node["type"].asString();
		e.id = node["id"].asString();
		e.position.x = static_cast<float>(node["x"].asFloat(0.0));
		e.position.y = static_cast<float>(node["y"].asFloat(0.0));
		e.size.x = static_cast<float>(node["w"].asFloat(0.0));
		e.size.y = static_cast<float>(node["h"].asFloat(0.0));
		if (node.hasKey("properties"))
		{
			e.properties = node["properties"];
		}
		return e;
	}
};

/// @brief マップファイル全体のデータ
struct MapFile
{
	DataNode metadata;                ///< メタデータ（名前、バージョン等）
	std::vector<MapEntity> entities;  ///< エンティティリスト

	/// @brief JSONに変換する
	/// @param pretty 整形するか
	/// @return JSON文字列
	[[nodiscard]] std::string toJson(bool pretty = false) const
	{
		auto root = DataNode::object();
		root["metadata"] = metadata.isObject() ? metadata : DataNode::object();

		auto arr = DataNode::array();
		for (const auto& e : entities)
		{
			arr.push_back(e.toNode());
		}
		root["entities"] = std::move(arr);

		return root.toJson(pretty);
	}

	/// @brief JSON文字列からマップを構築する
	/// @param json JSON文字列
	/// @return マップデータ
	[[nodiscard]] static MapFile fromJson(const std::string& json)
	{
		MapFile map;
		auto root = DataNode::parse(json);

		if (root.hasKey("metadata"))
		{
			map.metadata = root["metadata"];
		}

		if (root.hasKey("entities"))
		{
			const auto& arr = root["entities"];
			for (size_t i = 0; i < arr.size(); ++i)
			{
				map.entities.push_back(MapEntity::fromNode(arr[i]));
			}
		}

		return map;
	}
};

/// @brief パラメータテーブル（DataNodeのラッパー、ゲーム設定用）
///
/// ドットパスアクセスとデフォルト値付き取得を提供するラッパー。
/// JSONファイルからロードしたパラメータに型安全にアクセスする。
///
/// @code
/// auto params = ParamTable::fromJson(jsonStr);
/// float speed = params.getFloat("player.speed", 5200.0f);
/// int hp = params.getInt("player.hp", 100);
/// std::string name = params.getString("player.name", "Hero");
/// @endcode
class ParamTable
{
public:
	/// @brief デフォルトコンストラクタ（空のテーブル）
	ParamTable() : m_root(DataNode::object()) {}

	/// @brief DataNodeから構築する
	explicit ParamTable(DataNode root) : m_root(std::move(root)) {}

	/// @brief JSON文字列からパラメータテーブルを構築する
	[[nodiscard]] static ParamTable fromJson(const std::string& json)
	{
		return ParamTable(DataNode::parse(json));
	}

	/// @brief JSON文字列に変換する
	[[nodiscard]] std::string toJson(bool pretty = false) const
	{
		return m_root.toJson(pretty);
	}

	/// @brief float値を取得する
	[[nodiscard]] float getFloat(const std::string& path, float defaultVal = 0.0f) const
	{
		return static_cast<float>(m_root.at(path).asFloat(static_cast<double>(defaultVal)));
	}

	/// @brief double値を取得する
	[[nodiscard]] double getDouble(const std::string& path, double defaultVal = 0.0) const
	{
		return m_root.at(path).asFloat(defaultVal);
	}

	/// @brief int値を取得する
	[[nodiscard]] int getInt(const std::string& path, int defaultVal = 0) const
	{
		return static_cast<int>(m_root.at(path).asInt(defaultVal));
	}

	/// @brief bool値を取得する
	[[nodiscard]] bool getBool(const std::string& path, bool defaultVal = false) const
	{
		return m_root.at(path).asBool(defaultVal);
	}

	/// @brief 文字列を取得する
	[[nodiscard]] std::string getString(const std::string& path, const std::string& defaultVal = "") const
	{
		const auto& node = m_root.at(path);
		if (node.isString()) return node.asString();
		return defaultVal;
	}

	/// @brief Vec2fを取得する（配列 [x, y] 形式）
	[[nodiscard]] Vec2f getVec2(const std::string& path, const Vec2f& defaultVal = {}) const
	{
		const auto& node = m_root.at(path);
		if (node.isArray() && node.size() >= 2)
		{
			return Vec2f{
				static_cast<float>(node[0].asFloat(defaultVal.x)),
				static_cast<float>(node[1].asFloat(defaultVal.y))
			};
		}
		return defaultVal;
	}

	/// @brief セクション（サブテーブル）を取得する
	[[nodiscard]] ParamTable section(const std::string& path) const
	{
		return ParamTable(m_root.at(path));
	}

	/// @brief 値をセットする
	void setFloat(const std::string& key, float value)
	{
		m_root[key] = DataNode(static_cast<double>(value));
	}

	void setInt(const std::string& key, int value)
	{
		m_root[key] = DataNode(static_cast<int64_t>(value));
	}

	void setBool(const std::string& key, bool value)
	{
		m_root[key] = DataNode(value);
	}

	void setString(const std::string& key, const std::string& value)
	{
		m_root[key] = DataNode(value);
	}

	/// @brief 内部DataNodeへの参照を取得する
	[[nodiscard]] const DataNode& root() const noexcept { return m_root; }
	[[nodiscard]] DataNode& root() noexcept { return m_root; }

private:
	DataNode m_root;
};

} // namespace sgc::data
