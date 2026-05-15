#pragma once

/// @file CommandSchema.hpp
/// @brief コマンドJSONスキーマ定義
/// @details AIエージェントが送信できるコマンドの形式を定義する。
///          コマンド文字列のパース・シリアライズ・スキーマ出力を提供する。

#include <optional>
#include <string>
#include <string_view>

#include "mitiru/control/CommandQueue.hpp"

namespace mitiru::control
{

/// @brief サポートされるコマンドタイプ文字列
/// @details AIエージェントはこれらの文字列をCommand.typeに指定する。
namespace CommandType
{
	inline constexpr std::string_view KeyDown     = "key_down";      ///< キー押下
	inline constexpr std::string_view KeyUp       = "key_up";        ///< キー解放
	inline constexpr std::string_view MouseMove   = "mouse_move";    ///< マウス移動
	inline constexpr std::string_view MouseDown   = "mouse_down";    ///< マウスボタン押下
	inline constexpr std::string_view MouseUp     = "mouse_up";      ///< マウスボタン解放
	inline constexpr std::string_view WaitFrames  = "wait_frames";   ///< フレーム待機
	inline constexpr std::string_view Snapshot    = "snapshot";      ///< スナップショット要求
	inline constexpr std::string_view Capture     = "capture";       ///< 画面キャプチャ要求
	inline constexpr std::string_view Step        = "step";          ///< 1フレーム進行
} // namespace CommandType

/// @brief JSON文字列からコマンドを簡易パースする
/// @param jsonStr JSON文字列（例: {"type":"key_down","payload":"{\"keyCode\":65}"}）
/// @return パース成功時はCommand、失敗時はnullopt
/// @note 簡易パーサーのため、正しいJSON形式を前提とする
[[nodiscard]] inline std::optional<Command> parseCommand(const std::string& jsonStr)
{
	Command cmd;

	/// type の抽出
	const auto typePos = jsonStr.find("\"type\":");
	if (typePos == std::string::npos)
	{
		return std::nullopt;
	}

	/// type値の開始引用符を探す
	const auto typeQuoteStart = jsonStr.find('"', typePos + 7);
	if (typeQuoteStart == std::string::npos)
	{
		return std::nullopt;
	}
	const auto typeQuoteEnd = jsonStr.find('"', typeQuoteStart + 1);
	if (typeQuoteEnd == std::string::npos)
	{
		return std::nullopt;
	}
	cmd.type = jsonStr.substr(typeQuoteStart + 1, typeQuoteEnd - typeQuoteStart - 1);

	/// payload の抽出（オプション）
	const auto payloadPos = jsonStr.find("\"payload\":");
	if (payloadPos != std::string::npos)
	{
		const auto payloadQuoteStart = jsonStr.find('"', payloadPos + 10);
		if (payloadQuoteStart != std::string::npos)
		{
			const auto payloadQuoteEnd = jsonStr.find('"', payloadQuoteStart + 1);
			if (payloadQuoteEnd != std::string::npos)
			{
				cmd.payload = jsonStr.substr(
					payloadQuoteStart + 1,
					payloadQuoteEnd - payloadQuoteStart - 1
				);
			}
		}
	}

	return cmd;
}

/// @brief コマンドをJSON文字列に変換する
/// @param command 変換対象のコマンド
/// @return JSON形式の文字列
[[nodiscard]] inline std::string commandToJson(const Command& command)
{
	std::string json;
	json += "{\"type\":\"" + command.type + "\"";
	if (!command.payload.empty())
	{
		json += ",\"payload\":\"" + command.payload + "\"";
	}
	json += "}";
	return json;
}

/// @brief コマンドスキーマのJSON定義を返す
/// @return JSON Schema形式の文字列
[[nodiscard]] inline std::string schemaJson()
{
	return R"({
  "type": "object",
  "properties": {
    "type": {
      "type": "string",
      "enum": [
        "key_down", "key_up",
        "mouse_move", "mouse_down", "mouse_up",
        "wait_frames", "snapshot", "capture", "step"
      ],
      "description": "Command type identifier"
    },
    "payload": {
      "type": "string",
      "description": "JSON-encoded payload specific to the command type"
    }
  },
  "required": ["type"],
  "descriptions": {
    "key_down": "Simulate a key press. Payload: {\"keyCode\": <int>}",
    "key_up": "Simulate a key release. Payload: {\"keyCode\": <int>}",
    "mouse_move": "Move the mouse cursor. Payload: {\"x\": <float>, \"y\": <float>}",
    "mouse_down": "Simulate mouse button press. Payload: {\"button\": <int>}",
    "mouse_up": "Simulate mouse button release. Payload: {\"button\": <int>}",
    "wait_frames": "Wait for N frames. Payload: {\"count\": <int>}",
    "snapshot": "Request a state snapshot. No payload required.",
    "capture": "Request a screen capture. No payload required.",
    "step": "Advance one frame. No payload required."
  }
})";
}

} // namespace mitiru::control
