#pragma once

/// @file ContentLoader.hpp
/// @brief Stateless typed content loader — JSON file or in-memory JSON -> C++ struct in one call.
///
/// **Purpose.** §9 data-driven authoring: balance tables, dialogue scripts, level data, and
/// any other read-only authored content can be declared in JSON and loaded as typed C++ structs
/// at startup or scene-load time.
///
/// **User-side type opt-in.** Register your struct with nlohmann's serialization macros:
///
/// @code
/// struct BalanceRow { std::string name; int cost; float winRate; };
/// NLOHMANN_DEFINE_TYPE_INTRUSIVE(BalanceRow, name, cost, winRate)
/// @endcode
///
/// Then load in one line:
///
/// @code
/// // Single struct
/// auto r = mitiru::data::ContentLoader<BalanceRow>::loadFile("data/balance.json");
/// if (r.ok()) { const BalanceRow& row = *r.value; }
///
/// // Array of structs
/// auto r2 = mitiru::data::ContentLoader<std::vector<BalanceRow>>::loadFile("data/units.json");
///
/// // Nested struct
/// struct BalanceTable { std::vector<BalanceRow> rows; };
/// NLOHMANN_DEFINE_TYPE_INTRUSIVE(BalanceTable, rows)
/// auto r3 = mitiru::data::ContentLoader<BalanceTable>::loadFile("data/table.json");
/// @endcode
///
/// **Versioned content.** For authored content that needs schema migration, wrap the type with
/// `Versioned<T>` or use `MigrationChain<T>` from `JsonBinding.hpp` independently. ContentLoader
/// itself is stateless and has no notion of versions — it is a thin static dispatcher.
///
/// **Opt-in schema validation.** `loadFileValidated / loadJsonValidated / loadStringValidated`
/// run a `mitiru::data::Schema` check before binding. Useful for AI-generated or external JSON
/// content where structural verification is required prior to deserialization.
///
/// @note Hot-path discipline: do NOT call from per-frame code. Intended for boot-time loads,
///       scene transitions, and editor tooling. All entry points allocate via nlohmann/json.

#include <string>

#include <mitiru/data/Json.hpp>
#include <mitiru/data/JsonBinding.hpp>
#include <mitiru/data/SchemaValidator.hpp>

namespace mitiru::data {

/// @brief Stateless loader for typed content (balance tables, dialogue scripts, level data, etc.).
///
/// All methods are static — the class carries no state.
///
/// Usage:
/// @code
/// struct BalanceRow { std::string name; int cost; float winRate; };
/// NLOHMANN_DEFINE_TYPE_INTRUSIVE(BalanceRow, name, cost, winRate)
///
/// auto r = ContentLoader<BalanceRow>::loadFile("data/balance.json");
/// if (r.ok()) { process(*r.value); }
///
/// auto rows = ContentLoader<std::vector<BalanceRow>>::loadFile("data/units.json");
/// @endcode
template <typename T>
class ContentLoader {
public:
    ContentLoader()  = default;
    ~ContentLoader() = default;

    /// @brief Load typed content from a file path.
    ///
    /// Returns an error result if the file cannot be opened, is not valid JSON,
    /// or does not match the schema expected by T's from_json.
    ///
    /// @param path Absolute or relative path to a UTF-8 JSON file.
    /// @return FromJsonResult<T> — check ok() before accessing value.
    [[nodiscard]] static FromJsonResult<T> loadFile(const std::string& path)
    {
        const auto json = loadJsonFile(path);
        if (!json) {
            return FromJsonResult<T>{ std::nullopt, "failed to open or parse: " + path };
        }
        return fromJsonResult<T>(*json);
    }

    /// @brief Load typed content from an already-parsed in-memory JSON object.
    ///
    /// @param json A valid nlohmann::json value.
    /// @return FromJsonResult<T> — check ok() before accessing value.
    [[nodiscard]] static FromJsonResult<T> loadJson(const Json& json)
    {
        return fromJsonResult<T>(json);
    }

    /// @brief Load typed content from a raw JSON string.
    ///
    /// Parses the string first; returns an error result if parsing or
    /// deserialization fails.
    ///
    /// @param s Raw UTF-8 JSON string.
    /// @return FromJsonResult<T> — check ok() before accessing value.
    [[nodiscard]] static FromJsonResult<T> loadString(const std::string& s)
    {
        try {
            return loadJson(Json::parse(s));
        } catch (const std::exception& e) {
            return FromJsonResult<T>{ std::nullopt, e.what() };
        }
    }

    /// @brief Load typed content from a file path with schema validation.
    ///
    /// Runs `mitiru::data::SchemaValidator` against the parsed JSON before binding to T.
    /// On validation failure returns an error result with the concatenated errors,
    /// short-circuiting the bind step.
    ///
    /// @param path   Absolute or relative path to a UTF-8 JSON file.
    /// @param schema Schema definition to validate against.
    /// @return FromJsonResult<T> — check ok() before accessing value.
    [[nodiscard]] static FromJsonResult<T> loadFileValidated(
        const std::string& path, const Schema& schema)
    {
        const auto json = loadJsonFile(path);
        if (!json) {
            return FromJsonResult<T>{ std::nullopt, "failed to open or parse: " + path };
        }
        return loadJsonValidated(*json, schema);
    }

    /// @brief Load typed content from an in-memory JSON object with schema validation.
    ///
    /// @param json   A valid nlohmann::json value.
    /// @param schema Schema definition to validate against.
    /// @return FromJsonResult<T> — check ok() before accessing value.
    [[nodiscard]] static FromJsonResult<T> loadJsonValidated(
        const Json& json, const Schema& schema)
    {
        const auto err = runSchemaCheck(json, schema);
        if (!err.empty()) {
            return FromJsonResult<T>{ std::nullopt, err };
        }
        return loadJson(json);
    }

    /// @brief Load typed content from a raw JSON string with schema validation.
    ///
    /// Parses the string first, then runs schema validation prior to binding.
    ///
    /// @param s      Raw UTF-8 JSON string.
    /// @param schema Schema definition to validate against.
    /// @return FromJsonResult<T> — check ok() before accessing value.
    [[nodiscard]] static FromJsonResult<T> loadStringValidated(
        const std::string& s, const Schema& schema)
    {
        Json parsed;
        try {
            parsed = Json::parse(s);
        } catch (const std::exception& e) {
            return FromJsonResult<T>{ std::nullopt, e.what() };
        }
        return loadJsonValidated(parsed, schema);
    }

private:
    /// @brief Run schema validation; return empty string on success, error message otherwise.
    [[nodiscard]] static std::string runSchemaCheck(const Json& json, const Schema& schema)
    {
        SchemaValidator validator;
        validator.registerSchema(schema);
        const auto result = validator.validate(schema.name, json.dump());
        if (result.valid) {
            return {};
        }
        std::string msg = "schema validation failed:";
        for (const auto& e : result.errors) {
            msg += " ";
            msg += e;
            msg += ";";
        }
        return msg;
    }
};

} // namespace mitiru::data
