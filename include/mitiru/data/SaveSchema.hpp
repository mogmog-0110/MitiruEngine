#pragma once

/// @file SaveSchema.hpp
/// @brief Typed save-data schema holder: versioning + migration in one place.
///
/// **Purpose.** Every save-data type that goes through `mitiru::cef::SaveStore`
/// needs a consistent answer for "what version is this struct, and how do I
/// read an older one?". `SaveSchema<T>` combines a current-version stamp with a
/// `MigrationChain<T>`, and exposes a compact serialize/deserialize surface so
/// consumers never hand-write `nlohmann::json` construction.
///
/// **Opt-in.** The underlying struct must be registered with nlohmann via:
/// @code
/// NLOHMANN_DEFINE_TYPE_INTRUSIVE(MyStruct, field1, field2, ...)
/// // or NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE for types you cannot modify
/// @endcode
///
/// **Typical usage**
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
/// @note Hot-path discipline: JSON serialization allocates internally.
///       Call only at save/load points, not per-frame.

#include <string>

#include <mitiru/data/JsonBinding.hpp>

namespace mitiru::data {

/// @brief Save-data schema holder for type T.
///
/// Owns the current schema version integer and a `MigrationChain<T>` that
/// upgrades older blobs transparently. Default copy and move semantics apply
/// (the migration chain holds `std::function` objects which are copyable).
///
/// @tparam T  Save-data struct. Must satisfy nlohmann's `to_json`/`from_json`
///            interface (e.g. via `NLOHMANN_DEFINE_TYPE_INTRUSIVE`).
template <typename T>
class SaveSchema
{
public:
    /// @brief Construct with the current schema version.
    /// @param currentVersion  Version number embedded in every new blob.
    ///                        Migration steps must bring any older version up
    ///                        to this number.
    explicit SaveSchema(int currentVersion) noexcept
        : m_currentVersion(currentVersion)
    {}

    // Default copy/move are intentional — MigrationChain is copyable.
    SaveSchema(const SaveSchema&)            = default;
    SaveSchema& operator=(const SaveSchema&) = default;
    SaveSchema(SaveSchema&&)                 = default;
    SaveSchema& operator=(SaveSchema&&)      = default;
    ~SaveSchema()                            = default;

    // -----------------------------------------------------------------------
    // Accessors
    // -----------------------------------------------------------------------

    /// @brief Current schema version (set at construction).
    [[nodiscard]] int currentVersion() const noexcept { return m_currentVersion; }

    /// @brief Mutable access to the migration chain.
    ///
    /// Call `migrations().addStep(from, to, fn)` after construction to
    /// register upgrade steps for legacy blobs.
    [[nodiscard]] MigrationChain<T>& migrations() noexcept { return m_migrations; }

    /// @brief Const access to the migration chain.
    [[nodiscard]] const MigrationChain<T>& migrations() const noexcept { return m_migrations; }

    // -----------------------------------------------------------------------
    // Serialization
    // -----------------------------------------------------------------------

    /// @brief Serialize value into a versioned JSON envelope at the current version.
    ///
    /// Output layout: `{ "version": N, "data": <T> }`
    [[nodiscard]] Json toJson(const T& value) const
    {
        return toJsonVersioned(value, m_currentVersion);
    }

    /// @brief Serialize value into a compact JSON string (no indentation).
    ///
    /// Suitable for passing directly to `SaveStore`'s `payload` field.
    [[nodiscard]] std::string toJsonString(const T& value) const
    {
        return toJson(value).dump();
    }

    // -----------------------------------------------------------------------
    // Deserialization
    // -----------------------------------------------------------------------

    /// @brief Deserialize a versioned envelope, applying migrations if needed.
    ///
    /// If `json["version"]` equals `currentVersion()`, deserializes directly.
    /// Otherwise walks registered migration steps until the current version is
    /// reached, then deserializes. Returns an error if any step is missing.
    ///
    /// @param json  `{ "version": N, "data": ... }` envelope (e.g. from SaveStore).
    [[nodiscard]] FromJsonResult<T> fromJson(const Json& json) const
    {
        return m_migrations.load(json, m_currentVersion);
    }

    /// @brief Parse a JSON string and deserialize, applying migrations if needed.
    ///
    /// Wraps `fromJson`; additionally catches JSON parse exceptions and
    /// surfaces them as an error in the returned `FromJsonResult<T>`.
    ///
    /// @param s  Raw JSON string (e.g. directly from SaveStore read blob).
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
