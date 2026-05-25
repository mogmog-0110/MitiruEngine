#pragma once

/// @file Style2D.hpp
/// @brief CSS ライクな宣言的 2D 描画データ構造

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <variant>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>

namespace mitiru::render
{

// ── Color ヘルパー ────────────────────────────────────────

/// @brief よく使う色の静的 factory メソッド (sgc::Colorf をラップ)
struct Color
{
    /// @brief 0xRRGGBB hex literal を Colorf に変換する
    static constexpr sgc::Colorf hex(uint32_t rgb)
    {
        return sgc::Colorf::fromRGBA8(
            static_cast<uint8_t>((rgb >> 16) & 0xFF),
            static_cast<uint8_t>((rgb >> 8) & 0xFF),
            static_cast<uint8_t>(rgb & 0xFF));
    }

    static constexpr sgc::Colorf rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        return sgc::Colorf::fromRGBA8(r, g, b);
    }

    static constexpr sgc::Colorf rgba(uint8_t r, uint8_t g, uint8_t b, float a)
    {
        constexpr float inv = 1.0f / 255.0f;
        return {static_cast<float>(r) * inv,
                static_cast<float>(g) * inv,
                static_cast<float>(b) * inv, a};
    }

    static constexpr sgc::Colorf rgba(float r, float g, float b, float a)
    {
        return {r, g, b, a};
    }

    static constexpr sgc::Colorf white() { return {1, 1, 1, 1}; }
    static constexpr sgc::Colorf black() { return {0, 0, 0, 1}; }
    static constexpr sgc::Colorf transparent() { return {0, 0, 0, 0}; }
};

// ── Gradient ────────────────────────────────────────────

struct ColorStop
{
    float offset = 0;
    sgc::Colorf color{1, 1, 1, 1};
};

/// @brief Fill も兼ねる Gradient (Type::Solid = 単色)
struct Gradient
{
    enum class Type { Solid, Linear, Radial };

    Type type = Type::Solid;
    sgc::Colorf solidColor{1, 1, 1, 1};
    float angle = 0;                    // degree、Linear 用
    sgc::Vec2f center{0.5f, 0.5f};      // Radial 用
    float radius = 0.5f;                // Radial 用
    std::array<ColorStop, 8> stops{};
    uint8_t stopCount = 0;

    // ── Factory メソッド ──

    static Gradient solid(sgc::Colorf c)
    {
        Gradient g;
        g.type = Type::Solid;
        g.solidColor = c;
        return g;
    }

    static Gradient linear(float angleDeg, std::initializer_list<ColorStop> stopList)
    {
        Gradient g;
        g.type = Type::Linear;
        g.angle = angleDeg;
        g.stopCount = static_cast<uint8_t>(
            std::min(stopList.size(), std::size_t{8}));
        uint8_t i = 0;
        for (const auto& s : stopList)
        {
            if (i >= 8) break;
            g.stops[i++] = s;
        }
        return g;
    }

    static Gradient radial(sgc::Vec2f ctr, float rad,
                           std::initializer_list<ColorStop> stopList)
    {
        Gradient g;
        g.type = Type::Radial;
        g.center = ctr;
        g.radius = rad;
        g.stopCount = static_cast<uint8_t>(
            std::min(stopList.size(), std::size_t{8}));
        uint8_t i = 0;
        for (const auto& s : stopList)
        {
            if (i >= 8) break;
            g.stops[i++] = s;
        }
        return g;
    }

    /// @brief Colorf からの暗黙変換
    Gradient(sgc::Colorf c) // NOLINT(google-explicit-constructor)
        : type(Type::Solid), solidColor(c) {}

    Gradient() = default;
};

// ── Fill (Gradient の alias、Colorf の暗黙変換に対応) ─

using Fill = Gradient;

// ── Shadow ──────────────────────────────────────────────

struct Shadow
{
    float x = 0;
    float y = 0;
    float blur = 0;
    sgc::Colorf color{0, 0, 0, 0};
};

// ── Corners ─────────────────────────────────────────────

struct Corners
{
    float tl = 0;
    float tr = 0;
    float br = 0;
    float bl = 0;

    static constexpr Corners all(float r) { return {r, r, r, r}; }
};

// ── Stroke ──────────────────────────────────────────────

struct Stroke
{
    Fill fill{sgc::Colorf{0, 0, 0, 0}};
    float width = 0;
    std::array<float, 4> widths{};  // top, right, bottom, left (任意)
};

// ── Transform ───────────────────────────────────────────

/// @brief 2D transform パラメータ (衝突回避のため StyleTransform と命名)
struct StyleTransform
{
    float rotate = 0;                    // degree
    sgc::Vec2f scale{1.0f, 1.0f};
    sgc::Vec2f translate{0.0f, 0.0f};
    sgc::Vec2f skew{0.0f, 0.0f};
    sgc::Vec2f anchor{0.5f, 0.5f};
};

// ── Style ───────────────────────────────────────────────

struct Style
{
    Fill fill{sgc::Colorf{1, 1, 1, 1}};
    Stroke stroke{};
    Corners corners{};
    Shadow shadow{};
    Shadow innerShadow{};
    float opacity = 1.0f;
    StyleTransform transform{};

    /// @brief 全数値プロパティを線形補間する
    static Style lerp(const Style& a, const Style& b, float t)
    {
        Style out;
        out.fill = lerpFill(a.fill, b.fill, t);
        out.stroke.fill = lerpFill(a.stroke.fill, b.stroke.fill, t);
        out.stroke.width = mix(a.stroke.width, b.stroke.width, t);
        for (int i = 0; i < 4; ++i)
            out.stroke.widths[static_cast<size_t>(i)] =
                mix(a.stroke.widths[static_cast<size_t>(i)],
                    b.stroke.widths[static_cast<size_t>(i)], t);

        out.corners = {mix(a.corners.tl, b.corners.tl, t),
                       mix(a.corners.tr, b.corners.tr, t),
                       mix(a.corners.br, b.corners.br, t),
                       mix(a.corners.bl, b.corners.bl, t)};

        out.shadow = lerpShadow(a.shadow, b.shadow, t);
        out.innerShadow = lerpShadow(a.innerShadow, b.innerShadow, t);
        out.opacity = mix(a.opacity, b.opacity, t);

        out.transform.rotate = mix(a.transform.rotate, b.transform.rotate, t);
        out.transform.scale = lerpVec(a.transform.scale, b.transform.scale, t);
        out.transform.translate = lerpVec(a.transform.translate, b.transform.translate, t);
        out.transform.skew = lerpVec(a.transform.skew, b.transform.skew, t);
        out.transform.anchor = lerpVec(a.transform.anchor, b.transform.anchor, t);

        return out;
    }

    /// @brief Merge: `over` の非デフォルト値のみ上書きする
    static Style merge(const Style& base, const Style& over)
    {
        Style out = base;

        // Fill: デフォルトの白単色でなければ上書きする
        const Fill defaultFill{sgc::Colorf{1, 1, 1, 1}};
        if (over.fill.type != defaultFill.type ||
            !(over.fill.solidColor == defaultFill.solidColor))
        {
            out.fill = over.fill;
        }

        if (over.stroke.width != 0)
        {
            out.stroke = over.stroke;
        }

        if (over.corners.tl != 0 || over.corners.tr != 0 ||
            over.corners.br != 0 || over.corners.bl != 0)
        {
            out.corners = over.corners;
        }

        if (over.shadow.blur != 0 || over.shadow.x != 0 || over.shadow.y != 0)
        {
            out.shadow = over.shadow;
        }

        if (over.innerShadow.blur != 0 || over.innerShadow.x != 0 ||
            over.innerShadow.y != 0)
        {
            out.innerShadow = over.innerShadow;
        }

        if (over.opacity != 1.0f)
        {
            out.opacity = over.opacity;
        }

        if (over.transform.rotate != 0 ||
            over.transform.scale.x != 1 || over.transform.scale.y != 1 ||
            over.transform.translate.x != 0 || over.transform.translate.y != 0 ||
            over.transform.skew.x != 0 || over.transform.skew.y != 0)
        {
            out.transform = over.transform;
        }

        return out;
    }

private:
    static float mix(float a, float b, float t) { return a + (b - a) * t; }

    static sgc::Vec2f lerpVec(sgc::Vec2f a, sgc::Vec2f b, float t)
    {
        return {mix(a.x, b.x, t), mix(a.y, b.y, t)};
    }

    static Shadow lerpShadow(const Shadow& a, const Shadow& b, float t)
    {
        return {mix(a.x, b.x, t),
                mix(a.y, b.y, t),
                mix(a.blur, b.blur, t),
                a.color.lerp(b.color, t)};
    }

    static Fill lerpFill(const Fill& a, const Fill& b, float t)
    {
        // 両方 solid: 単純な color lerp
        if (a.type == Gradient::Type::Solid && b.type == Gradient::Type::Solid)
        {
            return Gradient::solid(a.solidColor.lerp(b.solidColor, t));
        }

        // Gradient stop: 各 stop color をペアごとに lerp する
        Fill out = b;
        uint8_t count = std::max(a.stopCount, b.stopCount);
        out.stopCount = count;
        for (uint8_t i = 0; i < count; ++i)
        {
            const auto& ca = (i < a.stopCount) ? a.stops[i] : a.stops[a.stopCount > 0 ? a.stopCount - 1 : 0];
            const auto& cb = (i < b.stopCount) ? b.stops[i] : b.stops[b.stopCount > 0 ? b.stopCount - 1 : 0];
            out.stops[i].offset = mix(ca.offset, cb.offset, t);
            out.stops[i].color = ca.color.lerp(cb.color, t);
        }
        out.angle = mix(a.angle, b.angle, t);
        out.center = lerpVec(a.center, b.center, t);
        out.radius = mix(a.radius, b.radius, t);
        return out;
    }
};

// ── Edge ────────────────────────────────────────────────

enum class Edge { Top, Right, Bottom, Left };

// ── ZigzagStyle ─────────────────────────────────────────

struct ZigzagStyle
{
    sgc::Colorf color{1, 1, 1, 1};
    float toothSize = 12;
    Edge edge = Edge::Bottom;
};

// ── Shape types ─────────────────────────────────────────

struct ShapeRect    { sgc::Rectf rect; };
struct ShapeCircle  { sgc::Vec2f center; float radius = 0; };
struct ShapeEllipse { sgc::Vec2f center; float rx = 0; float ry = 0; };
struct ShapeTriangle{ sgc::Vec2f p0; sgc::Vec2f p1; sgc::Vec2f p2; };
struct ShapeLine    { sgc::Vec2f from; sgc::Vec2f to; float thickness = 1; };
struct ShapeArc     { sgc::Vec2f center; float radius = 0; float startAngle = 0; float endAngle = 0; };
struct ShapePie     { sgc::Vec2f center; float radius = 0; float startAngle = 0; float endAngle = 0; };
struct ShapeRing    { sgc::Vec2f center; float outer = 0; float inner = 0; };
struct ShapePolygon { std::vector<sgc::Vec2f> points; };

// ── Path (bezier 曲線) ───────────────────────────────

enum class PathCommand : uint8_t { MoveTo, LineTo, QuadTo, CubicTo, Close };

struct PathPoint
{
    PathCommand cmd = PathCommand::MoveTo;
    sgc::Vec2f p0{};     // target (MoveTo, LineTo) または control point 1 (QuadTo, CubicTo)
    sgc::Vec2f p1{};     // control point 2 (CubicTo) または target (QuadTo)
    sgc::Vec2f p2{};     // target point (CubicTo のみ)
};

struct ShapePath
{
    std::vector<PathPoint> commands;

    ShapePath& moveTo(float x, float y)
    {
        commands.push_back({PathCommand::MoveTo, {x, y}, {}, {}});
        return *this;
    }

    ShapePath& lineTo(float x, float y)
    {
        commands.push_back({PathCommand::LineTo, {x, y}, {}, {}});
        return *this;
    }

    ShapePath& quadTo(float cx, float cy, float x, float y)
    {
        commands.push_back({PathCommand::QuadTo, {cx, cy}, {x, y}, {}});
        return *this;
    }

    ShapePath& cubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y)
    {
        commands.push_back({PathCommand::CubicTo, {c1x, c1y}, {c2x, c2y}, {x, y}});
        return *this;
    }

    ShapePath& close()
    {
        commands.push_back({PathCommand::Close, {}, {}, {}});
        return *this;
    }
};

using ShapeData = std::variant<
    ShapeRect, ShapeCircle, ShapeEllipse, ShapeTriangle,
    ShapeLine, ShapeArc, ShapePie, ShapeRing, ShapePolygon,
    ShapePath>;

} // namespace mitiru::render
