#pragma once

/// @file CommandSchema.hpp
/// @brief コマンド JSON スキーマ定義
/// @details AI エージェントが送信できるコマンドの形式を定義する。
///          コマンド文字列のパース・シリアライズ・スキーマ出力を提供する。

#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "mitiru/control/CommandQueue.hpp"

namespace mitiru::control
{

/// @brief サポートされるコマンドタイプ文字列
/// @details AI エージェントはこれらの文字列を Command.type に指定する。
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

/// @brief JSON 文字列からコマンドを読む
/// @param jsonStr {"type":"key_down","payload":"{\"keyCode\":65}"}。payload はオブジェクトそのままでもよい (文字列に戻して入れる)
/// @return "type" が文字列で無い・JSON として読めないときは nullopt
[[nodiscard]] inline std::optional<Command> parseCommand(const std::string& jsonStr)
{
	const auto doc = nlohmann::json::parse(jsonStr, nullptr, false);
	if (!doc.is_object()) return std::nullopt;
	const auto type = doc.find("type");
	if (type == doc.end() || !type->is_string()) return std::nullopt;

	Command cmd;
	cmd.type = type->get<std::string>();
	if (const auto payload = doc.find("payload"); payload != doc.end())
	{
		cmd.payload = payload->is_string() ? payload->get<std::string>() : payload->dump();
	}
	return cmd;
}

/// @brief コマンドを JSON 文字列に変換する。payload は文字列として入れ、空なら書かない。
[[nodiscard]] inline std::string commandToJson(const Command& command)
{
	nlohmann::ordered_json doc{{"type", command.type}};
	if (!command.payload.empty()) { doc["payload"] = command.payload; }
	return doc.dump();
}

/// @brief コマンドスキーマの JSON 定義を返す
/// @return JSON Schema 形式の文字列
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
