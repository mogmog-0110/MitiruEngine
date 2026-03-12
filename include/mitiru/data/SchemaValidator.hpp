#pragma once

/// @file SchemaValidator.hpp
/// @brief JSONスキーマ検証 — AI生成コンテンツの検証用
///
/// スキーマ定義に基づいてJSON文字列を検証し、
/// テンプレートJSONの自動生成も行う。
///
/// @code
/// mitiru::data::Schema schema;
/// schema.name = "entity";
/// schema.version = "1.0";
/// schema.fields.push_back({"name", FieldType::String, true, "", "entity name"});
///
/// mitiru::data::SchemaValidator validator;
/// validator.registerSchema(schema);
/// auto result = validator.validate("entity", jsonStr);
/// if (!result.valid) { /* result.errors にエラー一覧 */ }
/// @endcode

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "mitiru/data/JsonBuilder.hpp"

namespace mitiru::data
{

/// @brief スキーマフィールドの型
enum class FieldType
{
	String,  ///< 文字列
	Int,     ///< 整数
	Float,   ///< 浮動小数点
	Bool,    ///< 真偽値
	Array,   ///< 配列
	Object   ///< オブジェクト
};

/// @brief スキーマフィールド定義
struct SchemaField
{
	std::string name;                    ///< フィールド名
	FieldType type{FieldType::String};   ///< 型
	bool required{false};                ///< 必須フィールドか
	std::string defaultValue;            ///< デフォルト値（文字列表現）
	std::string description;             ///< 説明文
	std::optional<float> minValue;       ///< 最小値（数値型用）
	std::optional<float> maxValue;       ///< 最大値（数値型用）
};

/// @brief スキーマ定義
struct Schema
{
	std::string name;                    ///< スキーマ名
	std::string version;                 ///< バージョン
	std::vector<SchemaField> fields;     ///< フィールド定義リスト
};

/// @brief 検証結果
struct ValidationResult
{
	bool valid{true};                    ///< 検証成功か
	std::vector<std::string> errors;     ///< エラーメッセージ一覧
};

/// @brief JSONスキーマ検証器
///
/// スキーマを登録し、JSON文字列の検証やテンプレート生成を行う。
/// ビルトインスキーマとして entity, scene, prefab, tilemap を提供する。
class SchemaValidator
{
public:
	/// @brief コンストラクタ（ビルトインスキーマを登録）
	SchemaValidator()
	{
		registerBuiltinSchemas();
	}

	/// @brief スキーマを登録する
	/// @param schema スキーマ定義
	void registerSchema(const Schema& schema)
	{
		m_schemas[schema.name] = schema;
	}

	/// @brief JSON文字列をスキーマに基づいて検証する
	/// @param schemaName スキーマ名
	/// @param jsonString 検証対象のJSON文字列
	/// @return 検証結果
	[[nodiscard]] ValidationResult validate(
		const std::string& schemaName,
		const std::string& jsonString) const
	{
		ValidationResult result;

		auto it = m_schemas.find(schemaName);
		if (it == m_schemas.end())
		{
			result.valid = false;
			result.errors.push_back("Schema not found: " + schemaName);
			return result;
		}

		const auto& schema = it->second;

		/// JSONをパース
		JsonReader reader;
		if (!reader.parse(jsonString))
		{
			result.valid = false;
			result.errors.push_back("Invalid JSON format");
			return result;
		}

		/// 各フィールドを検証
		for (const auto& field : schema.fields)
		{
			validateField(reader, field, result);
		}

		return result;
	}

	/// @brief スキーマからテンプレートJSON文字列を生成する
	/// @param schemaName スキーマ名
	/// @return テンプレートJSON文字列（スキーマ不在時は空文字列）
	[[nodiscard]] std::string generateTemplate(const std::string& schemaName) const
	{
		auto it = m_schemas.find(schemaName);
		if (it == m_schemas.end()) return "";

		const auto& schema = it->second;
		JsonBuilder builder;
		builder.beginObject();

		for (const auto& field : schema.fields)
		{
			builder.key(field.name);
			appendDefaultValue(builder, field);
		}

		builder.endObject();
		return builder.build();
	}

	/// @brief 登録済みスキーマを取得する
	/// @param name スキーマ名
	/// @return スキーマ（存在しない場合nullopt）
	[[nodiscard]] std::optional<Schema> getSchema(const std::string& name) const
	{
		auto it = m_schemas.find(name);
		if (it == m_schemas.end()) return std::nullopt;
		return it->second;
	}

private:
	std::unordered_map<std::string, Schema> m_schemas;

	/// @brief フィールドを検証する
	void validateField(
		const JsonReader& reader,
		const SchemaField& field,
		ValidationResult& result) const
	{
		/// 必須フィールドの存在チェック
		bool hasValue = false;
		switch (field.type)
		{
		case FieldType::String:
			hasValue = reader.getString(field.name).has_value();
			break;
		case FieldType::Int:
			hasValue = reader.getInt(field.name).has_value();
			break;
		case FieldType::Float:
			hasValue = reader.getFloat(field.name).has_value();
			break;
		case FieldType::Bool:
			hasValue = reader.getBool(field.name).has_value();
			break;
		case FieldType::Array:
			hasValue = reader.getArray(field.name).has_value();
			break;
		case FieldType::Object:
			hasValue = reader.getObject(field.name).has_value();
			break;
		}

		if (field.required && !hasValue)
		{
			result.valid = false;
			result.errors.push_back("Missing required field: " + field.name);
			return;
		}

		if (!hasValue) return;

		/// 数値型の範囲チェック
		if (field.type == FieldType::Int && field.minValue.has_value())
		{
			auto val = reader.getInt(field.name);
			if (val.has_value() && static_cast<float>(*val) < *field.minValue)
			{
				result.valid = false;
				result.errors.push_back(
					field.name + " is below minimum value");
			}
		}
		if (field.type == FieldType::Int && field.maxValue.has_value())
		{
			auto val = reader.getInt(field.name);
			if (val.has_value() && static_cast<float>(*val) > *field.maxValue)
			{
				result.valid = false;
				result.errors.push_back(
					field.name + " exceeds maximum value");
			}
		}
		if (field.type == FieldType::Float && field.minValue.has_value())
		{
			auto val = reader.getFloat(field.name);
			if (val.has_value() && *val < *field.minValue)
			{
				result.valid = false;
				result.errors.push_back(
					field.name + " is below minimum value");
			}
		}
		if (field.type == FieldType::Float && field.maxValue.has_value())
		{
			auto val = reader.getFloat(field.name);
			if (val.has_value() && *val > *field.maxValue)
			{
				result.valid = false;
				result.errors.push_back(
					field.name + " exceeds maximum value");
			}
		}
	}

	/// @brief デフォルト値をビルダーに追加する
	static void appendDefaultValue(JsonBuilder& builder, const SchemaField& field)
	{
		if (!field.defaultValue.empty())
		{
			switch (field.type)
			{
			case FieldType::String:
				builder.value(field.defaultValue);
				break;
			case FieldType::Int:
				builder.value(std::stoi(field.defaultValue));
				break;
			case FieldType::Float:
				builder.value(std::stof(field.defaultValue));
				break;
			case FieldType::Bool:
				builder.value(field.defaultValue == "true");
				break;
			case FieldType::Array:
				builder.rawValue(field.defaultValue);
				break;
			case FieldType::Object:
				builder.rawValue(field.defaultValue);
				break;
			}
			return;
		}

		/// デフォルト値が未指定の場合の初期値
		switch (field.type)
		{
		case FieldType::String:  builder.value("");    break;
		case FieldType::Int:     builder.value(0);     break;
		case FieldType::Float:   builder.value(0.0f);  break;
		case FieldType::Bool:    builder.value(false);  break;
		case FieldType::Array:   builder.rawValue("[]"); break;
		case FieldType::Object:  builder.rawValue("{}"); break;
		}
	}

	/// @brief ビルトインスキーマを登録する
	void registerBuiltinSchemas()
	{
		/// エンティティスキーマ
		{
			Schema s;
			s.name = "entity";
			s.version = "1.0";
			s.fields = {
				{"name", FieldType::String, true, "", "Entity name"},
				{"position", FieldType::Object, false, R"json({"x":0,"y":0,"z":0})json", "World position"},
				{"rotation", FieldType::Object, false, R"json({"x":0,"y":0,"z":0})json", "Rotation (Euler)"},
				{"scale", FieldType::Object, false, R"json({"x":1,"y":1,"z":1})json", "Scale"},
				{"components", FieldType::Array, false, "[]", "Component list"},
			};
			m_schemas[s.name] = s;
		}

		/// シーンスキーマ
		{
			Schema s;
			s.name = "scene";
			s.version = "1.0";
			s.fields = {
				{"name", FieldType::String, true, "", "Scene name"},
				{"entities", FieldType::Array, false, "[]", "Entity list"},
				{"background", FieldType::String, false, "", "Background asset"},
			};
			m_schemas[s.name] = s;
		}

		/// プレハブスキーマ
		{
			Schema s;
			s.name = "prefab";
			s.version = "1.0";
			s.fields = {
				{"name", FieldType::String, true, "", "Prefab name"},
				{"components", FieldType::Object, false, "{}", "Component data"},
				{"children", FieldType::Array, false, "[]", "Child prefab names"},
			};
			m_schemas[s.name] = s;
		}

		/// タイルマップスキーマ
		{
			Schema s;
			s.name = "tilemap";
			s.version = "1.0";
			s.fields = {
				{"name", FieldType::String, true, "", "Tilemap name"},
				{"tileWidth", FieldType::Int, true, "32", "Tile width in pixels", 1.0f, 512.0f},
				{"tileHeight", FieldType::Int, true, "32", "Tile height in pixels", 1.0f, 512.0f},
				{"layers", FieldType::Array, false, "[]", "Tilemap layers"},
			};
			m_schemas[s.name] = s;
		}
	}
};

} // namespace mitiru::data
