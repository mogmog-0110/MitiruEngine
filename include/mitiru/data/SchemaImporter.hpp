#pragma once

/// @file SchemaImporter.hpp
/// @brief JSON Schema draft-07 ドキュメントを `mitiru::data::Schema` へ変換する。
///
/// **目的。** value table やその他の authored content が、その形状を C++ ソース
/// ではなく外部の `*.schema.json` (JSON Schema draft-07) ファイルで宣言できる
/// ようにする。import された `Schema` は
/// `SchemaValidator::registerSchema` と `ContentLoader<T>::loadFileValidated`
/// パイプラインへ直接接続される。
///
/// **対応サブセット (draft-07)。**
/// - top-level `"type": "object"` は必須。それ以外の top-level type は reject。
/// - `"required": [field, ...]` — ここに列挙された名前は `SchemaField::required = true` になる。
/// - `"properties": { name: { type, minimum?, maximum?, minLength?, maxLength? } }`.
///   - `minimum` / `maximum` は `SchemaField::minValue` / `maxValue` に対応する。
///   - `minLength` / `maxLength` は **読むが破棄する**。現状エンジンの
///     `SchemaField` に文字列長 field が無いため。これは黙って捨てるのではなく
///     明示的・文書化された省略である。`SchemaField` に length field が
///     追加されたら再対応する。
/// - 受理される property `"type"` 文字列: `"string"`, `"integer"`, `"number"`,
///   `"boolean"`, `"array"`, `"object"`。対応する `FieldType` へ map する。
///
/// **明示的な draft-07 reject** (いずれも `error` を埋める):
/// - top-level `type != "object"` (例: `"array"`, `"string"`)
/// - top-level `"type"` 欠落
/// - `"properties"` map 欠落 (field 無しの object schema は不正扱い)
/// - `"type"` field を持たない property
/// - property `"type"` が array (union type — 非対応)
/// - property `"type"` が未知の文字列
/// - `"required"` が文字列 array でない
///
/// その他の draft-07 機能 (`$ref`, `oneOf`, `allOf`, `anyOf`, `enum`,
/// `pattern`, `additionalProperties`, `definitions`, `format`, `default`,
/// `items` schema, `$schema` URI 等) は黙殺せず
/// `SchemaImportResult::warnings` に **warning として記録される**。これらは
/// error を起こさず、結果の `Schema` にも漏れ出さないが、consumer は
/// `warnings` list を見て忠実度の喪失を検知できる。warning 形式は
/// `"<scope>: <feature> <reason>"` (例: `"field 'cost': enum constraint ignored
/// (SchemaField has no enum support)"`)。
///
/// @code
/// auto r = mitiru::data::SchemaImporter::fromJsonSchemaFile(
///     "data/unit.schema.json", "unit_row");
/// if (r.ok()) {
///     validator.registerSchema(*r.schema);
/// } else {
///     log(r.error);
/// }
/// @endcode
///
/// @note Header-only。static / stateless。Hot-path 規律: per-frame code から
///       呼ばないこと — boot 時の load や editor tooling 向け。

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "mitiru/data/Json.hpp"
#include "mitiru/data/SchemaValidator.hpp"

namespace mitiru::data
{

/// @brief JSON Schema draft-07 ドキュメント import の結果。
///
/// 成功時は `schema` が埋まり `error` は空。失敗時は `schema` が
/// `std::nullopt` で、`error` に可能なら問題の field 名を含む人間可読な
/// メッセージが入る。
///
/// `warnings` には、遭遇したが結果の `Schema` で表現できなかった draft-07
/// 機能を説明する人間可読メッセージが 0 個以上入る。各 warning は
/// `"<scope>: <feature> <reason>"` 形式に従う。consumer は忠実度の喪失
/// (例: enum constraint が黙って捨てられた) を検知したい時に `warnings` を
/// 確認すべき。`warnings` が空でないことは失敗を意味しない — import した
/// `Schema` が valid なら `ok()` は true を返す。
struct SchemaImportResult
{
    std::optional<Schema>    schema;
    std::string              error;
    std::vector<std::string> warnings;

    [[nodiscard]] bool ok() const noexcept { return schema.has_value(); }
    explicit operator bool() const noexcept { return ok(); }
};

/// @brief JSON Schema draft-07 → `mitiru::data::Schema` の stateless converter。
///
/// 対応 / reject される機能は file header を参照。
class SchemaImporter
{
public:
    SchemaImporter()  = default;
    ~SchemaImporter() = default;

    /// @brief 生の JSON Schema 文字列から import する。
    /// @param s          生 UTF-8 の JSON Schema ドキュメント。
    /// @param schemaName 生成される `Schema` に付ける名前 (`SchemaValidator::validate`
    ///                   の lookup で使われる)。
    [[nodiscard]] static SchemaImportResult fromJsonSchemaString(
        const std::string& s, std::string schemaName)
    {
        try {
            return fromJsonSchemaJson(Json::parse(s), std::move(schemaName));
        } catch (const std::exception& e) {
            return SchemaImportResult{ std::nullopt,
                std::string{"failed to parse JSON Schema: "} + e.what() };
        }
    }

    /// @brief parse 済みの JSON Schema object から import する。
    [[nodiscard]] static SchemaImportResult fromJsonSchemaJson(
        const Json& json, std::string schemaName)
    {
        return buildFromJson(json, std::move(schemaName));
    }

    /// @brief file path から import する。
    [[nodiscard]] static SchemaImportResult fromJsonSchemaFile(
        const std::string& path, std::string schemaName)
    {
        const auto json = loadJsonFile(path);
        if (!json) {
            return SchemaImportResult{ std::nullopt,
                "failed to open or parse JSON Schema file: " + path };
        }
        return buildFromJson(*json, std::move(schemaName));
    }

private:
    /// @brief top-level 構造を検証する。成功時は空文字列を返す。
    [[nodiscard]] static std::string validateRoot(const Json& j)
    {
        if (!j.is_object()) {
            return "JSON Schema root must be an object";
        }
        if (!j.contains("type")) {
            return "JSON Schema root missing required \"type\" key";
        }
        if (!j["type"].is_string() || j["type"].get<std::string>() != "object") {
            return "JSON Schema root \"type\" must be the string \"object\"";
        }
        if (!j.contains("properties") || !j["properties"].is_object()) {
            return "JSON Schema root missing \"properties\" object";
        }
        return {};
    }

    /// @brief top-level builder。`type=object` を強制し `properties` を走査する。
    [[nodiscard]] static SchemaImportResult buildFromJson(
        const Json& j, std::string name)
    {
        if (const auto rootErr = validateRoot(j); !rootErr.empty()) {
            return SchemaImportResult{ std::nullopt, rootErr, {} };
        }

        std::vector<std::string> warnings;
        collectTopLevelWarnings(j, warnings);

        std::vector<std::string> requiredNames;
        if (const auto reqErr = collectRequiredNames(j, requiredNames);
            !reqErr.empty()) {
            return SchemaImportResult{ std::nullopt, reqErr, std::move(warnings) };
        }

        Schema schema;
        schema.name    = std::move(name);
        schema.version = "1.0";

        if (const auto propsErr = buildProperties(
                j["properties"], requiredNames, schema, warnings);
            !propsErr.empty()) {
            return SchemaImportResult{ std::nullopt, propsErr,
                std::move(warnings) };
        }

        return SchemaImportResult{ std::move(schema), {}, std::move(warnings) };
    }

    /// @brief `properties` object を走査し field を `schema` に push する。
    ///        成功時は空文字列、最初に失敗した property があればその error
    ///        文字列を返す。
    [[nodiscard]] static std::string buildProperties(
        const Json&                     props,
        const std::vector<std::string>& requiredNames,
        Schema&                         schema,
        std::vector<std::string>&       warnings)
    {
        const auto isRequired = [&](const std::string& n) {
            for (const auto& r : requiredNames) {
                if (r == n) return true;
            }
            return false;
        };

        for (auto it = props.begin(); it != props.end(); ++it) {
            std::string fieldErr;
            auto        field = buildField(it.key(), it.value(),
                isRequired(it.key()), fieldErr, warnings);
            if (!field) return fieldErr;
            schema.fields.push_back(std::move(*field));
        }
        return {};
    }

    /// @brief `required` の名前を抽出する。成功時は空文字列、`required` field が
    ///        不正な場合はその理由を説明する error 文字列を返す。
    [[nodiscard]] static std::string collectRequiredNames(
        const Json& j, std::vector<std::string>& out)
    {
        if (!j.contains("required")) return {};
        const auto& req = j["required"];
        if (!req.is_array()) {
            return "JSON Schema \"required\" must be an array of strings";
        }
        out.reserve(req.size());
        for (const auto& item : req) {
            if (!item.is_string()) {
                return "JSON Schema \"required\" entries must be strings";
            }
            out.push_back(item.get<std::string>());
        }
        return {};
    }

    /// @brief importer が `Schema` で表現できない top-level draft-07 key
    ///        (composition, $ref, $schema URI 等) について warning を push する。
    static void collectTopLevelWarnings(
        const Json& j, std::vector<std::string>& warnings)
    {
        if (j.contains("$schema")) {
            warnings.emplace_back(
                "top-level: $schema URI is informational and ignored");
        }
        if (j.contains("$ref")) {
            warnings.emplace_back(
                "top-level: $ref ignored (importer does not resolve references)");
        }
        if (j.contains("oneOf") || j.contains("allOf") || j.contains("anyOf")) {
            warnings.emplace_back(
                "top-level: oneOf/allOf/anyOf composition ignored "
                "(flat schema only)");
        }
        if (j.contains("definitions")) {
            warnings.emplace_back(
                "top-level: definitions block ignored "
                "(importer does not resolve references)");
        }
        if (j.contains("additionalProperties")) {
            warnings.emplace_back(
                "top-level: additionalProperties constraint ignored "
                "(Schema does not model open/closed objects)");
        }
        if (j.contains("patternProperties")) {
            warnings.emplace_back(
                "top-level: patternProperties ignored "
                "(SchemaField has no regex support)");
        }
    }

    /// @brief importer が単一の `SchemaField` で表現できない property-level
    ///        draft-07 key について warning を push する。
    static void collectFieldWarnings(const std::string& fieldName,
                                     const Json&        spec,
                                     std::vector<std::string>& warnings)
    {
        const auto scope = std::string{"field '"} + fieldName + "': ";
        if (spec.contains("$ref")) {
            warnings.emplace_back(scope +
                "$ref ignored (importer does not resolve references)");
        }
        if (spec.contains("oneOf") || spec.contains("allOf") ||
            spec.contains("anyOf")) {
            warnings.emplace_back(scope +
                "oneOf/allOf/anyOf composition ignored (flat schema only)");
        }
        if (spec.contains("enum")) {
            warnings.emplace_back(scope +
                "enum constraint ignored (SchemaField has no enum support)");
        }
        if (spec.contains("pattern")) {
            warnings.emplace_back(scope +
                "pattern constraint ignored (SchemaField has no regex support)");
        }
        if (spec.contains("format")) {
            warnings.emplace_back(scope +
                "format hint ignored (SchemaField has no format field)");
        }
        if (spec.contains("default")) {
            warnings.emplace_back(scope +
                "default value ignored (SchemaField has no default field)");
        }
        if (spec.contains("items")) {
            warnings.emplace_back(scope +
                "items sub-schema ignored (importer does not recurse into arrays)");
        }
        if (spec.contains("additionalProperties")) {
            warnings.emplace_back(scope +
                "additionalProperties ignored "
                "(SchemaField does not model open/closed objects)");
        }
        if (spec.contains("minLength") || spec.contains("maxLength")) {
            warnings.emplace_back(scope +
                "minLength/maxLength ignored (SchemaField has no length fields)");
        }
        if (spec.contains("const")) {
            warnings.emplace_back(scope +
                "const constraint ignored (SchemaField has no const support)");
        }
    }

    /// @brief property spec node から単一の field を構築する。
    [[nodiscard]] static std::optional<SchemaField> buildField(
        std::string fieldName, const Json& spec, bool required,
        std::string& error, std::vector<std::string>& warnings)
    {
        if (!spec.is_object()) {
            error = "property \"" + fieldName + "\" must be an object";
            return std::nullopt;
        }
        if (!spec.contains("type")) {
            error = "property \"" + fieldName + "\" missing \"type\" key";
            return std::nullopt;
        }
        if (!spec["type"].is_string()) {
            error = "property \"" + fieldName +
                "\" \"type\" must be a string (union types not supported)";
            return std::nullopt;
        }

        const auto typeStr = spec["type"].get<std::string>();
        const auto mapped  = mapJsonSchemaType(typeStr);
        if (!mapped) {
            error = "property \"" + fieldName + "\" has unknown type \"" +
                typeStr + "\"";
            return std::nullopt;
        }

        // 捨てる前に非対応の key を記録しておく。
        collectFieldWarnings(fieldName, spec, warnings);

        SchemaField field;
        field.name        = std::move(fieldName);
        field.type        = *mapped;
        field.required    = required;
        field.description = spec.contains("description") &&
                                    spec["description"].is_string()
                              ? spec["description"].get<std::string>()
                              : std::string{};

        if (spec.contains("minimum") && spec["minimum"].is_number()) {
            field.minValue = spec["minimum"].get<float>();
        }
        if (spec.contains("maximum") && spec["maximum"].is_number()) {
            field.maxValue = spec["maximum"].get<float>();
        }
        // minLength/maxLength は今は意図的に読んで破棄する:
        // SchemaField に文字列長 field が無いため。header docs 参照。

        return field;
    }

    /// @brief draft-07 type 文字列を `FieldType` へ map する。未知 / 非対応の
    ///        type には nullopt を返す。
    [[nodiscard]] static std::optional<FieldType> mapJsonSchemaType(
        const std::string& s)
    {
        if (s == "string")  return FieldType::String;
        if (s == "integer") return FieldType::Int;
        if (s == "number")  return FieldType::Float;
        if (s == "boolean") return FieldType::Bool;
        if (s == "array")   return FieldType::Array;
        if (s == "object")  return FieldType::Object;
        return std::nullopt;
    }
};

} // namespace mitiru::data
