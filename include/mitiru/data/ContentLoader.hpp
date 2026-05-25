#pragma once

/// @file ContentLoader.hpp
/// @brief stateless な typed content loader — JSON file または in-memory JSON を 1 回の呼び出しで C++ struct へ。
///
/// **目的。** §9 data-driven authoring: balance table、dialogue script、level data、
/// その他あらゆる read-only な authored content を JSON で宣言し、起動時または
/// scene-load 時に typed な C++ struct として load できる。
///
/// **利用側の type opt-in。** nlohmann の serialization マクロで自分の struct を register する:
///
/// @code
/// struct BalanceRow { std::string name; int cost; float winRate; };
/// NLOHMANN_DEFINE_TYPE_INTRUSIVE(BalanceRow, name, cost, winRate)
/// @endcode
///
/// あとは 1 行で load する:
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
/// **Versioned content。** schema migration が必要な authored content には、type を
/// `Versioned<T>` で wrap するか、`JsonBinding.hpp` の `MigrationChain<T>` を独立して
/// 使う。ContentLoader 自体は stateless で version の概念を持たない — 薄い static
/// dispatcher である。
///
/// **Opt-in schema validation。** `loadFileValidated / loadJsonValidated / loadStringValidated`
/// は bind 前に `mitiru::data::Schema` check を走らせる。deserialize 前に構造検証が
/// 必要な AI 生成 / 外部 JSON content に有用。
///
/// @note Hot-path 規律: per-frame code から呼ばないこと。boot 時の load、
///       scene 遷移、editor tooling 向け。全 entry point は nlohmann/json 経由で allocate する。

#include <string>

#include <mitiru/data/Json.hpp>
#include <mitiru/data/JsonBinding.hpp>
#include <mitiru/data/SchemaValidator.hpp>

namespace mitiru::data {

/// @brief typed content (balance table、dialogue script、level data 等) の stateless loader。
///
/// 全 method が static — class は state を持たない。
///
/// 使い方:
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

    /// @brief file path から typed content を load する。
    ///
    /// file を開けない、valid な JSON でない、または T の from_json が期待する
    /// schema に一致しない場合は error result を返す。
    ///
    /// @param path UTF-8 JSON file への絶対 / 相対 path。
    /// @return FromJsonResult<T> — value にアクセスする前に ok() を確認すること。
    [[nodiscard]] static FromJsonResult<T> loadFile(const std::string& path)
    {
        const auto json = loadJsonFile(path);
        if (!json) {
            return FromJsonResult<T>{ std::nullopt, "failed to open or parse: " + path };
        }
        return fromJsonResult<T>(*json);
    }

    /// @brief parse 済みの in-memory JSON object から typed content を load する。
    ///
    /// @param json valid な nlohmann::json value。
    /// @return FromJsonResult<T> — value にアクセスする前に ok() を確認すること。
    [[nodiscard]] static FromJsonResult<T> loadJson(const Json& json)
    {
        return fromJsonResult<T>(json);
    }

    /// @brief 生の JSON 文字列から typed content を load する。
    ///
    /// 先に文字列を parse する。parse または deserialize に失敗したら
    /// error result を返す。
    ///
    /// @param s 生 UTF-8 の JSON 文字列。
    /// @return FromJsonResult<T> — value にアクセスする前に ok() を確認すること。
    [[nodiscard]] static FromJsonResult<T> loadString(const std::string& s)
    {
        try {
            return loadJson(Json::parse(s));
        } catch (const std::exception& e) {
            return FromJsonResult<T>{ std::nullopt, e.what() };
        }
    }

    /// @brief schema validation 付きで file path から typed content を load する。
    ///
    /// T へ bind する前に、parse した JSON に対して `mitiru::data::SchemaValidator`
    /// を走らせる。validation 失敗時は連結した error を持つ error result を返し、
    /// bind step を short-circuit する。
    ///
    /// @param path   UTF-8 JSON file への絶対 / 相対 path。
    /// @param schema 検証に使う Schema 定義。
    /// @return FromJsonResult<T> — value にアクセスする前に ok() を確認すること。
    [[nodiscard]] static FromJsonResult<T> loadFileValidated(
        const std::string& path, const Schema& schema)
    {
        const auto json = loadJsonFile(path);
        if (!json) {
            return FromJsonResult<T>{ std::nullopt, "failed to open or parse: " + path };
        }
        return loadJsonValidated(*json, schema);
    }

    /// @brief schema validation 付きで in-memory JSON object から typed content を load する。
    ///
    /// @param json   valid な nlohmann::json value。
    /// @param schema 検証に使う Schema 定義。
    /// @return FromJsonResult<T> — value にアクセスする前に ok() を確認すること。
    [[nodiscard]] static FromJsonResult<T> loadJsonValidated(
        const Json& json, const Schema& schema)
    {
        const auto err = runSchemaCheck(json, schema);
        if (!err.empty()) {
            return FromJsonResult<T>{ std::nullopt, err };
        }
        return loadJson(json);
    }

    /// @brief schema validation 付きで生の JSON 文字列から typed content を load する。
    ///
    /// 先に文字列を parse し、bind の前に schema validation を走らせる。
    ///
    /// @param s      生 UTF-8 の JSON 文字列。
    /// @param schema 検証に使う Schema 定義。
    /// @return FromJsonResult<T> — value にアクセスする前に ok() を確認すること。
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
    /// @brief schema validation を走らせる。成功時は空文字列、それ以外は error メッセージを返す。
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
