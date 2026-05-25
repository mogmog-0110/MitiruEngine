#pragma once

#include <algorithm>

/// @file Timer.hpp
/// @brief gameplay 用の一発 countdown timer (調理 timer、ダイアログ待ち等)
///
/// 使用例:
/// @code
///   mitiru::time::Timer t{3.0f};
///   // in update loop:
///   t.tick(dt);
///   if (t.expired()) { /* baking done */ }
///   float fraction = t.progress(); // 0.0 → 1.0
/// @endcode
///
/// スレッド安全性: thread-safe ではない。読み取りと同じスレッドで tick すること。

namespace mitiru::time {

/// 一発の countdown timer。
/// @c expired() が true を返した後、以降の @c tick() 呼び出しは no-op。
/// @c reset() または @c reset(newDuration) で allocation 無しに再利用できる。
class Timer {
public:
    /// 指定した @p duration (秒) で構築する。0 より大きいこと。
    explicit Timer(float duration) noexcept
        : m_remaining(duration), m_duration(duration) {}

    /// timer を @p dt 秒進める。
    /// timer が expire 済みなら no-op。
    void tick(float dt) noexcept {
        if (m_remaining <= 0.0f) { return; }
        m_remaining -= dt;
        if (m_remaining < 0.0f) { m_remaining = 0.0f; }
    }

    /// 蓄積した時間が元の duration に達した、または超えた時 true を返す。
    [[nodiscard]] bool expired() const noexcept {
        return m_remaining <= 0.0f;
    }

    /// expire までの残り秒数。[0, duration] にクランプされる。
    [[nodiscard]] float remaining() const noexcept { return m_remaining; }

    /// [0, 1] に正規化された進捗。
    /// 構築時は 0.0、expire 時は 1.0 を返す。
    [[nodiscard]] float progress() const noexcept {
        if (m_duration <= 0.0f) { return 1.0f; }
        const float elapsed = m_duration - m_remaining;
        return std::clamp(elapsed / m_duration, 0.0f, 1.0f);
    }

    /// 元の duration で再開する。
    void reset() noexcept {
        m_remaining = m_duration;
    }

    /// 新しい duration で再開する (保持している基準 duration も更新する)。
    void reset(float newDuration) noexcept {
        m_duration  = newDuration;
        m_remaining = newDuration;
    }

private:
    float m_remaining;
    float m_duration;
};

} // namespace mitiru::time
