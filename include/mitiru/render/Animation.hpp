#pragma once

#include <mitiru/render/Style2D.hpp>
#include <mitiru/render/Easing.hpp>

#include <vector>
#include <algorithm>
#include <cmath>

namespace mitiru::render {

// --- ループモード ---

enum class Loop { Once, Infinite, Count };

struct LoopMode {
    Loop type = Loop::Once;
    int count = 1;

    static LoopMode once() { return {Loop::Once, 1}; }
    static LoopMode infinite() { return {Loop::Infinite, 0}; }
    static LoopMode times(int n) { return {Loop::Count, n}; }
};

// --- 再生方向 ---

enum class Direction { Normal, Reverse, Alternate, AlternateReverse };

// --- CSS transition 相当 ---

struct Transition {
    float duration = 0.3f;
    Easing easing = Easing::ease();
    float delay = 0.0f;

    void target(const Style& newTarget) {
        if (!m_initialized) {
            m_current = newTarget;
            m_from = newTarget;
            m_to = newTarget;
            m_initialized = true;
            m_animating = false;
            return;
        }
        // target が実際に変わった時だけ再スタートする
        m_from = m_current;
        m_to = newTarget;
        m_elapsed = -delay; // 負の elapsed で delay を表現する
        m_animating = true;
    }

    void update(float dt) {
        if (!m_animating) { return; }

        m_elapsed += dt;
        if (m_elapsed < 0.0f) { return; } // まだ delay 中

        const float raw = (duration > 0.0f)
            ? std::clamp(m_elapsed / duration, 0.0f, 1.0f)
            : 1.0f;
        const float t = easing.evaluate(raw);

        m_current = Style::lerp(m_from, m_to, t);

        if (raw >= 1.0f) {
            m_current = m_to;
            m_animating = false;
        }
    }

    [[nodiscard]] const Style& current() const { return m_current; }
    [[nodiscard]] bool isAnimating() const { return m_animating; }

    void reset(const Style& initial) {
        m_current = initial;
        m_from = initial;
        m_to = initial;
        m_elapsed = 0.0f;
        m_animating = false;
        m_initialized = true;
    }

private:
    Style m_current{};
    Style m_from{};
    Style m_to{};
    float m_elapsed = 0.0f;
    bool m_animating = false;
    bool m_initialized = false;
};

// --- CSS @keyframes 相当 ---

struct KeyframeEntry {
    float offset; // 0.0 ～ 1.0
    Style style;
};

struct Keyframes {
    std::vector<KeyframeEntry> frames;
    float duration = 1.0f;
    Easing easing = Easing::linear();
    LoopMode loop = LoopMode::once();
    Direction direction = Direction::Normal;

    [[nodiscard]] Style evaluate(float time) const {
        if (frames.empty()) { return {}; }
        if (frames.size() == 1) { return frames.front().style; }

        const float localT = computeLocalT(time);

        // 前後の keyframe を探す
        // frames は offset でソート済みである前提
        std::size_t upper = 0;
        for (std::size_t i = 0; i < frames.size(); ++i) {
            if (frames[i].offset >= localT) {
                upper = i;
                break;
            }
            upper = i;
        }

        if (upper == 0) { return frames.front().style; }
        if (frames[upper].offset < localT) { return frames.back().style; }

        const auto& a = frames[upper - 1];
        const auto& b = frames[upper];

        const float segRange = b.offset - a.offset;
        const float segT = (segRange > 1e-7f)
            ? std::clamp((localT - a.offset) / segRange, 0.0f, 1.0f)
            : 0.0f;

        const float easedT = easing.evaluate(segT);
        return Style::lerp(a.style, b.style, easedT);
    }

    [[nodiscard]] bool isComplete(float time) const {
        if (loop.type == Loop::Infinite) { return false; }
        const float totalDuration = duration * static_cast<float>(loop.count);
        return time >= totalDuration;
    }

private:
    [[nodiscard]] float computeLocalT(float time) const {
        if (duration <= 0.0f) { return 1.0f; }

        const int totalLoops = loop.count;
        float iterationF = time / duration;
        int iteration = static_cast<int>(std::floor(iterationF));

        // 無限ループ以外はクランプする
        if (loop.type != Loop::Infinite && iteration >= totalLoops) {
            iteration = totalLoops - 1;
            iterationF = static_cast<float>(totalLoops);
        }

        float t = iterationF - static_cast<float>(iteration);
        t = std::clamp(t, 0.0f, 1.0f);

        // 全ループ完了済みなら終端にスナップする
        if (loop.type != Loop::Infinite && time >= duration * static_cast<float>(totalLoops)) {
            t = 1.0f;
            iteration = totalLoops - 1;
        }

        // 再生方向を適用する
        const bool shouldReverse = [&]() {
            switch (direction) {
                case Direction::Normal:           return false;
                case Direction::Reverse:          return true;
                case Direction::Alternate:        return (iteration % 2) != 0;
                case Direction::AlternateReverse: return (iteration % 2) == 0;
            }
            return false;
        }();

        return shouldReverse ? (1.0f - t) : t;
    }
};

} // namespace mitiru::render
