#pragma once

/// @file SaveData.hpp
/// @brief GraphWalker セーブ/ロードシステム
///
/// SharedDataをJSON文字列にシリアライズ/デシリアライズする。
/// mitiru::data::JsonBuilder / JsonReader を使用する。
///
/// @code
/// mitiru::gw::SharedData data = mitiru::gw::SaveData::createNewGame();
/// std::string json = mitiru::gw::SaveData::save(0, data);
/// auto loaded = mitiru::gw::SaveData::load(json);
/// @endcode

#include <optional>
#include <string>

#include "mitiru/data/JsonBuilder.hpp"
#include "mitiru/graphwalker/Common.hpp"

namespace mitiru::gw
{

/// @brief セーブスロットの概要情報
struct SaveSlot
{
	bool isEmpty = true;                   ///< 空きスロットか
	std::string checkpointId;              ///< 最後のチェックポイントID
	sgc::Vec2f position;                   ///< プレイヤー位置
	std::set<std::string> visited;         ///< 訪問済みチェックポイント
	std::set<std::string> collected;       ///< 収集済みアイテム
	std::set<std::string> buttons;         ///< アンロック済みボタン
	std::set<std::string> gates;           ///< アンロック済みゲート
	std::map<std::string, bool> achievements; ///< 実績
	float playTime = 0.0f;                 ///< プレイ時間（秒）
	int ngPlus = 0;                        ///< NG+レベル
	int colorblindMode = 0;                ///< 色覚モード
	bool highContrast = false;             ///< 高コントラスト
	std::string version;                   ///< セーブフォーマットバージョン
};

/// @brief セーブデータの読み書きを管理する
class SaveData
{
public:
	/// @brief 現在のセーブフォーマットバージョン
	static constexpr const char* CURRENT_VERSION = "v5";

	/// @brief SharedDataをJSON文字列にシリアライズする
	/// @param slotIndex スロット番号
	/// @param data 共有データ
	/// @return JSON文字列
	[[nodiscard]] static std::string save(int slotIndex, const SharedData& data)
	{
		data::JsonBuilder builder;
		builder.beginObject();

		builder.key("version").value(CURRENT_VERSION);
		builder.key("slot").value(slotIndex);

		/// ゲーム状態
		builder.key("state").value(static_cast<int>(data.state));
		builder.key("currentZone").value(static_cast<int>(data.currentZone));
		builder.key("checkpointId").value(data.currentCheckpointId);
		builder.key("spawnX").value(data.playerSpawnPos.x);
		builder.key("spawnY").value(data.playerSpawnPos.y);
		builder.key("playTime").value(data.playTime);
		builder.key("ngPlusLevel").value(data.ngPlusLevel);
		builder.key("highContrast").value(data.highContrast);
		builder.key("colorblindMode").value(data.colorblindMode);

		/// 訪問済みチェックポイント
		builder.key("visitedCheckpoints").beginArray();
		for (const auto& cp : data.visitedCheckpoints)
		{
			builder.value(cp);
		}
		builder.endArray();

		/// 収集済みアイテム
		builder.key("collectedItems").beginArray();
		for (const auto& item : data.collectedItems)
		{
			builder.value(item);
		}
		builder.endArray();

		/// アンロック済みボタン
		builder.key("unlockedButtons").beginArray();
		for (const auto& btn : data.unlockedButtons)
		{
			builder.value(btn);
		}
		builder.endArray();

		/// アンロック済みゲート
		builder.key("unlockedGates").beginArray();
		for (const auto& gate : data.unlockedGates)
		{
			builder.value(gate);
		}
		builder.endArray();

		/// 実績
		builder.key("achievements").beginObject();
		for (const auto& [id, achieved] : data.achievements)
		{
			builder.key(id).value(achieved);
		}
		builder.endObject();

		builder.endObject();
		return builder.build();
	}

	/// @brief JSON文字列からSharedDataにデシリアライズする
	/// @param json JSON文字列
	/// @return SharedData（パース失敗時はnullopt）
	[[nodiscard]] static std::optional<SharedData> load(const std::string& json)
	{
		data::JsonReader reader;
		if (!reader.parse(json))
		{
			return std::nullopt;
		}

		SharedData data;

		/// バージョン確認
		auto ver = reader.getString("version");
		if (!ver.has_value())
		{
			return std::nullopt;
		}

		/// ゲーム状態
		if (auto v = reader.getInt("state"))
		{
			data.state = static_cast<GameState>(v.value());
		}
		if (auto v = reader.getInt("currentZone"))
		{
			data.currentZone = static_cast<ZoneId>(v.value());
		}
		if (auto v = reader.getString("checkpointId"))
		{
			data.currentCheckpointId = v.value();
		}
		if (auto sx = reader.getFloat("spawnX"))
		{
			if (auto sy = reader.getFloat("spawnY"))
			{
				data.playerSpawnPos = sgc::Vec2f{ sx.value(), sy.value() };
			}
		}
		if (auto v = reader.getFloat("playTime"))
		{
			data.playTime = v.value();
		}
		if (auto v = reader.getInt("ngPlusLevel"))
		{
			data.ngPlusLevel = v.value();
		}
		if (auto v = reader.getBool("highContrast"))
		{
			data.highContrast = v.value();
		}
		if (auto v = reader.getInt("colorblindMode"))
		{
			data.colorblindMode = v.value();
		}
		if (auto v = reader.getInt("slot"))
		{
			data.currentSlot = v.value();
		}

		/// 配列フィールドのパース
		if (auto arr = reader.getArray("visitedCheckpoints"))
		{
			for (const auto& s : arr.value())
			{
				data.visitedCheckpoints.insert(s);
			}
		}
		if (auto arr = reader.getArray("collectedItems"))
		{
			for (const auto& s : arr.value())
			{
				data.collectedItems.insert(s);
			}
		}
		if (auto arr = reader.getArray("unlockedButtons"))
		{
			for (const auto& s : arr.value())
			{
				data.unlockedButtons.insert(s);
			}
		}
		if (auto arr = reader.getArray("unlockedGates"))
		{
			for (const auto& s : arr.value())
			{
				data.unlockedGates.insert(s);
			}
		}

		/// 実績のパース（ネストされたオブジェクト）
		if (auto achObj = reader.getObject("achievements"))
		{
			/// ネストされたJsonReaderから元のJSON文字列をパースし直す
			parseAchievements(achObj.value().source(), data.achievements);
		}

		return data;
	}

	/// @brief 新規ゲーム用のデフォルトSharedDataを生成する
	/// @return デフォルト状態のSharedData
	[[nodiscard]] static SharedData createNewGame()
	{
		SharedData data;
		data.state = GameState::Playing;
		data.currentSlot = 0;
		data.playerSpawnPos = { GameConstants::DEFAULT_SPAWN_X, GameConstants::DEFAULT_SPAWN_Y };
		data.currentCheckpointId = "";
		data.currentZone = ZoneId::Tutorial;
		data.playTime = 0.0f;
		data.ngPlusLevel = 0;
		data.highContrast = false;
		data.colorblindMode = 0;
		return data;
	}

	/// @brief SharedDataからSaveSlotの概要を生成する
	/// @param data 共有データ
	/// @return SaveSlot
	[[nodiscard]] static SaveSlot toSlot(const SharedData& data)
	{
		SaveSlot slot;
		slot.isEmpty = false;
		slot.checkpointId = data.currentCheckpointId;
		slot.position = data.playerSpawnPos;
		slot.visited = data.visitedCheckpoints;
		slot.collected = data.collectedItems;
		slot.buttons = data.unlockedButtons;
		slot.gates = data.unlockedGates;
		slot.achievements = data.achievements;
		slot.playTime = data.playTime;
		slot.ngPlus = data.ngPlusLevel;
		slot.colorblindMode = data.colorblindMode;
		slot.highContrast = data.highContrast;
		slot.version = CURRENT_VERSION;
		return slot;
	}

private:
	/// @brief 実績オブジェクトのJSON文字列をパースする
	/// @param json 実績部分のJSON文字列
	/// @param out 出力先マップ
	static void parseAchievements(
		const std::string& json,
		std::map<std::string, bool>& out)
	{
		/// 簡易パース: {"key1":true,"key2":false,...} 形式を処理
		size_t pos = 0;
		while (pos < json.size())
		{
			/// キーを探す
			auto keyStart = json.find('"', pos);
			if (keyStart == std::string::npos) break;
			auto keyEnd = json.find('"', keyStart + 1);
			if (keyEnd == std::string::npos) break;

			std::string key = json.substr(keyStart + 1, keyEnd - keyStart - 1);

			/// コロンの後の値を探す
			auto colonPos = json.find(':', keyEnd);
			if (colonPos == std::string::npos) break;

			pos = colonPos + 1;

			/// 空白スキップ
			while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;

			/// true/false判定
			if (pos + 4 <= json.size() && json.substr(pos, 4) == "true")
			{
				out[key] = true;
				pos += 4;
			}
			else if (pos + 5 <= json.size() && json.substr(pos, 5) == "false")
			{
				out[key] = false;
				pos += 5;
			}
			else
			{
				break;
			}

			/// カンマまたは閉じ括弧をスキップ
			while (pos < json.size() && json[pos] != ',' && json[pos] != '}') ++pos;
			if (pos < json.size() && json[pos] == ',') ++pos;
		}
	}
};

} // namespace mitiru::gw
