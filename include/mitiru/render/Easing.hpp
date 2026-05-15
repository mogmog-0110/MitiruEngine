#pragma once

#include <cmath>
#include <algorithm>

namespace mitiru::render {

struct Easing {
    static Easing linear() {
        return Easing{0.0f, 0.0f, 1.0f, 1.0f, true};
    }

    static Easing ease() {
        return cubicBezier(0.25f, 0.1f, 0.25f, 1.0f);
    }

    static Easing easeIn() {
        return cubicBezier(0.42f, 0.0f, 1.0f, 1.0f);
    }

    static Easing easeOut() {
        return cubicBezier(0.0f, 0.0f, 0.58f, 1.0f);
    }

    static Easing easeInOut() {
        return cubicBezier(0.42f, 0.0f, 0.58f, 1.0f);
    }

    static Easing cubicBezier(float x1, float y1, float x2, float y2) {
        const bool isLin = (std::abs(x1) < 1e-6f && std::abs(y1) < 1e-6f &&
                            std::abs(x2 - 1.0f) < 1e-6f && std::abs(y2 - 1.0f) < 1e-6f);
        return Easing{x1, y1, x2, y2, isLin};
    }

    [[nodiscard]] float evaluate(float t) const {
        t = std::clamp(t, 0.0f, 1.0f);
        if (m_isLinear) { return t; }

        // Newton-Raphson: find s where x(s) = t
        float s = t; // initial guess
        for (int i = 0; i < 8; ++i) {
            const float xs = sampleX(s);
            const float dx = sampleDx(s);
            if (std::abs(dx) < 1e-7f) { break; }
            s -= (xs - t) / dx;
            s = std::clamp(s, 0.0f, 1.0f);
        }

        return sampleY(s);
    }

private:
    float m_x1 = 0.0f;
    float m_y1 = 0.0f;
    float m_x2 = 1.0f;
    float m_y2 = 1.0f;
    bool m_isLinear = true;

    Easing(float x1, float y1, float x2, float y2, bool isLin)
        : m_x1(x1), m_y1(y1), m_x2(x2), m_y2(y2), m_isLinear(isLin) {}

    // B(s) = 3*p1*s*(1-s)^2 + 3*p2*s^2*(1-s) + s^3
    [[nodiscard]] float sampleX(float s) const {
        const float inv = 1.0f - s;
        return 3.0f * m_x1 * s * inv * inv + 3.0f * m_x2 * s * s * inv + s * s * s;
    }

    [[nodiscard]] float sampleY(float s) const {
        const float inv = 1.0f - s;
        return 3.0f * m_y1 * s * inv * inv + 3.0f * m_y2 * s * s * inv + s * s * s;
    }

    // dx/ds derivative for Newton's method
    [[nodiscard]] float sampleDx(float s) const {
        const float inv = 1.0f - s;
        return 3.0f * m_x1 * inv * inv
             + 6.0f * (m_x2 - m_x1) * s * inv
             + 3.0f * (1.0f - m_x2) * s * s;
    }
};

} // namespace mitiru::render
