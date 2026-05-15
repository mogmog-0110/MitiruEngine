#pragma once

/// @file SchemaImporter.hpp
/// @brief Convert a JSON Schema draft-07 document into a `mitiru::data::Schema`.
///
/// **Purpose.** Allow value tables and other authored content to declare their
/// shape in an external `*.schema.json` (JSON Schema draft-07) file rather than
/// in C++ source. The imported `Schema` plugs directly into
/// `SchemaValidator::registerSchema` and the `ContentLoader<T>::loadFileValidated`
/// pipeline.
///
/// **Supported subset (draft-07).**
/// - Top-level `"type": "object"` is REQUIRED. Any other top-level type is rejected.
/// - `"required": [field, ...]` — names listed here become `SchemaField::required = true`.
/// - `"properties": { name: { type, minimum?, maximum?, minLength?, maxLength? } }`.
///   - `minimum` / `maximum` map onto `SchemaField::minValue` / `maxValue`.
///   - `minLength` / `maxLength` are **read but discarded** because the engine's
///     `SchemaField` has no per-string length fields today; this is an explicit,
///     documented omission rather than a silent drop. Re-add when `SchemaField`
///     grows length fields.
/// - Property `"type"` strings accepted: `"string"`, `"integer"`, `"number"`,
///   `"boolean"`, `"array"`, `"object"`. They map to the matching `FieldType`.
///
/// **Explicit draft-07 rejections** (any of these populates `error`):
/// - top-level `type != "object"` (e.g. `"array"`, `"string"`)
/// - missing top-level `"type"`
/// - missing `"properties"` map (an object schema with no fields is treated as malformed)
/// - property without a `"type"` field
/// - property `"type"` is an array (union types — not supported)
/// - property `"type"` is an unknown string
/// - `"required"` is not an array of strings
///
/// All other draft-07 features (`$ref`, `oneOf`, `allOf`, `anyOf`, `enum`,
/// `pattern`, `additionalProperties`, `definitions`, `format`, `default`,
/// `items` schemas, `$schema` URI, etc.) are **captured as warnings** in
/// `SchemaImportResult::warnings` rather than silently ignored. They do not
/// cause errors and do not leak into the resulting `Schema`, but consumers can
/// inspect the `warnings` list to detect fidelity loss. The warning format is
/// `"<scope>: <feature> <reason>"` (e.g. `"field 'cost': enum constraint ignored
/// (SchemaField has no enum support)"`).
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
/// @note Header-only. Static, stateless. Hot-path discipline: do NOT call from
///       per-frame code — intended for boot-time loads and editor tooling.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "mitiru/data/Json.hpp"
#include "mitiru/data/SchemaValidator.hpp"

namespace mitiru::data
{

/// @brief Result of importing a JSON Schema draft-07 document.
///
/// On success `schema` is populated and `error` is empty. On failure `schema`
/// is `std::nullopt` and `error` contains a human-readable message naming the
/// offending field where possible.
///
/// `warnings` contains zero or more human-readable messages describing draft-07
/// features that were encountered but could not be represented in the resulting
/// `Schema`. Each warning follows the format
/// `"<scope>: <feature> <reason>"`. Consumers should inspect `warnings` when
/// they need to detect fidelity loss (e.g. an enum constraint silently dropped).
/// A non-empty `warnings` list does NOT imply failure — `ok()` still returns
/// true when the imported `Schema` is valid.
struct SchemaImportResult
{
    std::optional<Schema>    schema;
    std::string              error;
    std::vector<std::string> warnings;

    [[nodiscard]] bool ok() const noexcept { return schema.has_value(); }
    explicit operator bool() const noexcept { return ok(); }
};

/// @brief Stateless converter from JSON Schema draft-07 to `mitiru::data::Schema`.
///
/// See file header for supported / rejected features.
class SchemaImporter
{
public:
    SchemaImporter()  = default;
    ~SchemaImporter() = default;

    /// @brief Import from a raw JSON Schema string.
    /// @param s          Raw UTF-8 JSON Schema document.
    /// @param schemaName Name assigned to the produced `Schema` (used by
    ///                   `SchemaValidator::validate` lookups).
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

    /// @brief Import from an already-parsed JSON Schema object.
    [[nodiscard]] static SchemaImportResult fromJsonSchemaJson(
        const Json& json, std::string schemaName)
    {
        return buildFromJson(json, std::move(schemaName));
    }

    /// @brief Import from a file path.
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
    /// @brief Validate top-level structure. Returns empty string on success.
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

    /// @brief Top-level builder. Enforces `type=object`, walks `properties`.
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

    /// @brief Walk the `properties` object, push fields onto `schema`. Returns
    ///        empty string on success, or an error string for the first failing
    ///        property.
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

    /// @brief Extract `required` names. Returns empty string on success, or
    ///        an error string describing why the `required` field is malformed.
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

    /// @brief Push warnings for top-level draft-07 keys that the importer cannot
    ///        represent in `Schema` (composition, $ref, $schema URI, etc.).
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

    /// @brief Push warnings for property-level draft-07 keys that the importer
    ///        cannot represent in a single `SchemaField`.
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

    /// @brief Build a single field from a property spec node.
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

        // Record any unsupported keys before we drop them on the floor.
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
        // minLength/maxLength are intentionally read-and-discard for now:
        // SchemaField has no string-length fields. See header docs.

        return field;
    }

    /// @brief Map a draft-07 type string onto `FieldType`. Returns nullopt for
    ///        unknown / unsupported types.
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
