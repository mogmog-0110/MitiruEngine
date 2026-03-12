#pragma once

/// @file Scripting.hpp
/// @brief 軽量スクリプティングスタブ
/// @details AIエージェントがJSONで記述したアクションシーケンスを解析する。
///          将来的なフルスクリプトエンジンの基盤となるスタブ実装。

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mitiru::control
{

/// @brief スクリプトアクション
/// @details 1つのスクリプト命令を表す。
///          typeでアクション種別を指定し、paramsで追加パラメータを渡す。
struct ScriptAction
{
	std::string type;                            ///< アクション種別
	std::map<std::string, std::string> params;   ///< パラメータ（key-value）

	/// @brief JSON文字列に変換する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{\"type\":\"" + type + "\",\"params\":{";
		bool first = true;
		for (const auto& [key, value] : params)
		{
			if (!first) json += ",";
			json += "\"" + key + "\":\"" + value + "\"";
			first = false;
		}
		json += "}}";
		return json;
	}
};

/// @brief スクリプトシーケンス（アクションの配列）
using ScriptSequence = std::vector<ScriptAction>;

/// @brief JSON文字列からスクリプトシーケンスを簡易パースする
/// @param jsonStr JSON配列文字列
/// @return パースされたスクリプトシーケンス（失敗時は空のシーケンス）
/// @note 簡易パーサーのため、正しいJSON形式を前提とする。
///       将来的には外部JSONライブラリとの統合を予定。
[[nodiscard]] inline ScriptSequence parseScript(const std::string& jsonStr)
{
	ScriptSequence sequence;

	/// アクション配列の各要素を抽出する簡易パーサー
	/// 各アクションの "type" フィールドを探索
	std::size_t pos = 0;
	while (pos < jsonStr.size())
	{
		const auto typePos = jsonStr.find("\"type\":", pos);
		if (typePos == std::string::npos)
		{
			break;
		}

		ScriptAction action;

		/// type値の抽出
		const auto typeQuoteStart = jsonStr.find('"', typePos + 7);
		if (typeQuoteStart == std::string::npos) break;

		const auto typeQuoteEnd = jsonStr.find('"', typeQuoteStart + 1);
		if (typeQuoteEnd == std::string::npos) break;

		action.type = jsonStr.substr(typeQuoteStart + 1,
			typeQuoteEnd - typeQuoteStart - 1);

		/// params ブロックの抽出
		const auto paramsPos = jsonStr.find("\"params\":", typeQuoteEnd);
		if (paramsPos != std::string::npos)
		{
			/// params開始ブレースを探す
			const auto braceStart = jsonStr.find('{', paramsPos + 9);
			if (braceStart != std::string::npos)
			{
				/// 対応する閉じブレースを探す
				int depth = 1;
				std::size_t braceEnd = braceStart + 1;
				while (braceEnd < jsonStr.size() && depth > 0)
				{
					if (jsonStr[braceEnd] == '{') ++depth;
					else if (jsonStr[braceEnd] == '}') --depth;
					++braceEnd;
				}

				/// paramsブロック内のkey-valueペアを抽出
				const std::string paramsBlock = jsonStr.substr(
					braceStart + 1, braceEnd - braceStart - 2);
				std::size_t pPos = 0;
				while (pPos < paramsBlock.size())
				{
					const auto keyStart = paramsBlock.find('"', pPos);
					if (keyStart == std::string::npos) break;

					const auto keyEnd = paramsBlock.find('"', keyStart + 1);
					if (keyEnd == std::string::npos) break;

					const auto valStart = paramsBlock.find('"', keyEnd + 1);
					if (valStart == std::string::npos) break;

					const auto valEnd = paramsBlock.find('"', valStart + 1);
					if (valEnd == std::string::npos) break;

					const std::string key = paramsBlock.substr(
						keyStart + 1, keyEnd - keyStart - 1);
					const std::string value = paramsBlock.substr(
						valStart + 1, valEnd - valStart - 1);

					action.params[key] = value;
					pPos = valEnd + 1;
				}

				pos = braceEnd;
			}
			else
			{
				pos = typeQuoteEnd + 1;
			}
		}
		else
		{
			pos = typeQuoteEnd + 1;
		}

		sequence.push_back(std::move(action));
	}

	return sequence;
}

/// @brief スクリプトシーケンスをJSON文字列に変換する
/// @param sequence 変換対象のシーケンス
/// @return JSON配列形式の文字列
[[nodiscard]] inline std::string scriptToJson(const ScriptSequence& sequence)
{
	std::string json;
	json += "[";
	for (std::size_t i = 0; i < sequence.size(); ++i)
	{
		if (i > 0) json += ",";
		json += sequence[i].toJson();
	}
	json += "]";
	return json;
}

} // namespace mitiru::control
