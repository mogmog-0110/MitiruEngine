#pragma once

/// @file Scripting.hpp
/// @brief 軽量スクリプティングスタブ
/// @details AI エージェントが JSON で記述したアクションシーケンスを解析する。
///          将来的なフルスクリプトエンジンの基盤となるスタブ実装。

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace mitiru::control
{

/// @brief スクリプトアクション
/// @details 1 つのスクリプト命令を表す。
///          type でアクション種別を指定し、params で追加パラメータを渡す。
struct ScriptAction
{
	std::string type;                            ///< アクション種別
	std::map<std::string, std::string> params;   ///< パラメータ（key-value）

	[[nodiscard]] nlohmann::ordered_json toJsonValue() const
	{
		return {{"type", type}, {"params", params}};
	}

	/// @brief {"type":"...","params":{...}} の JSON 文字列にする
	[[nodiscard]] std::string toJson() const
	{
		return toJsonValue().dump();
	}
};

/// @brief スクリプトシーケンス（アクションの配列）
using ScriptSequence = std::vector<ScriptAction>;

/// @brief JSON 配列からスクリプトシーケンスを読む
/// @details "type" が文字列でない要素は飛ばす。params の値が数値などのときは JSON 表記の文字列にする。
///          JSON として読めない・配列でないときは空。
[[nodiscard]] inline ScriptSequence parseScript(const std::string& jsonStr)
{
	ScriptSequence sequence;
	const auto doc = nlohmann::json::parse(jsonStr, nullptr, false);
	if (!doc.is_array()) return sequence;

	for (const auto& entry : doc)
	{
		const auto type = entry.is_object() ? entry.find("type") : entry.end();
		if (!entry.is_object() || type == entry.end() || !type->is_string()) continue;

		ScriptAction action;
		action.type = type->get<std::string>();
		if (const auto params = entry.find("params"); params != entry.end() && params->is_object())
		{
			for (const auto& [key, value] : params->items())
			{
				action.params[key] = value.is_string() ? value.get<std::string>() : value.dump();
			}
		}
		sequence.push_back(std::move(action));
	}
	return sequence;
}

/// @brief スクリプトシーケンスを JSON 配列の文字列にする
[[nodiscard]] inline std::string scriptToJson(const ScriptSequence& sequence)
{
	nlohmann::ordered_json arr = nlohmann::ordered_json::array();
	for (const auto& action : sequence) { arr.push_back(action.toJsonValue()); }
	return arr.dump();
}

} // namespace mitiru::control
