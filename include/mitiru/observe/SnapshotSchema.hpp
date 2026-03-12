#pragma once

/// @file SnapshotSchema.hpp
/// @brief スナップショットJSONスキーマ定義
/// @details AIエージェントがスナップショットの構造を理解するためのスキーマ情報。
///          エンティティ・UI要素のスキーマも含む。

#include <string>
#include <string_view>

namespace mitiru::observe
{

/// @brief エンティティスキーマ定義
/// @details スナップショット内のエンティティ情報の構造を記述する。
struct EntitySchema
{
	/// @brief エンティティスキーマのJSON定義を返す
	/// @return JSON Schema形式の文字列
	[[nodiscard]] static std::string schemaJson()
	{
		return R"({
  "type": "object",
  "properties": {
    "id": { "type": "integer", "description": "Entity unique identifier" },
    "name": { "type": "string", "description": "Entity display name" },
    "position": {
      "type": "object",
      "properties": {
        "x": { "type": "number" },
        "y": { "type": "number" }
      },
      "required": ["x", "y"]
    },
    "labels": {
      "type": "array",
      "items": { "type": "string" },
      "description": "Semantic labels for AI identification"
    },
    "state": {
      "type": "object",
      "additionalProperties": { "type": "string" },
      "description": "Key-value state pairs"
    }
  },
  "required": ["id"]
})";
	}
};

/// @brief UIスキーマ定義
/// @details スナップショット内のUI要素情報の構造を記述する。
struct UISchema
{
	/// @brief UIスキーマのJSON定義を返す
	/// @return JSON Schema形式の文字列
	[[nodiscard]] static std::string schemaJson()
	{
		return R"json({
  "type": "object",
  "properties": {
    "elementType": { "type": "string", "description": "UI element type (button, label, etc.)" },
    "id": { "type": "string", "description": "UI element identifier" },
    "bounds": {
      "type": "object",
      "properties": {
        "x": { "type": "number" },
        "y": { "type": "number" },
        "width": { "type": "number" },
        "height": { "type": "number" }
      },
      "required": ["x", "y", "width", "height"]
    },
    "visible": { "type": "boolean" },
    "enabled": { "type": "boolean" },
    "text": { "type": "string" }
  },
  "required": ["elementType", "id"]
})json";
	}
};

/// @brief スナップショットスキーマ
/// @details スナップショット全体のJSON Schema定義を提供する。
///          AIエージェントはこのスキーマを参照してデータ構造を理解する。
struct SnapshotSchema
{
	/// @brief スキーマバージョン
	static constexpr std::string_view VERSION = "2.0.0";

	/// @brief スナップショット全体のJSON Schema定義を返す
	/// @return JSON Schema形式の文字列
	[[nodiscard]] static std::string schemaJson()
	{
		std::string schema;
		schema += R"({
  "type": "object",
  "version": ")" + std::string(VERSION) + R"(",
  "properties": {
    "frameNumber": { "type": "integer", "description": "Current frame number" },
    "timestamp": { "type": "number", "description": "Elapsed time in seconds" },
    "entityCount": { "type": "integer", "description": "Total entity count" },
    "entities": {
      "type": "array",
      "items": )" + EntitySchema::schemaJson() + R"(
    },
    "ui": {
      "type": "array",
      "items": )" + UISchema::schemaJson() + R"(
    },
    "states": {
      "type": "object",
      "additionalProperties": { "type": "string" },
      "description": "Global observable state key-value pairs"
    },
    "diffs": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "key": { "type": "string" },
          "oldValue": { "type": "string" },
          "newValue": { "type": "string" },
          "frame": { "type": "integer" }
        }
      },
      "description": "State changes since last frame"
    }
  },
  "required": ["frameNumber", "timestamp"]
})";
		return schema;
	}
};

} // namespace mitiru::observe
