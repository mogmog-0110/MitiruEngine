#pragma once

/// @file Cooldown.hpp
/// @brief Rate-limiter / ability cooldown helper.
///
/// A Cooldown starts in the @e ready state (remaining == 0).
/// Call @c trigger() to consume the ready state and start the cooldown window.
/// After @p duration seconds have elapsed, @c ready() returns true again.
///
/// Usage example:
/// @code
///   mitiru::time::Cooldown fireCooldown{0.25f};
///
///   // in update loop:
///   fireCooldown.tick(dt);
///   if (inputFire && fireCooldown.ready()) {
///       fireCooldown.trigger();
///       spawnBullet();
///   }
/// @endcode
///
/// Thread-safety: NOT thread-safe. Tick on the same thread as reads.

namespace mitiru::time {

/// Rate-limiter that starts ready and re-arms after a fixed duration.
class Cooldown {
public:
    /// Construct with the given cooldown @p duration (seconds). Must be > 0.
    explicit Cooldown(float duration) noexcept
        : m_remaining(0.0f), m_duration(duration) {}

    /// Advance the cooldown by @p dt seconds.
    void tick(float dt) noexcept {
        if (m_remaining <= 0.0f) { return; }
        m_remaining -= dt;
        if (m_remaining < 0.0f) { m_remaining = 0.0f; }
    }

    /// Returns true when the cooldown has elapsed (or was never triggered).
    [[nodiscard]] bool ready() const noexcept {
        return m_remaining <= 0.0f;
    }

    /// Start the cooldown window. Typically called immediately after
    /// confirming @c ready(). Calling @c trigger() while not ready resets
    /// the window (refresh behaviour — intentional for most use cases).
    void trigger() noexcept {
        m_remaining = m_duration;
    }

    /// Seconds until the cooldown re-arms. Returns 0 when already ready.
    [[nodiscard]] float remaining() const noexcept {
        return m_remaining > 0.0f ? m_remaining : 0.0f;
    }

private:
    float m_remaining;
    float m_duration;
};

} // namespace mitiru::time
