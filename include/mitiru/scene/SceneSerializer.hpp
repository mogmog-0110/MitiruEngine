#pragma once

/// @file SceneSerializer.hpp
/// @brief シーンシリアライズ/デシリアライズ
/// @details MitiruWorldの状態をJSON形式でシリアライズ・デシリアライズする。

#include <string>
#include <vector>

#include <mitiru/ecs/MitiruWorld.hpp>

namespace mitiru::scene
{

/// @brief シリアライズされたシーンデータ
struct SceneData
{
	std::string sceneName;      ///< シーン名
	std::string entitiesJson;   ///< エンティティ情報のJSON配列
	std::string metadata;       ///< 任意のメタデータJSON

	/// @brief JSON文字列に変換する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"sceneName\":\"" + sceneName + "\",";
		json += "\"entities\":" + entitiesJson + ",";
		json += "\"metadata\":" + (metadata.empty() ? "{}" : metadata);
		json += "}";
		return json;
	}
};

/// @brief シーンシリアライザ
/// @details MitiruWorldの状態をJSON形式で入出力する。
///
/// @code
/// auto json = mitiru::scene::SceneSerializer::serialize(world, "Level1");
/// auto data = mitiru::scene::SceneSerializer::deserialize(json);
/// @endcode
class SceneSerializer
{
public:
	/// @brief MitiruWorldの状態をJSON文字列にシリアライズする
	/// @param world 対象のワールド
	/// @param sceneName シーン名
	/// @return JSON形式の文字列
	[[nodiscard]] static std::string serialize(
		const ecs::MitiruWorld& world, const std::string& sceneName)
	{
		SceneData data;
		data.sceneName = sceneName;
		data.entitiesJson = world.snapshot();
		data.metadata = "{}";
		return data.toJson();
	}

	/// @brief JSON文字列からSceneDataをデシリアライズする（簡易パーサー）
	/// @param json JSON形式の文字列
	/// @return デシリアライズされたSceneData
	/// @note 簡易実装のため、正規のJSONパーサーではない
	[[nodiscard]] static SceneData deserialize(const std::string& json)
	{
		SceneData data;

		/// "sceneName":"..." の抽出
		const auto nameKey = json.find("\"sceneName\":\"");
		if (nameKey != std::string::npos)
		{
			const auto nameStart = nameKey + 13;
			const auto nameEnd = json.find('"', nameStart);
			if (nameEnd != std::string::npos)
			{
				data.sceneName = json.substr(nameStart, nameEnd - nameStart);
			}
		}

		/// "entities":{ ... } の抽出
		const auto entKey = json.find("\"entities\":");
		if (entKey != std::string::npos)
		{
			const auto entStart = entKey + 11;
			/// ブレース/ブラケットのネスト追跡
			data.entitiesJson = extractJsonValue(json, entStart);
		}

		/// "metadata":{ ... } の抽出
		const auto metaKey = json.find("\"metadata\":");
		if (metaKey != std::string::npos)
		{
			const auto metaStart = metaKey + 11;
			data.metadata = extractJsonValue(json, metaStart);
		}

		return data;
	}

private:
	/// @brief JSON値を抽出する（オブジェクトまたは配列）
	/// @param json 元のJSON文字列
	/// @param start 値の開始位置
	/// @return 抽出された値文字列
	[[nodiscard]] static std::string extractJsonValue(
		const std::string& json, std::size_t start)
	{
		if (start >= json.size())
		{
			return "{}";
		}

		const char openChar = json[start];
		char closeChar = '}';
		if (openChar == '[')
		{
			closeChar = ']';
		}
		else if (openChar != '{')
		{
			/// 文字列やプリミティブの場合はカンマか閉じブレースまで
			const auto end = json.find_first_of(",}", start);
			return json.substr(start, end - start);
		}

		int depth = 0;
		for (std::size_t i = start; i < json.size(); ++i)
		{
			if (json[i] == openChar)
			{
				++depth;
			}
			else if (json[i] == closeChar)
			{
				--depth;
				if (depth == 0)
				{
					return json.substr(start, i - start + 1);
				}
			}
		}

		return json.substr(start);
	}
};

} // namespace mitiru::scene
