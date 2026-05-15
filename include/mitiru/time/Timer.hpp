#pragma once

#include <algorithm>

/// @file Timer.hpp
/// @brief One-shot countdown timer for gameplay use (baking timers, dialogue waits, etc.)
///
/// Usage example:
/// @code
///   mitiru::time::Timer t{3.0f};
///   // in update loop:
///   t.tick(dt);
///   if (t.expired()) { /* baking done */ }
///   float fraction = t.progress(); // 0.0 → 1.0
/// @endcode
///
/// Thread-safety: NOT thread-safe. Tick on the same thread as reads.

namespace mitiru::time {

/// One-shot countdown timer.
/// After @c expired() returns true, subsequent @c tick() calls are no-ops.
/// Reset via @c reset() or @c reset(newDuration) to reuse the object without
/// allocation.
class Timer {
public:
    /// Construct with the given @p duration (seconds). Must be > 0.
    explicit Timer(float duration) noexcept
        : m_remaining(duration), m_duration(duration) {}

    /// Advance the timer by @p dt seconds.
    /// No-op once the timer has expired.
    void tick(float dt) noexcept {
        if (m_remaining <= 0.0f) { return; }
        m_remaining -= dt;
        if (m_remaining < 0.0f) { m_remaining = 0.0f; }
    }

    /// Returns true when the accumulated time has reached or exceeded the
    /// original duration.
    [[nodiscard]] bool expired() const noexcept {
        return m_remaining <= 0.0f;
    }

    /// Seconds left until expiry. Clamped to [0, duration].
    [[nodiscard]] float remaining() const noexcept { return m_remaining; }

    /// Normalized progress in [0, 1].
    /// Returns 0.0 at construction, 1.0 when expired.
    [[nodiscard]] float progress() const noexcept {
        if (m_duration <= 0.0f) { return 1.0f; }
        const float elapsed = m_duration - m_remaining;
        return std::clamp(elapsed / m_duration, 0.0f, 1.0f);
    }

    /// Restart with the original duration.
    void reset() noexcept {
        m_remaining = m_duration;
    }

    /// Restart with a new duration (also updates the stored base duration).
    void reset(float newDuration) noexcept {
        m_duration  = newDuration;
        m_remaining = newDuration;
    }

private:
    float m_remaining;
    float m_duration;
};

} // namespace mitiru::time
