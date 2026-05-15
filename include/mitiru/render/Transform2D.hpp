#pragma once

/// @file Transform2D.hpp
/// @brief 2D affine transform (2x3 matrix) for Screen draw operations.
///
/// Matrix layout (column-major style conceptually, stored as 6 floats):
///
///   | a  c  tx |
///   | b  d  ty |
///   | 0  0  1  |
///
/// Linear part = {a, b, c, d}, translation = {tx, ty}.
/// Point transform: p' = { a*p.x + c*p.y + tx,  b*p.x + d*p.y + ty }
///
/// Composition (this * rhs) applies `rhs` first, then `this` — i.e. the
/// left operand is the OUTER transform (parent).

#include <sgc/math/Vec2.hpp>
#include <sgc/math/Rect.hpp>

#include <algorithm>
#include <cmath>

namespace mitiru::render
{

struct Transform2D
{
    float a  = 1.0f;  ///< linear[0][0]
    float b  = 0.0f;  ///< linear[1][0]
    float c  = 0.0f;  ///< linear[0][1]
    float d  = 1.0f;  ///< linear[1][1]
    float tx = 0.0f;  ///< translate x
    float ty = 0.0f;  ///< translate y

    // ── Factories ──

    [[nodiscard]] static constexpr Transform2D identity() noexcept
    {
        return {};
    }

    [[nodiscard]] static constexpr Transform2D translate(float tx, float ty) noexcept
    {
        return {1.0f, 0.0f, 0.0f, 1.0f, tx, ty};
    }

    [[nodiscard]] static constexpr Transform2D scale(float sx, float sy) noexcept
    {
        return {sx, 0.0f, 0.0f, sy, 0.0f, 0.0f};
    }

    /// @brief Rotation around origin.
    /// @param rad Rotation angle in radians (positive = CCW in y-down screen space = CW visually).
    [[nodiscard]] static Transform2D rotate(float rad) noexcept
    {
        const float cs = std::cos(rad);
        const float sn = std::sin(rad);
        return {cs, sn, -sn, cs, 0.0f, 0.0f};
    }

    /// @brief Rotation around an arbitrary pivot (px, py).
    /// Equivalent to: translate(px, py) * rotate(rad) * translate(-px, -py)
    [[nodiscard]] static Transform2D rotateAround(float rad, float px, float py) noexcept
    {
        const float cs = std::cos(rad);
        const float sn = std::sin(rad);
        return {
            cs, sn, -sn, cs,
            px - cs * px + sn * py,
            py - sn * px - cs * py,
        };
    }

    // ── Composition ──

    /// @brief Matrix multiplication: result = this * rhs.
    /// @details Applies `rhs` first, then `this` (i.e. `this` is the outer/parent).
    [[nodiscard]] constexpr Transform2D operator*(const Transform2D& r) const noexcept
    {
        return Transform2D{
            a * r.a + c * r.b,             // a'
            b * r.a + d * r.b,             // b'
            a * r.c + c * r.d,             // c'
            b * r.c + d * r.d,             // d'
            a * r.tx + c * r.ty + tx,      // tx'
            b * r.tx + d * r.ty + ty,      // ty'
        };
    }

    // ── Application ──

    [[nodiscard]] constexpr sgc::Vec2f apply(const sgc::Vec2f& p) const noexcept
    {
        return sgc::Vec2f{a * p.x + c * p.y + tx, b * p.x + d * p.y + ty};
    }

    [[nodiscard]] constexpr sgc::Vec2f apply(float x, float y) const noexcept
    {
        return sgc::Vec2f{a * x + c * y + tx, b * x + d * y + ty};
    }

    /// @brief Apply to a rect and return its axis-aligned bounding rect.
    /// @note For rotated transforms this returns the AABB, not the rotated quad.
    [[nodiscard]] sgc::Rectf applyBounds(const sgc::Rectf& r) const noexcept
    {
        const auto p0 = apply(r.x(),             r.y());
        const auto p1 = apply(r.x() + r.width(), r.y());
        const auto p2 = apply(r.x() + r.width(), r.y() + r.height());
        const auto p3 = apply(r.x(),             r.y() + r.height());
        const float minX = std::min({p0.x, p1.x, p2.x, p3.x});
        const float minY = std::min({p0.y, p1.y, p2.y, p3.y});
        const float maxX = std::max({p0.x, p1.x, p2.x, p3.x});
        const float maxY = std::max({p0.y, p1.y, p2.y, p3.y});
        return sgc::Rectf{minX, minY, maxX - minX, maxY - minY};
    }

    // ── Queries ──

    /// @brief Average uniform scale factor (geometric mean of the 2D scale).
    /// @details Used to adjust radii/thickness for primitives like circles or lines.
    [[nodiscard]] float avgScale() const noexcept
    {
        const float sx = std::sqrt(a * a + b * b);
        const float sy = std::sqrt(c * c + d * d);
        return 0.5f * (sx + sy);
    }

    [[nodiscard]] float scaleX() const noexcept { return std::sqrt(a * a + b * b); }
    [[nodiscard]] float scaleY() const noexcept { return std::sqrt(c * c + d * d); }

    /// @brief Rotation angle in radians (only valid when scale is uniform).
    [[nodiscard]] float rotation() const noexcept { return std::atan2(b, a); }

    /// @brief Return true if this is (approximately) the identity transform.
    [[nodiscard]] bool isIdentity() const noexcept
    {
        constexpr float kEps = 1e-6f;
        return std::abs(a - 1.0f) < kEps && std::abs(d - 1.0f) < kEps
            && std::abs(b) < kEps && std::abs(c) < kEps
            && std::abs(tx) < kEps && std::abs(ty) < kEps;
    }

    /// @brief Return true if the linear part is the identity (translation-only).
    [[nodiscard]] bool isTranslationOnly() const noexcept
    {
        constexpr float kEps = 1e-6f;
        return std::abs(a - 1.0f) < kEps && std::abs(d - 1.0f) < kEps
            && std::abs(b) < kEps && std::abs(c) < kEps;
    }

    /// @brief Return true if there is any rotation or shear.
    [[nodiscard]] bool hasRotation() const noexcept
    {
        constexpr float kEps = 1e-6f;
        return std::abs(b) > kEps || std::abs(c) > kEps;
    }

    // ── Legacy compat accessors (to ease migration from the old struct) ──
    [[nodiscard]] float translateX() const noexcept { return tx; }
    [[nodiscard]] float translateY() const noexcept { return ty; }
};

} // namespace mitiru::render
