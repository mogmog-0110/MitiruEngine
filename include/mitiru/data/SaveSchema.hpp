#pragma once

/// @file SaveSchema.hpp
/// @brief typed な save-data schema holder: versioning と migration を 1 箇所に。
///
/// **目的。** `mitiru::cef::SaveStore` を通る各 save-data type は「この struct は
/// 何 version で、古いものをどう読むか?」に一貫した答えを必要とする。
/// `SaveSchema<T>` は current-version のスタンプと `MigrationChain<T>` を組み合わせ、
/// コンパクトな serialize/deserialize 面を公開するので、consumer が
/// `nlohmann::json` の構築を手書きする必要がない。
///
/// **Opt-in。** 基になる struct は nlohmann に以下で register されている必要がある:
/// @code
/// NLOHMANN_DEFINE_TYPE_INTRUSIVE(MyStruct, field1, field2, ...)
/// // or NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE for types you cannot modify
/// @endcode
///
/// **典型的な使い方**
/// @code
/// struct PlayerSave { int level = 1; std::string name; };
/// NLOHMANN_DEFINE_TYPE_INTRUSIVE(PlayerSave, level, name)
///
/// // Build the schema once (e.g. as a static or member variable):
/// mitiru::data::SaveSchema<PlayerSave> schema(/*currentVersion=*/2);
/// schema.migrations().addStep(1, 2, [](mitiru::data::Json data) {
///     data["name"] = "unnamed";   // v1 blobs had no name field
///     return data;
/// });
///
/// // Serialize for writing to SaveStore:
/// PlayerSave save{ .level = 5, .name = "Alice" };
/// std::string blob = schema.toJsonString(save);
///
/// // Deserialize raw blob from SaveStore (handles legacy versions):
/// auto result = schema.fromJsonString(blob);
/// if (result.ok()) {
///     const PlayerSave& restored = *result.value;
/// }
/// @endcode
///
/// @note Hot-path 規律: JSON serialization は内部で allocate する。
///       per-frame ではなく save/load の地点でのみ呼ぶこと。

#include <string>

#include <mitiru/data/JsonBinding.hpp>

namespace mitiru::data {

/// @brief type T 向けの save-data schema holder。
///
/// 現在の schema version 整数と、古い blob を透過的に upgrade する
/// `MigrationChain<T>` を保持する。default の copy / move semantics が適用される
/// (migration chain は copyable な `std::function` object を保持する)。
///
/// @tparam T  save-data struct。nlohmann の `to_json`/`from_json` interface を
///            満たすこと (例: `NLOHMANN_DEFINE_TYPE_INTRUSIVE` 経由)。
template <typename T>
class SaveSchema
{
public:
    /// @brief 現在の schema version で construct する。
    /// @param currentVersion  新しい blob すべてに埋め込まれる version 番号。
    ///                        migration step はあらゆる古い version をこの番号
    ///                        まで引き上げなければならない。
    explicit SaveSchema(int currentVersion) noexcept
        : m_currentVersion(currentVersion)
    {}

    // default の copy/move は意図的 — MigrationChain は copyable。
    SaveSchema(const SaveSchema&)            = default;
    SaveSchema& operator=(const SaveSchema&) = default;
    SaveSchema(SaveSchema&&)                 = default;
    SaveSchema& operator=(SaveSchema&&)      = default;
    ~SaveSchema()                            = default;

    // -----------------------------------------------------------------------
    // Accessors
    // -----------------------------------------------------------------------

    /// @brief 現在の schema version (construct 時に設定)。
    [[nodiscard]] int currentVersion() const noexcept { return m_currentVersion; }

    /// @brief migration chain への mutable access。
    ///
    /// construct 後に `migrations().addStep(from, to, fn)` を呼んで、legacy blob
    /// 向けの upgrade step を register する。
    [[nodiscard]] MigrationChain<T>& migrations() noexcept { return m_migrations; }

    /// @brief migration chain への const access。
    [[nodiscard]] const MigrationChain<T>& migrations() const noexcept { return m_migrations; }

    // -----------------------------------------------------------------------
    // Serialization
    // -----------------------------------------------------------------------

    /// @brief value を現在の version の versioned JSON envelope へ serialize する。
    ///
    /// 出力 layout: `{ "version": N, "data": <T> }`
    [[nodiscard]] Json toJson(const T& value) const
    {
        return toJsonVersioned(value, m_currentVersion);
    }

    /// @brief value をコンパクトな JSON 文字列 (インデント無し) へ serialize する。
    ///
    /// `SaveStore` の `payload` field へ直接渡すのに適する。
    [[nodiscard]] std::string toJsonString(const T& value) const
    {
        return toJson(value).dump();
    }

    // -----------------------------------------------------------------------
    // Deserialization
    // -----------------------------------------------------------------------

    /// @brief versioned envelope を deserialize する。必要なら migration を適用する。
    ///
    /// `json["version"]` が `currentVersion()` と等しければ直接 deserialize する。
    /// そうでなければ現在の version に到達するまで register された migration step を
    /// 辿り、その後 deserialize する。step が欠けていれば error を返す。
    ///
    /// @param json  `{ "version": N, "data": ... }` envelope (例: SaveStore から)。
    [[nodiscard]] FromJsonResult<T> fromJson(const Json& json) const
    {
        return m_migrations.load(json, m_currentVersion);
    }

    /// @brief JSON 文字列を parse して deserialize する。必要なら migration を適用する。
    ///
    /// `fromJson` を wrap する。加えて JSON parse 例外を catch し、返り値の
    /// `FromJsonResult<T>` に error として surface する。
    ///
    /// @param s  生の JSON 文字列 (例: SaveStore の read blob から直接)。
    [[nodiscard]] FromJsonResult<T> fromJsonString(const std::string& s) const
    {
        try
        {
            return fromJson(Json::parse(s));
        }
        catch (const std::exception& e)
        {
            return FromJsonResult<T>{ std::nullopt, e.what() };
        }
    }

private:
    int               m_currentVersion = 1;
    MigrationChain<T> m_migrations;
};

} // namespace mitiru::data
