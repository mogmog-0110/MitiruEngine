#pragma once

/// @file Migration.hpp
/// @brief Composable migration ops for SaveSchema<T>::migrations().
///
/// **Purpose.** `MigrationChain<T>::addStep` accepts a `std::function<Json(Json)>`
/// for upgrading legacy save blobs. Consumers usually write the same shapes
/// over and over: "backfill a missing field", "rename a field", "drop a field".
/// This header provides ready-made lambda factories so those common cases stay
/// declarative — and `compose(...)` lets one step do multiple things in order.
///
/// **Typical usage**
/// @code
/// using mitiru::data::Migration;
///
/// SaveSchema<PlayerSave> schema(4);
/// schema.migrations()
///     .addStep(1, 2, Migration::backfillField("unlockedRecipes",
///                                              Json::array({"Cookie"})))
///     .addStep(2, 3, Migration::renameField("score", "totalScore"))
///     .addStep(3, 4, Migration::compose({
///         Migration::removeField("legacyDebug"),
///         Migration::setField("schemaTouched", true),
///     }));
/// @endcode
///
/// All helpers return a `std::function<Json(Json)>` that captures arguments
/// **by value** so the returned op safely outlives the factory call.
///
/// @note `MigrationChain<T>::addStep` returns `*this`, so the fluent chain
///       above works directly. Calling `addStep` line by line (ignoring the
///       return value) is also fine when fluent style would hurt readability.

#include <functional>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include <mitiru/data/JsonBinding.hpp>

namespace mitiru::data {

/// @brief Composable migration helpers usable with `MigrationChain::addStep`.
///
/// Stateless utility class — all members are static factory functions that
/// return ready-made `std::function<Json(Json)>` lambdas. Captures are by
/// value so the resulting op is safe to store in `MigrationChain`.
class Migration
{
public:
    /// @brief Migration op signature: takes a JSON object, returns the upgraded JSON object.
    using Op = std::function<Json(Json)>;

    // -----------------------------------------------------------------------
    // Single-field ops
    // -----------------------------------------------------------------------

    /// @brief Backfill a missing field with @p defaultValue. No-op if already present.
    /// @param field         Field name to inspect.
    /// @param defaultValue  Value to set when the field is absent.
    [[nodiscard]] static inline Op backfillField(std::string field, Json defaultValue)
    {
        return [field = std::move(field), defaultValue = std::move(defaultValue)]
               (Json data) -> Json
        {
            if (!data.contains(field))
            {
                data[field] = defaultValue;
            }
            return data;
        };
    }

    /// @brief Rename a field. No-op if @p oldName is missing.
    /// @param oldName  Source field name (will be erased on success).
    /// @param newName  Destination field name (overwritten if it already exists).
    [[nodiscard]] static inline Op renameField(std::string oldName, std::string newName)
    {
        return [oldName = std::move(oldName), newName = std::move(newName)]
               (Json data) -> Json
        {
            if (data.contains(oldName))
            {
                data[newName] = data[oldName];
                data.erase(oldName);
            }
            return data;
        };
    }

    /// @brief Remove a field. No-op if absent.
    [[nodiscard]] static inline Op removeField(std::string field)
    {
        return [field = std::move(field)](Json data) -> Json
        {
            if (data.contains(field))
            {
                data.erase(field);
            }
            return data;
        };
    }

    /// @brief Set a field unconditionally (overwrites any existing value).
    [[nodiscard]] static inline Op setField(std::string field, Json value)
    {
        return [field = std::move(field), value = std::move(value)]
               (Json data) -> Json
        {
            data[field] = value;
            return data;
        };
    }

    /// @brief Apply a transform to a field's value. No-op if the field is absent.
    /// @param field  Field whose value is passed to @p fn.
    /// @param fn     Pure function returning the new value.
    [[nodiscard]] static inline Op transformField(std::string field,
                                                  std::function<Json(Json)> fn)
    {
        return [field = std::move(field), fn = std::move(fn)]
               (Json data) -> Json
        {
            if (data.contains(field))
            {
                data[field] = fn(data[field]);
            }
            return data;
        };
    }

    // -----------------------------------------------------------------------
    // Composition
    // -----------------------------------------------------------------------

    /// @brief Compose multiple ops left-to-right.
    /// @details Empty list returns the identity op. Each op receives the
    ///          output of the previous one.
    [[nodiscard]] static inline Op compose(std::initializer_list<Op> ops)
    {
        return compose(std::vector<Op>(ops.begin(), ops.end()));
    }

    /// @brief Compose via a runtime-built vector of ops.
    [[nodiscard]] static inline Op compose(std::vector<Op> ops)
    {
        return [ops = std::move(ops)](Json data) -> Json
        {
            for (const auto& op : ops)
            {
                data = op(std::move(data));
            }
            return data;
        };
    }
};

} // namespace mitiru::data
