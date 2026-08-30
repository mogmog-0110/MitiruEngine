#pragma once

/// @file Cooldown.hpp
/// @brief Rate-limiter / アビリティ cooldown ヘルパー。
///
/// Cooldown は @e ready 状態 (remaining == 0) で始まる。
/// @c trigger() を呼ぶと ready 状態を消費し cooldown window を開始する。
/// @p duration 秒が経過すると @c ready() は再び true を返す。
///
/// 使用例:
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
/// スレッド安全性: thread-safe ではない。読み取りと同じスレッドで tick すること。

namespace mitiru::time {

/// ready 状態で始まり、固定 duration 後に再 arm する rate-limiter。
class Cooldown {
public:
    /// 指定した cooldown @p duration (秒) で構築する。0 より大きいこと。
    explicit Cooldown(float duration) noexcept
        : m_remaining(0.0f), m_duration(duration) {}

    /// cooldown を @p dt 秒進める。
    void tick(float dt) noexcept {
        if (m_remaining <= 0.0f) { return; }
        m_remaining -= dt;
        if (m_remaining < 0.0f) { m_remaining = 0.0f; }
    }

    /// cooldown が経過した (または一度も trigger されていない) 時 true を返す。
    [[nodiscard]] bool ready() const noexcept {
        return m_remaining <= 0.0f;
    }

    /// cooldown window を開始する。通常は @c ready() 確認の直後に呼ぶ。
    /// ready でない状態で @c trigger() を呼ぶと window をリセットする
    /// (refresh 挙動。ほとんどの用途で意図的)。
    void trigger() noexcept {
        m_remaining = m_duration;
    }

    /// cooldown が再 arm するまでの秒数。既に ready なら 0 を返す。
    [[nodiscard]] float remaining() const noexcept {
        return m_remaining > 0.0f ? m_remaining : 0.0f;
    }

private:
    float m_remaining;
    float m_duration;
};

} // namespace mitiru::time
