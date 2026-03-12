#pragma once

/// @file Snapshot.hpp
/// @brief シーンスナップショット
/// @details フレーム状態をJSON文字列として出力するための構造体。
///          Phase 0 では最小限のフレーム情報のみ。

#include <cstdint>
#include <string>

namespace mitiru
{

/// @brief スナップショットデータ
/// @details 現在のフレーム状態を保持する。
///          toJson() でJSON文字列に変換可能。
///          エンティティ/コンポーネント/シーン情報を含むリッチスナップショット。
struct SnapshotData
{
	std::uint64_t frameNumber = 0;  ///< フレーム番号
	float timestamp = 0.0f;         ///< タイムスタンプ（秒）
	int entityCount = 0;            ///< エンティティ数
	std::string worldSnapshot;      ///< MitiruWorldからのJSONスナップショット
	std::string sceneInfo;          ///< 現在のシーン名/情報
	int drawCallCount = 0;          ///< Screenの描画コール数

	/// @brief JSON文字列に変換する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		/// 手動でJSON文字列を構築（外部ライブラリ非依存）
		std::string json;
		json.reserve(256);
		json += "{";
		json += "\"frameNumber\":" + std::to_string(frameNumber) + ",";
		json += "\"timestamp\":" + std::to_string(timestamp) + ",";
		json += "\"entityCount\":" + std::to_string(entityCount) + ",";
		json += "\"drawCallCount\":" + std::to_string(drawCallCount);

		if (!worldSnapshot.empty())
		{
			json += ",\"world\":" + worldSnapshot;
		}

		if (!sceneInfo.empty())
		{
			json += ",\"sceneInfo\":\"" + sceneInfo + "\"";
		}

		json += "}";
		return json;
	}
};

} // namespace mitiru
