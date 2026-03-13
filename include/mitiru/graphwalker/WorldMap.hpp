#pragma once

/// @file WorldMap.hpp
/// @brief ワールドマップデータ構造とJSON読み込み・保存
///
/// ワールドマップのデータモデル（プラットフォーム、収集物、チェックポイント、
/// NPC、ゲート、敵）を定義し、JSON文字列との相互変換を提供する。
///
/// @code
/// mitiru::gw::WorldMapLoader loader;
/// auto map = loader.createDefaultMap();
/// auto json = loader.saveToJson(map);
/// auto loaded = loader.loadFromJson(json);
/// @endcode

#include <string>
#include <vector>
#include <optional>
#include <sstream>
#include <cstddef>

#include "sgc/math/Vec2.hpp"

namespace mitiru::gw
{

/// @brief プラットフォームデータ
struct PlatformData
{
	sgc::Vec2f position;   ///< 左上座標
	sgc::Vec2f size;       ///< 幅と高さ
	std::string type;      ///< 種別（"static", "moving", "crumbling"等）
};

/// @brief 収集アイテムデータ
struct CollectibleData
{
	sgc::Vec2f position;   ///< 配置座標
	std::string buttonId;  ///< アンロックするボタンのID
};

/// @brief チェックポイントデータ
struct CheckpointData
{
	sgc::Vec2f position;   ///< 配置座標
	std::string id;        ///< チェックポイント識別子
	std::string name;      ///< 表示名
};

/// @brief NPCデータ
struct NpcData
{
	sgc::Vec2f position;   ///< 配置座標
	std::string id;        ///< NPC識別子
	std::string dialogue;  ///< 会話テキスト
	std::string questHint; ///< クエストヒント
};

/// @brief ゲートデータ
struct GateData
{
	sgc::Vec2f position;               ///< 配置座標
	sgc::Vec2f size;                   ///< ゲートサイズ
	std::vector<std::string> requiredButtons;  ///< 通過に必要なボタンID群
};

/// @brief 敵データ
struct EnemyData
{
	sgc::Vec2f patrolStart;  ///< 巡回開始地点
	sgc::Vec2f patrolEnd;    ///< 巡回終了地点
	float speed = 1.0f;      ///< 移動速度
	sgc::Vec2f size;         ///< 敵の当たり判定サイズ
};

/// @brief ワールドマップの全データを保持する構造体
struct WorldMapData
{
	sgc::Vec2f startPos;                       ///< プレイヤー開始位置
	std::vector<PlatformData> platforms;        ///< プラットフォーム一覧
	std::vector<CollectibleData> collectibles;  ///< 収集物一覧
	std::vector<CheckpointData> checkpoints;    ///< チェックポイント一覧
	std::vector<NpcData> npcs;                  ///< NPC一覧
	std::vector<GateData> gates;                ///< ゲート一覧
	std::vector<EnemyData> enemies;             ///< 敵一覧
};

/// @brief ワールドマップの読み込み・保存ユーティリティ
///
/// 簡易JSONパーサーとシリアライザーを内蔵する。
/// 外部JSON依存なしでワールドマップのロード・セーブが可能。
class WorldMapLoader
{
public:
	/// @brief JSON文字列からワールドマップを読み込む
	/// @param json JSON文字列
	/// @return 読み込み成功時はWorldMapData、失敗時はnullopt
	///
	/// @note 簡易パーサーのためネスト構造の完全なJSON仕様には対応しない。
	///       キーバリューペアの基本的な解析のみサポート。
	[[nodiscard]] std::optional<WorldMapData> loadFromJson(const std::string& json) const
	{
		WorldMapData data;

		// startPosの解析
		data.startPos = parseVec2(json, "startX", "startY");

		// platformsの解析
		auto platformsBlock = extractArray(json, "platforms");
		for (const auto& obj : extractObjects(platformsBlock))
		{
			PlatformData p;
			p.position.x = parseFloat(obj, "x");
			p.position.y = parseFloat(obj, "y");
			p.size.x = parseFloat(obj, "w");
			p.size.y = parseFloat(obj, "h");
			p.type = parseString(obj, "type");
			data.platforms.push_back(p);
		}

		// collectiblesの解析
		auto collectiblesBlock = extractArray(json, "collectibles");
		for (const auto& obj : extractObjects(collectiblesBlock))
		{
			CollectibleData c;
			c.position.x = parseFloat(obj, "x");
			c.position.y = parseFloat(obj, "y");
			c.buttonId = parseString(obj, "buttonId");
			data.collectibles.push_back(c);
		}

		// checkpointsの解析
		auto checkpointsBlock = extractArray(json, "checkpoints");
		for (const auto& obj : extractObjects(checkpointsBlock))
		{
			CheckpointData cp;
			cp.position.x = parseFloat(obj, "x");
			cp.position.y = parseFloat(obj, "y");
			cp.id = parseString(obj, "id");
			cp.name = parseString(obj, "name");
			data.checkpoints.push_back(cp);
		}

		// npcsの解析
		auto npcsBlock = extractArray(json, "npcs");
		for (const auto& obj : extractObjects(npcsBlock))
		{
			NpcData n;
			n.position.x = parseFloat(obj, "x");
			n.position.y = parseFloat(obj, "y");
			n.id = parseString(obj, "id");
			n.dialogue = parseString(obj, "dialogue");
			n.questHint = parseString(obj, "questHint");
			data.npcs.push_back(n);
		}

		// gatesの解析
		auto gatesBlock = extractArray(json, "gates");
		for (const auto& obj : extractObjects(gatesBlock))
		{
			GateData g;
			g.position.x = parseFloat(obj, "x");
			g.position.y = parseFloat(obj, "y");
			g.size.x = parseFloat(obj, "w");
			g.size.y = parseFloat(obj, "h");
			// requiredButtonsは簡易的にカンマ区切りで解析
			auto reqStr = parseString(obj, "requiredButtons");
			if (!reqStr.empty())
			{
				std::istringstream iss(reqStr);
				std::string token;
				while (std::getline(iss, token, ','))
				{
					// 前後の空白を除去
					auto start = token.find_first_not_of(" \t");
					auto end = token.find_last_not_of(" \t");
					if (start != std::string::npos)
					{
						g.requiredButtons.push_back(token.substr(start, end - start + 1));
					}
				}
			}
			data.gates.push_back(g);
		}

		// enemiesの解析
		auto enemiesBlock = extractArray(json, "enemies");
		for (const auto& obj : extractObjects(enemiesBlock))
		{
			EnemyData e;
			e.patrolStart.x = parseFloat(obj, "startX");
			e.patrolStart.y = parseFloat(obj, "startY");
			e.patrolEnd.x = parseFloat(obj, "endX");
			e.patrolEnd.y = parseFloat(obj, "endY");
			e.speed = parseFloat(obj, "speed");
			e.size.x = parseFloat(obj, "w");
			e.size.y = parseFloat(obj, "h");
			data.enemies.push_back(e);
		}

		return data;
	}

	/// @brief ワールドマップをJSON文字列に変換する
	/// @param data ワールドマップデータ
	/// @return JSON文字列
	[[nodiscard]] std::string saveToJson(const WorldMapData& data) const
	{
		std::ostringstream ss;
		ss << "{\n";
		ss << "  \"startX\": " << data.startPos.x << ",\n";
		ss << "  \"startY\": " << data.startPos.y << ",\n";

		// platforms
		ss << "  \"platforms\": [\n";
		for (std::size_t i = 0; i < data.platforms.size(); ++i)
		{
			const auto& p = data.platforms[i];
			ss << "    { \"x\": " << p.position.x << ", \"y\": " << p.position.y
			   << ", \"w\": " << p.size.x << ", \"h\": " << p.size.y
			   << ", \"type\": \"" << p.type << "\" }";
			if (i + 1 < data.platforms.size()) ss << ",";
			ss << "\n";
		}
		ss << "  ],\n";

		// collectibles
		ss << "  \"collectibles\": [\n";
		for (std::size_t i = 0; i < data.collectibles.size(); ++i)
		{
			const auto& c = data.collectibles[i];
			ss << "    { \"x\": " << c.position.x << ", \"y\": " << c.position.y
			   << ", \"buttonId\": \"" << c.buttonId << "\" }";
			if (i + 1 < data.collectibles.size()) ss << ",";
			ss << "\n";
		}
		ss << "  ],\n";

		// checkpoints
		ss << "  \"checkpoints\": [\n";
		for (std::size_t i = 0; i < data.checkpoints.size(); ++i)
		{
			const auto& cp = data.checkpoints[i];
			ss << "    { \"x\": " << cp.position.x << ", \"y\": " << cp.position.y
			   << ", \"id\": \"" << cp.id << "\", \"name\": \"" << cp.name << "\" }";
			if (i + 1 < data.checkpoints.size()) ss << ",";
			ss << "\n";
		}
		ss << "  ],\n";

		// npcs
		ss << "  \"npcs\": [\n";
		for (std::size_t i = 0; i < data.npcs.size(); ++i)
		{
			const auto& n = data.npcs[i];
			ss << "    { \"x\": " << n.position.x << ", \"y\": " << n.position.y
			   << ", \"id\": \"" << n.id
			   << "\", \"dialogue\": \"" << n.dialogue
			   << "\", \"questHint\": \"" << n.questHint << "\" }";
			if (i + 1 < data.npcs.size()) ss << ",";
			ss << "\n";
		}
		ss << "  ],\n";

		// gates
		ss << "  \"gates\": [\n";
		for (std::size_t i = 0; i < data.gates.size(); ++i)
		{
			const auto& g = data.gates[i];
			ss << "    { \"x\": " << g.position.x << ", \"y\": " << g.position.y
			   << ", \"w\": " << g.size.x << ", \"h\": " << g.size.y
			   << ", \"requiredButtons\": \"";
			for (std::size_t j = 0; j < g.requiredButtons.size(); ++j)
			{
				if (j > 0) ss << ",";
				ss << g.requiredButtons[j];
			}
			ss << "\" }";
			if (i + 1 < data.gates.size()) ss << ",";
			ss << "\n";
		}
		ss << "  ],\n";

		// enemies
		ss << "  \"enemies\": [\n";
		for (std::size_t i = 0; i < data.enemies.size(); ++i)
		{
			const auto& e = data.enemies[i];
			ss << "    { \"startX\": " << e.patrolStart.x << ", \"startY\": " << e.patrolStart.y
			   << ", \"endX\": " << e.patrolEnd.x << ", \"endY\": " << e.patrolEnd.y
			   << ", \"speed\": " << e.speed
			   << ", \"w\": " << e.size.x << ", \"h\": " << e.size.y << " }";
			if (i + 1 < data.enemies.size()) ss << ",";
			ss << "\n";
		}
		ss << "  ]\n";

		ss << "}";
		return ss.str();
	}

	/// @brief テスト用のデフォルトマップを生成する
	/// @return 基本的なプラットフォームと開始位置を持つマップ
	[[nodiscard]] WorldMapData createDefaultMap() const
	{
		WorldMapData data;

		// プレイヤー開始位置
		data.startPos = {100.0f, 400.0f};

		// 地面プラットフォーム
		data.platforms.push_back({
			{0.0f, 500.0f},
			{800.0f, 40.0f},
			"static"
		});

		// 中段プラットフォーム
		data.platforms.push_back({
			{200.0f, 350.0f},
			{150.0f, 20.0f},
			"static"
		});

		// 上段プラットフォーム
		data.platforms.push_back({
			{450.0f, 250.0f},
			{120.0f, 20.0f},
			"static"
		});

		// 収集アイテム（掛け算ボタン）
		data.collectibles.push_back({
			{275.0f, 320.0f},
			"*"
		});

		// チェックポイント
		data.checkpoints.push_back({
			{400.0f, 480.0f},
			"cp1",
			"First Checkpoint"
		});

		return data;
	}

private:
	/// @brief JSON文字列からVec2fを解析する
	/// @param json JSON文字列
	/// @param xKey X成分のキー名
	/// @param yKey Y成分のキー名
	/// @return 解析されたVec2f
	[[nodiscard]] static sgc::Vec2f parseVec2(
		const std::string& json, const std::string& xKey, const std::string& yKey)
	{
		return {parseFloat(json, xKey), parseFloat(json, yKey)};
	}

	/// @brief JSON文字列から浮動小数点値を抽出する
	/// @param json JSON文字列
	/// @param key キー名
	/// @return 値（見つからなければ0）
	[[nodiscard]] static float parseFloat(const std::string& json, const std::string& key)
	{
		const std::string pattern = "\"" + key + "\"";
		auto pos = json.find(pattern);
		if (pos == std::string::npos) return 0.0f;

		pos = json.find(':', pos + pattern.size());
		if (pos == std::string::npos) return 0.0f;
		++pos;

		// 空白スキップ
		while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
		{
			++pos;
		}

		try
		{
			std::size_t count = 0;
			float val = std::stof(json.substr(pos), &count);
			(void)count;
			return val;
		}
		catch (...)
		{
			return 0.0f;
		}
	}

	/// @brief JSON文字列から文字列値を抽出する
	/// @param json JSON文字列
	/// @param key キー名
	/// @return 文字列値（見つからなければ空文字列）
	[[nodiscard]] static std::string parseString(const std::string& json, const std::string& key)
	{
		const std::string pattern = "\"" + key + "\"";
		auto pos = json.find(pattern);
		if (pos == std::string::npos) return "";

		pos = json.find(':', pos + pattern.size());
		if (pos == std::string::npos) return "";
		++pos;

		// 空白スキップ
		while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
		{
			++pos;
		}

		if (pos >= json.size() || json[pos] != '"') return "";
		++pos;

		auto end = json.find('"', pos);
		if (end == std::string::npos) return "";

		return json.substr(pos, end - pos);
	}

	/// @brief JSON文字列から配列ブロックを抽出する
	/// @param json JSON全体
	/// @param key 配列のキー名
	/// @return 配列部分の文字列（[]を含む）
	[[nodiscard]] static std::string extractArray(const std::string& json, const std::string& key)
	{
		const std::string pattern = "\"" + key + "\"";
		auto pos = json.find(pattern);
		if (pos == std::string::npos) return "[]";

		pos = json.find('[', pos);
		if (pos == std::string::npos) return "[]";

		int depth = 0;
		std::size_t start = pos;
		for (std::size_t i = pos; i < json.size(); ++i)
		{
			if (json[i] == '[') ++depth;
			else if (json[i] == ']')
			{
				--depth;
				if (depth == 0)
				{
					return json.substr(start, i - start + 1);
				}
			}
		}

		return "[]";
	}

	/// @brief 配列ブロックから個々のオブジェクト文字列を抽出する
	/// @param arrayStr 配列JSON文字列
	/// @return 各オブジェクトの文字列の配列
	[[nodiscard]] static std::vector<std::string> extractObjects(const std::string& arrayStr)
	{
		std::vector<std::string> objects;
		int depth = 0;
		std::size_t start = 0;

		for (std::size_t i = 0; i < arrayStr.size(); ++i)
		{
			if (arrayStr[i] == '{')
			{
				if (depth == 0)
				{
					start = i;
				}
				++depth;
			}
			else if (arrayStr[i] == '}')
			{
				--depth;
				if (depth == 0)
				{
					objects.push_back(arrayStr.substr(start, i - start + 1));
				}
			}
		}

		return objects;
	}
};

} // namespace mitiru::gw
