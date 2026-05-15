#pragma once

/// @file BridgeViewPush.hpp
/// @brief Thin adapter that routes key-value state and one-shot events to the
///        `view.*` push channel defined in docs/BRIDGE_API_CONTRACT.md.
///
/// **Motivation.**
/// Each bridge subsystem (HUD, dialogue, transition, …) shares the naming
/// convention  `view.<subsystem>.<key>`.  Without a shared helper every bridge
/// must manually concatenate the prefix — a maintenance hazard when the prefix
/// spec changes.  `BridgeViewPush` encodes the prefix once per instance and
/// provides a minimal `set` / `emit` surface.
///
/// **Design.**
/// - `StateStore` is NOT included here.  Callers inject two `std::function`
///   sinks so the helper works in any test or host environment without CEF.
/// - The full channel key `"view.<subsystem>.<key>"` is assembled on each
///   `set`/`emit` call.  One `std::string` allocation per call is acceptable
///   for non-hot-path bridge traffic; if a subsystem needs zero-alloc dispatch
///   it should use `StateStore` directly in its hot path.
/// - `m_keyPrefix` (`"view.<subsystem>."`) is constructed once in the ctor so
///   the repeated prefix portion is never recomputed.
///
/// **Usage:**
/// ```cpp
/// // Wiring up to a real StateStore (no nlohmann/json needed here):
/// BridgeViewPush hud(
///     "hud",
///     [&store](std::string_view k, std::string_view v)
///         { store.set(k, store.json::parse(v)); },   // or a typed helper
///     [&store](std::string_view k, std::string_view v)
///         { store.emit(k, nlohmann::json::parse(v)); }
/// );
///
/// hud.set("hp", "80");          // → store.set("view.hud.hp", …)
/// hud.emit("damage", "{\"x\":1}"); // → store.emit("view.hud.damage", …)
/// ```
///
/// The recommended glue (pre-parsed JSON variant) is shown in
/// docs/BRIDGE_API_CONTRACT.md §3.

#include <functional>
#include <string>
#include <string_view>

namespace mitiru::bridge
{

/// @brief Routes `set`/`emit` calls to the canonical `view.<sub>.<key>` channel.
///
/// Thread-safety: The sinks themselves must be thread-safe if called from
/// multiple threads; `BridgeViewPush` adds no synchronization of its own.
class BridgeViewPush
{
public:
    /// Sink called by `set(key, jsonValue)`.
    /// @param key       Full channel key, e.g. `"view.hud.hp"`.
    /// @param jsonValue JSON-encoded value string, e.g. `"80"` or `"\"red\""`.
    using SetSink  = std::function<void(std::string_view key,
                                        std::string_view jsonValue)>;

    /// Sink called by `emit(key, jsonPayload)`.
    /// @param key         Full channel key, e.g. `"view.hud.damage"`.
    /// @param jsonPayload JSON-encoded payload string, e.g. `"{\"x\":1}"`.
    using EmitSink = std::function<void(std::string_view key,
                                        std::string_view jsonPayload)>;

    /// @brief Construct with a subsystem name and the two push sinks.
    ///
    /// @param subsystem  Short subsystem identifier, e.g. `"hud"`, `"dialog"`,
    ///                   `"transition"`.  An empty string is accepted and yields
    ///                   keys of the form `"view..<key>"`.
    /// @param setSink    Invoked by `set()`.  Must remain valid for the lifetime
    ///                   of this object.
    /// @param emitSink   Invoked by `emit()`.  Must remain valid for the
    ///                   lifetime of this object.
    BridgeViewPush(std::string    subsystem,
                   SetSink        setSink,
                   EmitSink       emitSink)
        : m_subsystem(std::move(subsystem))
        , m_keyPrefix("view." + m_subsystem + ".")
        , m_setSink(std::move(setSink))
        , m_emitSink(std::move(emitSink))
    {}

    // Non-copyable; sinks are move-only-friendly but copying the bound
    // lambdas can silently duplicate captured state.  Move is fine.
    BridgeViewPush(const BridgeViewPush&)            = delete;
    BridgeViewPush& operator=(const BridgeViewPush&) = delete;
    BridgeViewPush(BridgeViewPush&&)                 = default;
    BridgeViewPush& operator=(BridgeViewPush&&)      = default;

    /// @brief Push a retained key-value state update.
    ///
    /// Calls `setSink("view.<subsystem>.<key>", jsonValue)`.
    ///
    /// @param key       Short key within the subsystem, e.g. `"hp"`.
    /// @param jsonValue Pre-serialised JSON string for the value.
    ///
    /// @note One `std::string` allocation is incurred per call to build the
    ///       full channel key.  Acceptable for bridge traffic; avoid in tight
    ///       loops.
    void set(std::string_view key, std::string_view jsonValue)
    {
        if (m_setSink)
        {
            m_setSink(buildKey(key), jsonValue);
        }
    }

    /// @brief Fire a one-shot event.
    ///
    /// Calls `emitSink("view.<subsystem>.<key>", jsonPayload)`.
    ///
    /// @param key         Short event key, e.g. `"damage"`.
    /// @param jsonPayload Pre-serialised JSON payload string.
    void emit(std::string_view key, std::string_view jsonPayload)
    {
        if (m_emitSink)
        {
            m_emitSink(buildKey(key), jsonPayload);
        }
    }

    /// @brief The subsystem name passed at construction.
    [[nodiscard]] std::string_view subsystem() const noexcept
    {
        return m_subsystem;
    }

    /// @brief The computed key prefix, e.g. `"view.hud."`.
    ///
    /// Exposed primarily for testing and logging; callers should not need it
    /// in production code.
    [[nodiscard]] std::string_view keyPrefix() const noexcept
    {
        return m_keyPrefix;
    }

private:
    /// Prepend `m_keyPrefix` to `key` and return the full channel key.
    [[nodiscard]] std::string buildKey(std::string_view key) const
    {
        std::string full;
        full.reserve(m_keyPrefix.size() + key.size());
        full.append(m_keyPrefix);
        full.append(key);
        return full;
    }

    std::string m_subsystem;   ///< e.g. "hud"
    std::string m_keyPrefix;   ///< e.g. "view.hud." — pre-computed in ctor
    SetSink     m_setSink;
    EmitSink    m_emitSink;
};

} // namespace mitiru::bridge
