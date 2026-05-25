#pragma once

/// @file Transform2D.hpp
/// @brief Screen の draw 操作向け 2D アフィン変換 (2x3 matrix)。
///
/// matrix のレイアウト (概念上は column-major、6 個の float として保持):
///
///   | a  c  tx |
///   | b  d  ty |
///   | 0  0  1  |
///
/// 線形部 = {a, b, c, d}、平行移動 = {tx, ty}。
/// 点の変換: p' = { a*p.x + c*p.y + tx,  b*p.x + d*p.y + ty }
///
/// 合成 (this * rhs) は先に `rhs`、次に `this` を適用する — つまり左オペランドが
/// 外側 (親) の transform となる。

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
    float tx = 0.0f;  ///< 平行移動 x
    float ty = 0.0f;  ///< 平行移動 y

    // ── ファクトリ ──

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

    /// @brief 原点周りの回転。
    /// @param rad 回転角 (radian)。正値は y-down の screen 空間で CCW = 見た目では CW。
    [[nodiscard]] static Transform2D rotate(float rad) noexcept
    {
        const float cs = std::cos(rad);
        const float sn = std::sin(rad);
        return {cs, sn, -sn, cs, 0.0f, 0.0f};
    }

    /// @brief 任意の pivot (px, py) 周りの回転。
    /// translate(px, py) * rotate(rad) * translate(-px, -py) と等価。
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

    // ── 合成 ──

    /// @brief 行列の乗算: result = this * rhs。
    /// @details 先に `rhs`、次に `this` を適用する (つまり `this` が外側 / 親)。
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

    // ── 適用 ──

    [[nodiscard]] constexpr sgc::Vec2f apply(const sgc::Vec2f& p) const noexcept
    {
        return sgc::Vec2f{a * p.x + c * p.y + tx, b * p.x + d * p.y + ty};
    }

    [[nodiscard]] constexpr sgc::Vec2f apply(float x, float y) const noexcept
    {
        return sgc::Vec2f{a * x + c * y + tx, b * x + d * y + ty};
    }

    /// @brief rect に適用し、その軸並行の bounding rect を返す。
    /// @note 回転を含む transform では回転後の quad ではなく AABB を返す。
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

    // ── クエリ ──

    /// @brief 平均的な等方 scale 係数 (2D scale の幾何平均)。
    /// @details 円や線などの primitive の半径 / 太さを調整するのに使う。
    [[nodiscard]] float avgScale() const noexcept
    {
        const float sx = std::sqrt(a * a + b * b);
        const float sy = std::sqrt(c * c + d * d);
        return 0.5f * (sx + sy);
    }

    [[nodiscard]] float scaleX() const noexcept { return std::sqrt(a * a + b * b); }
    [[nodiscard]] float scaleY() const noexcept { return std::sqrt(c * c + d * d); }

    /// @brief 回転角 (radian)。scale が等方のときのみ有効。
    [[nodiscard]] float rotation() const noexcept { return std::atan2(b, a); }

    /// @brief (ほぼ) 単位変換であれば true を返す。
    [[nodiscard]] bool isIdentity() const noexcept
    {
        constexpr float kEps = 1e-6f;
        return std::abs(a - 1.0f) < kEps && std::abs(d - 1.0f) < kEps
            && std::abs(b) < kEps && std::abs(c) < kEps
            && std::abs(tx) < kEps && std::abs(ty) < kEps;
    }

    /// @brief 線形部が単位行列 (平行移動のみ) であれば true を返す。
    [[nodiscard]] bool isTranslationOnly() const noexcept
    {
        constexpr float kEps = 1e-6f;
        return std::abs(a - 1.0f) < kEps && std::abs(d - 1.0f) < kEps
            && std::abs(b) < kEps && std::abs(c) < kEps;
    }

    /// @brief 回転または剪断 (shear) があれば true を返す。
    [[nodiscard]] bool hasRotation() const noexcept
    {
        constexpr float kEps = 1e-6f;
        return std::abs(b) > kEps || std::abs(c) > kEps;
    }

    // ── 旧構造体からの移行を容易にする legacy 互換 accessor ──
    [[nodiscard]] float translateX() const noexcept { return tx; }
    [[nodiscard]] float translateY() const noexcept { return ty; }
};

} // namespace mitiru::render
