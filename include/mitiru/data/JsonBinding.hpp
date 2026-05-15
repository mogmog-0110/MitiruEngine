#pragma once

/// @file JsonBinding.hpp
/// @brief Type-driven C++ struct ↔ JSON binding (thin wrappers over nlohmann/json).
///
/// **Purpose.** Save/load schema (§5) and data-driven content authoring (§9)
/// both need a single answer for "how does a C++ struct become JSON, and back?".
/// This header provides the engine-side conventions on top of nlohmann/json's
/// existing serialization mechanism. It is NOT a new serialization library.
///
/// **User-side type opt-in.** Define `to_json` / `from_json` for your type via
/// nlohmann's standard macros (already a dependency):
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
/// The `NON_INTRUSIVE` form is declared outside the struct (as above).
/// Inside a class body, use `NLOHMANN_DEFINE_TYPE_INTRUSIVE(T, fields...)`.
/// See nlohmann/json docs for details.
///
/// **Versioning.** Save data and authored content both benefit from a schema
/// version field. `Versioned<T>` and `MigrationChain<T>` codify the conventions:
///   - serialized layout: `{ "version": N, "data": <T> }`
///   - `MigrationChain<T>` walks legacy versions up to the current one
///
/// @note Hot-path discipline: `toJson` / `fromJson` allocate via nlohmann/json
///       internally. Do NOT call from per-frame code; intended for save points,
///       boot-time content loads, and editor tooling.

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <mitiru/data/Json.hpp>

namespace mitiru::data {

// ---------------------------------------------------------------------------
// Basic toJson / fromJson
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
// MigrationChain — walks legacy versions up to the current one
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
    /// @return `*this` so callers can chain `addStep(...).addStep(...)`. The
    ///         return value is for convenience only — using it purely for
    ///         side effects (e.g. `chain.addStep(1, 2, fn);`) is also fine.
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
