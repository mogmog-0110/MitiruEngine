#pragma once

/// @file JsonBinding.hpp
/// @brief 型駆動の C++ struct ↔ JSON binding (nlohmann/json の薄い wrapper)。
///
/// **目的。** save/load schema (§5) も data 駆動の content authoring (§9) も、
/// 「C++ struct はどう JSON になり、どう戻るか」という単一の答えを必要とする。
/// 本ヘッダは nlohmann/json 既存の serialization 機構の上に engine 側の規約を
/// 提供する。新しい serialization library ではない。
///
/// **ユーザ型の opt-in。** 自分の型には nlohmann 標準マクロ (既に依存済み) で
/// `to_json` / `from_json` を定義する:
///
/// @code
/// struct PlayerStats {
///     int   level    = 1;
///     float health   = 100.0f;
///     std::string name;
/// };
/// NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PlayerStats, level, health, name)
/// @endcode
///
/// `NON_INTRUSIVE` 形式は struct の外で宣言する (上記の通り)。
/// class 本体の内側では `NLOHMANN_DEFINE_TYPE_INTRUSIVE(T, fields...)` を使う。
/// 詳細は nlohmann/json のドキュメントを参照。
///
/// **Versioning。** save data も authored content も schema version field の
/// 恩恵を受ける。`Versioned<T>` と `MigrationChain<T>` が規約を成文化する:
///   - serialized layout: `{ "version": N, "data": <T> }`
///   - `MigrationChain<T>` は旧 version を最新まで辿る
///
/// @note Hot-path 規律: `toJson` / `fromJson` は内部で nlohmann/json 経由の
///       allocation を行う。per-frame code から呼ばないこと。save point、
///       boot 時の content load、editor tooling での使用を想定。

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <mitiru/data/Json.hpp>

namespace mitiru::data {

// ---------------------------------------------------------------------------
// 基本の toJson / fromJson
// ---------------------------------------------------------------------------

/// @brief C++ value を JSON へ変換する
/// @details nlohmann::adl_serializer<T> を経由するため、ユーザ型は
///          NLOHMANN_DEFINE_TYPE_INTRUSIVE / NON_INTRUSIVE で opt-in 可能。
template <typename T>
[[nodiscard]] inline Json toJson(const T& value)
{
    return Json(value);
}

/// @brief JSON を C++ value へ変換する (失敗時 nullopt)
/// @details 型変換例外 (nlohmann::json::exception) は捕捉して nullopt。
///          詳細メッセージが要る場合は `fromJsonResult<T>` を使用。
template <typename T>
[[nodiscard]] inline std::optional<T> fromJson(const Json& json) noexcept
{
    try
    {
        return json.get<T>();
    }
    catch (...)
    {
        return std::nullopt;
    }
}

/// @brief 失敗時にエラーメッセージも返す fromJson
template <typename T>
struct FromJsonResult
{
    std::optional<T> value;       ///< 成功時の値
    std::string      error;       ///< 失敗時のメッセージ (成功時は空)

    [[nodiscard]] bool ok() const noexcept { return value.has_value(); }
    [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
};

/// @brief JSON を C++ value へ変換し、失敗時にエラー詳細を返す
template <typename T>
[[nodiscard]] inline FromJsonResult<T> fromJsonResult(const Json& json)
{
    try
    {
        return FromJsonResult<T>{ json.get<T>(), {} };
    }
    catch (const std::exception& e)
    {
        return FromJsonResult<T>{ std::nullopt, e.what() };
    }
    catch (...)
    {
        return FromJsonResult<T>{ std::nullopt, "unknown exception during from_json" };
    }
}

// ---------------------------------------------------------------------------
// Versioning
// ---------------------------------------------------------------------------

/// @brief Versioned JSON envelope: `{ "version": N, "data": <T> }`
/// @details Save data / authored content の互換性管理のための共通レイアウト。
template <typename T>
[[nodiscard]] inline Json toJsonVersioned(const T& value, int version)
{
    Json out;
    out["version"] = version;
    out["data"]    = toJson(value);
    return out;
}

/// @brief Versioned envelope を読み込む (version 一致時のみ成功)
/// @details version mismatch / フィールド欠落 / 型エラーいずれも error メッセージ付きで返す。
///          version migration が要る場合は `MigrationChain<T>` を使用。
template <typename T>
[[nodiscard]] inline FromJsonResult<T> fromJsonVersioned(const Json& json,
                                                        int expectedVersion)
{
    try
    {
        if (!json.contains("version") || !json.contains("data"))
        {
            return FromJsonResult<T>{ std::nullopt,
                "envelope missing 'version' or 'data' field" };
        }
        const int v = json.at("version").template get<int>();
        if (v != expectedVersion)
        {
            return FromJsonResult<T>{ std::nullopt,
                "version mismatch: expected " + std::to_string(expectedVersion)
                + " got " + std::to_string(v) };
        }
        return FromJsonResult<T>{ json.at("data").template get<T>(), {} };
    }
    catch (const std::exception& e)
    {
        return FromJsonResult<T>{ std::nullopt, e.what() };
    }
}

// ---------------------------------------------------------------------------
// MigrationChain — 旧 version を最新まで辿る
// ---------------------------------------------------------------------------

/// @brief 旧 version の JSON を最新まで段階的に変換するチェーン
/// @details 各 step は「version N の data JSON を受け取り、version N+1 の data JSON を返す」関数。
///          load() は version フィールドを読み、必要な step を順に適用してから
///          C++ struct に変換する。
///
/// @code
/// MigrationChain<PlayerStats> chain;
/// chain.addStep(1, 2, [](Json data){ data["name"] = ""; return data; });
/// chain.addStep(2, 3, [](Json data){ data["health"] = data.value("hp", 100); return data; });
///
/// auto r = chain.load(oldVersionedJson, /*currentVersion=*/3);
/// if (r.ok()) { auto& stats = *r.value; }
/// @endcode
template <typename T>
class MigrationChain
{
public:
    using Migrate = std::function<Json(Json)>;

    /// @brief 1 step を追加 (from → to)
    /// @return `addStep(...).addStep(...)` と chain できるよう `*this` を返す。
    ///         戻り値は利便性のためだけのもので、side effect 目的に
    ///         (例: `chain.addStep(1, 2, fn);`) 使い捨てても構わない。
    MigrationChain& addStep(int fromVersion, int toVersion, Migrate migrate)
    {
        m_steps.push_back(Step{ fromVersion, toVersion, std::move(migrate) });
        return *this;
    }

    /// @brief versioned JSON を読み込み、必要な migration を適用して T を返す
    /// @param json `{ "version": N, "data": ... }` 形式の envelope
    /// @param currentVersion ターゲットとする最新 version
    [[nodiscard]] FromJsonResult<T> load(const Json& json, int currentVersion) const
    {
        try
        {
            if (!json.contains("version") || !json.contains("data"))
            {
                return FromJsonResult<T>{ std::nullopt,
                    "envelope missing 'version' or 'data' field" };
            }

            int v = json.at("version").template get<int>();
            Json data = json.at("data");

            while (v != currentVersion)
            {
                const auto it = std::find_if(m_steps.begin(), m_steps.end(),
                    [v](const Step& s) { return s.from == v; });
                if (it == m_steps.end())
                {
                    return FromJsonResult<T>{ std::nullopt,
                        "no migration step starting from version " + std::to_string(v) };
                }
                data = it->migrate(std::move(data));
                v    = it->to;
            }

            return FromJsonResult<T>{ data.template get<T>(), {} };
        }
        catch (const std::exception& e)
        {
            return FromJsonResult<T>{ std::nullopt, e.what() };
        }
    }

    /// @brief 登録 step 数
    [[nodiscard]] std::size_t stepCount() const noexcept { return m_steps.size(); }

private:
    struct Step
    {
        int     from;
        int     to;
        Migrate migrate;
    };

    std::vector<Step> m_steps;
};

} // namespace mitiru::data
