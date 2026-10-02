#pragma once

/// @file SchemaValidator.hpp
/// @brief JSON スキーマ検証。AI 生成コンテンツの検証用
///
/// スキーマ定義に基づいて JSON 文字列を検証し、
/// テンプレート JSON の自動生成も行う。
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

#include <nlohmann/json.hpp>

#include "mitiru/data/JsonFields.hpp"

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

/// @brief JSON スキーマ検証器
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

	/// @brief JSON 文字列をスキーマに基づいて検証する
	/// @param schemaName スキーマ名
	/// @param jsonString 検証対象の JSON 文字列
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

		const auto doc = nlohmann::json::parse(jsonString, nullptr, false);
		if (!doc.is_object())
		{
			result.valid = false;
			result.errors.push_back("Invalid JSON format");
			return result;
		}

		for (const auto& field : it->second.fields)
		{
			validateField(doc, field, result);
		}
		return result;
	}

	/// @brief スキーマからテンプレート JSON 文字列を生成する
	/// @param schemaName スキーマ名
	/// @return テンプレート JSON 文字列（スキーマ不在時は空文字列）
	[[nodiscard]] std::string generateTemplate(const std::string& schemaName) const
	{
		auto it = m_schemas.find(schemaName);
		if (it == m_schemas.end()) return "";

		nlohmann::ordered_json doc = nlohmann::ordered_json::object();
		for (const auto& field : it->second.fields)
		{
			doc[field.name] = defaultValueOf(field);
		}
		return doc.dump();
	}

	/// @brief 登録済みスキーマを取得する
	/// @param name スキーマ名
	/// @return スキーマ（存在しない場合は nullopt）
	[[nodiscard]] std::optional<Schema> getSchema(const std::string& name) const
	{
		auto it = m_schemas.find(name);
		if (it == m_schemas.end()) return std::nullopt;
		return it->second;
	}

private:
	std::unordered_map<std::string, Schema> m_schemas;

	[[nodiscard]] static bool hasType(const nlohmann::json& v, FieldType type) noexcept
	{
		switch (type)
		{
		case FieldType::String: return v.is_string();
		case FieldType::Int:    return v.is_number_integer();
		case FieldType::Float:  return v.is_number();
		case FieldType::Bool:   return v.is_boolean();
		case FieldType::Array:  return v.is_array();
		case FieldType::Object: return v.is_object();
		}
		return false;
	}

	/// @brief フィールドを検証する。型が違う値は「無い」扱いにする。
	static void validateField(
		const nlohmann::json& doc,
		const SchemaField& field,
		ValidationResult& result)
	{
		const auto it = doc.find(field.name);
		const bool hasValue = it != doc.end() && hasType(*it, field.type);

		if (field.required && !hasValue)
		{
			result.valid = false;
			result.errors.push_back("Missing required field: " + field.name);
			return;
		}
		if (!hasValue) return;
		if (field.type != FieldType::Int && field.type != FieldType::Float) return;

		const float val = it->get<float>();
		if (field.minValue.has_value() && val < *field.minValue)
		{
			result.valid = false;
			result.errors.push_back(field.name + " is below minimum value");
		}
		if (field.maxValue.has_value() && val > *field.maxValue)
		{
			result.valid = false;
			result.errors.push_back(field.name + " exceeds maximum value");
		}
	}

	/// @brief テンプレートに入れる値。defaultValue が空なら型ごとの初期値。
	[[nodiscard]] static nlohmann::ordered_json defaultValueOf(const SchemaField& field)
	{
		const std::string& d = field.defaultValue;
		switch (field.type)
		{
		case FieldType::String: return d;
		case FieldType::Int:    return d.empty() ? 0 : std::stoi(d);
		case FieldType::Float:  return jsonFloat(d.empty() ? 0.0f : std::stof(d));
		case FieldType::Bool:   return d == "true";
		case FieldType::Array:
		case FieldType::Object:
			break;
		}
		auto parsed = nlohmann::ordered_json::parse(d, nullptr, false);
		const bool ok = field.type == FieldType::Array ? parsed.is_array() : parsed.is_object();
		if (ok) { return parsed; }
		return field.type == FieldType::Array ? nlohmann::ordered_json::array() : nlohmann::ordered_json::object();
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
