#pragma once

#include <vector>
#include <cstddef>

#include "mitiru/time/detail/SmallFunction.hpp"
#include "mitiru/debug/TracyZones.hpp"

/// @file Sequence.hpp
/// @brief Chainable timeline for ordered waits and callbacks.
///
/// Steps are added at construction time (heap allocation is fine there).
/// @c tick() advances the timeline without allocating — it only moves a
/// cursor and subtracts from an accumulator.
///
/// Callbacks are stored using @c mitiru::time::detail::SmallFunction — a
/// 48-byte SBO type-erased callable. Lambdas with captures up to 48 bytes
/// are stored inline (no heap allocation per action step).
///
/// Usage example:
/// @code
///   mitiru::time::Sequence seq;
///   seq.wait(1.0f)
///      .action([&]{ playSound("sizzle"); })
///      .wait(2.0f)
///      .action([&]{ showText("Done!"); });
///
///   // in update loop:
///   seq.tick(dt);
///   if (seq.done()) { /* sequence finished */ }
/// @endcode
///
/// Thread-safety: NOT thread-safe. Tick on the same thread as construction.

namespace mitiru::time {

/// Ordered sequence of timed waits and instant callbacks.
/// Waits and actions may be freely interleaved. An action step fires
/// immediately and advances to the next step within the same @c tick() call.
class Sequence {
public:
    Sequence() = default;

    /// Append a wait step of @p seconds duration. Returns *this for chaining.
    Sequence& wait(float seconds) {
        m_steps.push_back(Step{seconds, detail::SmallFunction{}});
        return *this;
    }

    /// Append an action step (zero-duration callback). Returns *this for
    /// chaining. The callback is fired during @c tick() when the cursor
    /// reaches this step — no allocation occurs at that point for captures
    /// up to 48 bytes.
    ///
    /// @tparam F  Any callable with signature @c void().
    template <typename F>
    Sequence& action(F&& fn) {
        m_steps.push_back(Step{0.0f, detail::SmallFunction{std::forward<F>(fn)}});
        return *this;
    }

    /// Advance the sequence by @p dt seconds.
    /// tick() is allocation-free: only the cursor and accumulator are mutated.
    void tick(float dt) {
        if (done()) { return; }

        m_acc += dt;

        while (m_cursor < m_steps.size()) {
            Step& step = m_steps[m_cursor];

            if (step.action_) {
                // Action step: fire and advance immediately (no time consumed)
                {
                    MITIRU_ZONE_NAMED("Sequence::action");
                    step.action_();
                }
                ++m_cursor;
                continue;
            }

            // Wait step: consume accumulated time
            if (m_acc >= step.wait_) {
                m_acc -= step.wait_;
                ++m_cursor;
            } else {
                break;
            }
        }
    }

    /// Returns true after the last step has been processed.
    [[nodiscard]] bool done() const noexcept {
        return m_cursor >= m_steps.size();
    }

    /// Restart from the first step. Accumulated time is discarded.
    void reset() noexcept {
        m_cursor = 0;
        m_acc    = 0.0f;
    }

private:
    struct Step {
        float                    wait_;    // > 0 for wait steps, 0 for action steps
        detail::SmallFunction    action_;  // empty for wait steps
    };

    std::vector<Step> m_steps;
    std::size_t       m_cursor = 0;
    float             m_acc    = 0.0f;
};

} // namespace mitiru::time
