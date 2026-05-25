#pragma once

/// @file StyledShapeRenderer.hpp
/// @brief 非矩形 primitive 用の Style2D 対応 shape renderer
/// @details ShapeRenderer を拡張し、gradient fill / shadow / stroke /
///          transform / opacity を CPU tessellation + vertex color で実現する。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/Style2D.hpp>
#include <mitiru/render/Vertex2D.hpp>

namespace mitiru::render
{

class StyledShapeRenderer
{
public:
    void begin()
    {
        m_vertices.clear();
        m_indices.clear();
        m_recording = true;
    }

    void end() { m_recording = false; }

    // ── Shape 描画メソッド ──────────────────────────────────

    void drawCircle(const ShapeCircle& shape, const Style& style)
    {
        if (!m_recording) return;
        const sgc::Vec2f center = shape.center;
        const float r = shape.radius;
        const sgc::Rectf bounds{center.x - r, center.y - r, r * 2, r * 2};
        const int seg = adaptiveSegments(r);
        drawShapeWithStyle(style, center, [&](const Gradient& fill, float expand) {
            emitCircleFan(center, r + expand, fill, bounds, seg);
        });
    }

    void drawEllipse(const ShapeEllipse& shape, const Style& style)
    {
        if (!m_recording) return;
        const sgc::Vec2f center = shape.center;
        const float rx = shape.rx;
        const float ry = shape.ry;
        const sgc::Rectf bounds{center.x - rx, center.y - ry, rx * 2, ry * 2};
        const int seg = adaptiveSegments(std::max(rx, ry));
        drawShapeWithStyle(style, center, [&](const Gradient& fill, float expand) {
            emitEllipseFan(center, rx + expand, ry + expand, fill, bounds, seg);
        });
    }

    void drawTriangle(const ShapeTriangle& shape, const Style& style)
    {
        if (!m_recording) return;
        const sgc::Vec2f ctr{(shape.p0.x + shape.p1.x + shape.p2.x) / 3.0f,
                             (shape.p0.y + shape.p1.y + shape.p2.y) / 3.0f};
        const auto bounds = triangleBounds(shape.p0, shape.p1, shape.p2);
        drawShapeWithStyle(style, ctr, [&](const Gradient& fill, float expand) {
            emitTriangle(shape.p0, shape.p1, shape.p2, ctr, expand, fill, bounds);
        });
    }

    void drawLine(const ShapeLine& shape, const Style& style)
    {
        if (!m_recording) return;
        const sgc::Vec2f mid{(shape.from.x + shape.to.x) * 0.5f,
                             (shape.from.y + shape.to.y) * 0.5f};
        const float minX = std::min(shape.from.x, shape.to.x);
        const float minY = std::min(shape.from.y, shape.to.y);
        const float maxX = std::max(shape.from.x, shape.to.x);
        const float maxY = std::max(shape.from.y, shape.to.y);
        const sgc::Rectf bounds{minX, minY, maxX - minX, maxY - minY};
        drawShapeWithStyle(style, mid, [&](const Gradient& fill, float expand) {
            emitLine(shape.from, shape.to, shape.thickness + expand * 2, fill, bounds);
        });
    }

    void drawArc(const ShapeArc& shape, const Style& style)
    {
        if (!m_recording) return;
        const sgc::Vec2f center = shape.center;
        const float r = shape.radius;
        const sgc::Rectf bounds{center.x - r, center.y - r, r * 2, r * 2};
        const int seg = adaptiveArcSegments(r, shape.startAngle, shape.endAngle);
        drawShapeWithStyle(style, center, [&](const Gradient& fill, float expand) {
            emitArcStrip(center, shape.radius + expand, shape.startAngle,
                         shape.endAngle, 2.0f + expand, fill, bounds, seg);
        });
    }

    void drawPie(const ShapePie& shape, const Style& style)
    {
        if (!m_recording) return;
        const sgc::Vec2f center = shape.center;
        const float r = shape.radius;
        const sgc::Rectf bounds{center.x - r, center.y - r, r * 2, r * 2};
        const int seg = adaptiveArcSegments(r, shape.startAngle, shape.endAngle);
        drawShapeWithStyle(style, center, [&](const Gradient& fill, float expand) {
            emitPieFan(center, shape.radius + expand, shape.startAngle,
                       shape.endAngle, fill, bounds, seg);
        });
    }

    void drawRing(const ShapeRing& shape, const Style& style)
    {
        if (!m_recording) return;
        const sgc::Vec2f center = shape.center;
        const float r = shape.outer;
        const sgc::Rectf bounds{center.x - r, center.y - r, r * 2, r * 2};
        const int seg = adaptiveSegments(r);
        drawShapeWithStyle(style, center, [&](const Gradient& fill, float expand) {
            emitRingStrip(center, shape.outer + expand, shape.inner - expand,
                          fill, bounds, seg);
        });
    }

    void drawPolygon(const ShapePolygon& shape, const Style& style)
    {
        if (!m_recording) return;
        if (shape.points.size() < 3) return;
        sgc::Vec2f ctr{0, 0};
        for (const auto& p : shape.points) { ctr.x += p.x; ctr.y += p.y; }
        ctr.x /= static_cast<float>(shape.points.size());
        ctr.y /= static_cast<float>(shape.points.size());
        const auto bounds = polygonBounds(shape.points);
        drawShapeWithStyle(style, ctr, [&](const Gradient& fill, float expand) {
            emitPolygonFan(shape.points, ctr, expand, fill, bounds);
        });
    }

    void drawPath(const ShapePath& shape, const Style& style)
    {
        if (!m_recording) return;
        if (shape.commands.empty()) return;

        // 全 path command を polyline に平坦化する
        const auto points = flattenPath(shape);
        if (points.size() < 3) return;

        // polygon 描画を再利用する
        sgc::Vec2f ctr{0, 0};
        for (const auto& p : points) { ctr.x += p.x; ctr.y += p.y; }
        ctr.x /= static_cast<float>(points.size());
        ctr.y /= static_cast<float>(points.size());
        const auto bounds = polygonBounds(points);
        drawShapeWithStyle(style, ctr, [&](const Gradient& fill, float expand) {
            emitPolygonFan(points, ctr, expand, fill, bounds);
        });
    }

    // ── Accessor ───────────────────────────────────────────

    [[nodiscard]] const std::vector<Vertex2D>& vertices() const { return m_vertices; }
    [[nodiscard]] const std::vector<uint32_t>& indices() const { return m_indices; }
    [[nodiscard]] bool hasData() const { return !m_vertices.empty(); }

private:
    static constexpr float PI = 3.14159265358979323846f;

    /// @brief 半径から最適な segment 数を算出する。
    /// sqrt(radius) でスケールする: 小さい shape は少なく、大きい shape は多く。
    static int adaptiveSegments(float radius)
    {
        return std::clamp(static_cast<int>(std::sqrt(radius) * 4.0f), 8, 64);
    }

    /// @brief 半径と角度幅から arc/pie の segment 数を算出する。
    static int adaptiveArcSegments(float radius, float startDeg, float endDeg)
    {
        const int full = adaptiveSegments(radius);
        const float ratio = std::abs(endDeg - startDeg) / 360.0f;
        return std::max(static_cast<int>(static_cast<float>(full) * ratio), 4);
    }

    std::vector<Vertex2D> m_vertices;
    std::vector<uint32_t> m_indices;
    bool m_recording = false;

    // ── Gradient 評価 ─────────────────────────────────────

    sgc::Colorf evaluateGradient(const Gradient& grad,
                                 float nx, float ny) const
    {
        if (grad.type == Gradient::Type::Solid) return grad.solidColor;

        float t = 0;
        if (grad.type == Gradient::Type::Linear)
        {
            const float rad = grad.angle * PI / 180.0f;
            const float dx = std::cos(rad);
            const float dy = std::sin(rad);
            t = std::clamp((nx - 0.5f) * dx + (ny - 0.5f) * dy + 0.5f, 0.0f, 1.0f);
        }
        else // Radial
        {
            const float ddx = nx - grad.center.x;
            const float ddy = ny - grad.center.y;
            const float dist = std::sqrt(ddx * ddx + ddy * ddy);
            t = std::clamp(dist / std::max(grad.radius, 0.001f), 0.0f, 1.0f);
        }
        return sampleStops(grad, t);
    }

    sgc::Colorf sampleStops(const Gradient& grad, float t) const
    {
        if (grad.stopCount == 0) return grad.solidColor;
        if (grad.stopCount == 1) return grad.stops[0].color;

        if (t <= grad.stops[0].offset) return grad.stops[0].color;
        const uint8_t last = grad.stopCount - 1;
        if (t >= grad.stops[last].offset) return grad.stops[last].color;

        for (uint8_t i = 0; i < last; ++i)
        {
            if (t >= grad.stops[i].offset && t <= grad.stops[i + 1].offset)
            {
                const float range = grad.stops[i + 1].offset - grad.stops[i].offset;
                const float local = (range > 1e-6f)
                    ? (t - grad.stops[i].offset) / range : 0.0f;
                return grad.stops[i].color.lerp(grad.stops[i + 1].color, local);
            }
        }
        return grad.stops[last].color;
    }

    // ── Transform ───────────────────────────────────────────

    sgc::Vec2f applyTransform(const sgc::Vec2f& point,
                              const sgc::Vec2f& center,
                              const StyleTransform& xf) const
    {
        float px = point.x - center.x;
        float py = point.y - center.y;

        // Scale
        px *= xf.scale.x;
        py *= xf.scale.y;

        // Rotate
        if (xf.rotate != 0)
        {
            const float rad = xf.rotate * PI / 180.0f;
            const float cs = std::cos(rad);
            const float sn = std::sin(rad);
            const float rx = px * cs - py * sn;
            const float ry = px * sn + py * cs;
            px = rx;
            py = ry;
        }

        return {px + center.x + xf.translate.x, py + center.y + xf.translate.y};
    }

    // ── bounds 内の正規化座標 ───────────────────

    static sgc::Vec2f normalize(const sgc::Vec2f& p, const sgc::Rectf& b)
    {
        const float w = b.width();
        const float h = b.height();
        const float nx = (w > 1e-6f) ? (p.x - b.x()) / w : 0.5f;
        const float ny = (h > 1e-6f) ? (p.y - b.y()) / h : 0.5f;
        return {nx, ny};
    }

    // ── 描画 orchestrator ───────────────────────────────────

    template <typename EmitFn>
    void drawShapeWithStyle(const Style& style, const sgc::Vec2f& center,
                            EmitFn&& emitFill)
    {
        const auto vertStart = static_cast<uint32_t>(m_vertices.size());

        const bool hasShadow = (style.shadow.blur > 0 || style.shadow.x != 0 ||
                                style.shadow.y != 0) &&
                               style.shadow.color.a > 0;
        const bool hasStroke = style.stroke.width > 0;
        const bool hasTransform = (style.transform.rotate != 0 ||
                                   style.transform.scale.x != 1 ||
                                   style.transform.scale.y != 1 ||
                                   style.transform.translate.x != 0 ||
                                   style.transform.translate.y != 0);

        // 1. Shadow pass
        if (hasShadow)
        {
            const auto shadowStart = static_cast<uint32_t>(m_vertices.size());
            emitFill(Gradient::solid(style.shadow.color), style.shadow.blur * 0.5f);
            // shadow vertex をオフセットする
            for (auto i = shadowStart; i < m_vertices.size(); ++i)
            {
                m_vertices[i].position.x += style.shadow.x;
                m_vertices[i].position.y += style.shadow.y;
                m_vertices[i].color.a *= style.opacity;
            }
        }

        // 2. Stroke pass
        if (hasStroke)
        {
            const auto strokeStart = static_cast<uint32_t>(m_vertices.size());
            emitFill(style.stroke.fill, style.stroke.width);
            for (auto i = strokeStart; i < m_vertices.size(); ++i)
                m_vertices[i].color.a *= style.opacity;
        }

        // 3. Fill pass
        {
            const auto fillStart = static_cast<uint32_t>(m_vertices.size());
            emitFill(style.fill, 0.0f);
            for (auto i = fillStart; i < m_vertices.size(); ++i)
                m_vertices[i].color.a *= style.opacity;
        }

        // 4. この draw call の全 vertex に transform を適用する
        if (hasTransform)
        {
            for (auto i = vertStart; i < m_vertices.size(); ++i)
                m_vertices[i].position = applyTransform(
                    m_vertices[i].position, center, style.transform);
        }
    }

    // ── Primitive emitter ──────────────────────────────────

    void emitCircleFan(sgc::Vec2f center, float radius,
                       const Gradient& fill, const sgc::Rectf& bounds, int seg)
    {
        const auto ci = static_cast<uint32_t>(m_vertices.size());
        const auto nCenter = normalize(center, bounds);
        m_vertices.emplace_back(center, evaluateGradient(fill, nCenter.x, nCenter.y));

        const float step = 2.0f * PI / static_cast<float>(seg);
        for (int i = 0; i <= seg; ++i)
        {
            const float a = step * static_cast<float>(i);
            const sgc::Vec2f pos{center.x + radius * std::cos(a),
                                 center.y + radius * std::sin(a)};
            const auto n = normalize(pos, bounds);
            m_vertices.emplace_back(pos, evaluateGradient(fill, n.x, n.y));
        }
        for (int i = 0; i < seg; ++i)
        {
            m_indices.push_back(ci);
            m_indices.push_back(ci + 1 + static_cast<uint32_t>(i));
            m_indices.push_back(ci + 2 + static_cast<uint32_t>(i));
        }
    }

    void emitEllipseFan(sgc::Vec2f center, float rx, float ry,
                        const Gradient& fill, const sgc::Rectf& bounds, int seg)
    {
        const auto ci = static_cast<uint32_t>(m_vertices.size());
        const auto nc = normalize(center, bounds);
        m_vertices.emplace_back(center, evaluateGradient(fill, nc.x, nc.y));

        const float step = 2.0f * PI / static_cast<float>(seg);
        for (int i = 0; i <= seg; ++i)
        {
            const float a = step * static_cast<float>(i);
            const sgc::Vec2f pos{center.x + rx * std::cos(a),
                                 center.y + ry * std::sin(a)};
            const auto n = normalize(pos, bounds);
            m_vertices.emplace_back(pos, evaluateGradient(fill, n.x, n.y));
        }
        for (int i = 0; i < seg; ++i)
        {
            m_indices.push_back(ci);
            m_indices.push_back(ci + 1 + static_cast<uint32_t>(i));
            m_indices.push_back(ci + 2 + static_cast<uint32_t>(i));
        }
    }

    void emitTriangle(sgc::Vec2f p0, sgc::Vec2f p1, sgc::Vec2f p2,
                      sgc::Vec2f ctr, float expand,
                      const Gradient& fill, const sgc::Rectf& bounds)
    {
        auto expandPt = [&](sgc::Vec2f p) -> sgc::Vec2f {
            const float dx = p.x - ctr.x;
            const float dy = p.y - ctr.y;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d < 1e-6f) return p;
            const float s = (d + expand) / d;
            return {ctr.x + dx * s, ctr.y + dy * s};
        };
        const auto ep0 = expandPt(p0);
        const auto ep1 = expandPt(p1);
        const auto ep2 = expandPt(p2);

        const auto bi = static_cast<uint32_t>(m_vertices.size());
        auto addVert = [&](sgc::Vec2f p) {
            const auto n = normalize(p, bounds);
            m_vertices.emplace_back(p, evaluateGradient(fill, n.x, n.y));
        };
        addVert(ep0); addVert(ep1); addVert(ep2);
        m_indices.push_back(bi);
        m_indices.push_back(bi + 1);
        m_indices.push_back(bi + 2);
    }

    void emitLine(sgc::Vec2f from, sgc::Vec2f to, float thickness,
                  const Gradient& fill, const sgc::Rectf& bounds)
    {
        const sgc::Vec2f dir{to.x - from.x, to.y - from.y};
        const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len < 1e-6f) return;

        const float ht = thickness * 0.5f;
        const sgc::Vec2f perp{-dir.y / len * ht, dir.x / len * ht};
        const auto bi = static_cast<uint32_t>(m_vertices.size());

        auto addVert = [&](sgc::Vec2f p) {
            const auto n = normalize(p, bounds);
            m_vertices.emplace_back(p, evaluateGradient(fill, n.x, n.y));
        };
        addVert({from.x + perp.x, from.y + perp.y});
        addVert({from.x - perp.x, from.y - perp.y});
        addVert({to.x - perp.x, to.y - perp.y});
        addVert({to.x + perp.x, to.y + perp.y});

        m_indices.push_back(bi);     m_indices.push_back(bi + 1); m_indices.push_back(bi + 2);
        m_indices.push_back(bi);     m_indices.push_back(bi + 2); m_indices.push_back(bi + 3);
    }

    void emitArcStrip(sgc::Vec2f center, float radius,
                      float startDeg, float endDeg, float thickness,
                      const Gradient& fill, const sgc::Rectf& bounds, int seg)
    {
        const float startRad = startDeg * PI / 180.0f;
        const float endRad = endDeg * PI / 180.0f;
        const float step = (endRad - startRad) / static_cast<float>(seg);
        const float halfT = thickness * 0.5f;
        const auto bi = static_cast<uint32_t>(m_vertices.size());

        for (int i = 0; i <= seg; ++i)
        {
            const float a = startRad + step * static_cast<float>(i);
            const float cs = std::cos(a);
            const float sn = std::sin(a);
            const sgc::Vec2f outer{center.x + (radius + halfT) * cs,
                                   center.y + (radius + halfT) * sn};
            const sgc::Vec2f inner{center.x + (radius - halfT) * cs,
                                   center.y + (radius - halfT) * sn};
            auto no = normalize(outer, bounds);
            auto ni = normalize(inner, bounds);
            m_vertices.emplace_back(outer, evaluateGradient(fill, no.x, no.y));
            m_vertices.emplace_back(inner, evaluateGradient(fill, ni.x, ni.y));
        }
        for (int i = 0; i < seg; ++i)
        {
            const auto i0 = bi + static_cast<uint32_t>(i * 2);
            m_indices.push_back(i0);     m_indices.push_back(i0 + 1); m_indices.push_back(i0 + 2);
            m_indices.push_back(i0 + 1); m_indices.push_back(i0 + 3); m_indices.push_back(i0 + 2);
        }
    }

    void emitPieFan(sgc::Vec2f center, float radius,
                    float startDeg, float endDeg,
                    const Gradient& fill, const sgc::Rectf& bounds, int seg)
    {
        const float startRad = startDeg * PI / 180.0f;
        const float endRad = endDeg * PI / 180.0f;
        const float step = (endRad - startRad) / static_cast<float>(seg);

        const auto ci = static_cast<uint32_t>(m_vertices.size());
        const auto nc = normalize(center, bounds);
        m_vertices.emplace_back(center, evaluateGradient(fill, nc.x, nc.y));

        for (int i = 0; i <= seg; ++i)
        {
            const float a = startRad + step * static_cast<float>(i);
            const sgc::Vec2f pos{center.x + radius * std::cos(a),
                                 center.y + radius * std::sin(a)};
            const auto n = normalize(pos, bounds);
            m_vertices.emplace_back(pos, evaluateGradient(fill, n.x, n.y));
        }
        for (int i = 0; i < seg; ++i)
        {
            m_indices.push_back(ci);
            m_indices.push_back(ci + 1 + static_cast<uint32_t>(i));
            m_indices.push_back(ci + 2 + static_cast<uint32_t>(i));
        }
    }

    void emitRingStrip(sgc::Vec2f center, float outer, float inner,
                       const Gradient& fill, const sgc::Rectf& bounds, int seg)
    {
        if (inner < 0) inner = 0;
        const auto bi = static_cast<uint32_t>(m_vertices.size());
        const float step = 2.0f * PI / static_cast<float>(seg);

        for (int i = 0; i <= seg; ++i)
        {
            const float a = step * static_cast<float>(i);
            const float cs = std::cos(a);
            const float sn = std::sin(a);
            const sgc::Vec2f op{center.x + outer * cs, center.y + outer * sn};
            const sgc::Vec2f ip{center.x + inner * cs, center.y + inner * sn};
            auto no = normalize(op, bounds);
            auto ni = normalize(ip, bounds);
            m_vertices.emplace_back(op, evaluateGradient(fill, no.x, no.y));
            m_vertices.emplace_back(ip, evaluateGradient(fill, ni.x, ni.y));
        }
        for (int i = 0; i < seg; ++i)
        {
            const auto i0 = bi + static_cast<uint32_t>(i * 2);
            m_indices.push_back(i0);     m_indices.push_back(i0 + 1); m_indices.push_back(i0 + 2);
            m_indices.push_back(i0 + 1); m_indices.push_back(i0 + 3); m_indices.push_back(i0 + 2);
        }
    }

    void emitPolygonFan(const std::vector<sgc::Vec2f>& pts, sgc::Vec2f ctr,
                        float expand, const Gradient& fill, const sgc::Rectf& bounds)
    {
        const auto ci = static_cast<uint32_t>(m_vertices.size());
        const auto nc = normalize(ctr, bounds);
        m_vertices.emplace_back(ctr, evaluateGradient(fill, nc.x, nc.y));

        for (const auto& p : pts)
        {
            const float dx = p.x - ctr.x;
            const float dy = p.y - ctr.y;
            const float d = std::sqrt(dx * dx + dy * dy);
            sgc::Vec2f ep = p;
            if (d > 1e-6f && expand != 0)
            {
                const float s = (d + expand) / d;
                ep = {ctr.x + dx * s, ctr.y + dy * s};
            }
            const auto n = normalize(ep, bounds);
            m_vertices.emplace_back(ep, evaluateGradient(fill, n.x, n.y));
        }
        const auto count = static_cast<int>(pts.size());
        for (int i = 0; i < count; ++i)
        {
            m_indices.push_back(ci);
            m_indices.push_back(ci + 1 + static_cast<uint32_t>(i));
            m_indices.push_back(ci + 1 + static_cast<uint32_t>((i + 1) % count));
        }
    }

    // ── Path 平坦化 ──────────────────────────────────────

    /// @brief de Casteljau 分割で ShapePath を polyline に平坦化する
    static std::vector<sgc::Vec2f> flattenPath(const ShapePath& shape)
    {
        std::vector<sgc::Vec2f> pts;
        sgc::Vec2f cursor{0, 0};

        for (const auto& cmd : shape.commands)
        {
            switch (cmd.cmd)
            {
            case PathCommand::MoveTo:
                cursor = cmd.p0;
                pts.push_back(cursor);
                break;

            case PathCommand::LineTo:
                cursor = cmd.p0;
                pts.push_back(cursor);
                break;

            case PathCommand::QuadTo:
            {
                // 2 次 bezier: cursor -> cmd.p0 (control) -> cmd.p1 (target)
                constexpr int segs = 8;
                const sgc::Vec2f start = cursor;
                const sgc::Vec2f cp = cmd.p0;
                const sgc::Vec2f end = cmd.p1;
                for (int i = 1; i <= segs; ++i)
                {
                    const float t = static_cast<float>(i) / static_cast<float>(segs);
                    const float u = 1.0f - t;
                    const float x = u * u * start.x + 2.0f * u * t * cp.x + t * t * end.x;
                    const float y = u * u * start.y + 2.0f * u * t * cp.y + t * t * end.y;
                    pts.push_back({x, y});
                }
                cursor = end;
                break;
            }

            case PathCommand::CubicTo:
            {
                // 3 次 bezier: cursor -> cmd.p0 (c1) -> cmd.p1 (c2) -> cmd.p2 (target)
                constexpr int segs = 16;
                const sgc::Vec2f start = cursor;
                const sgc::Vec2f c1 = cmd.p0;
                const sgc::Vec2f c2 = cmd.p1;
                const sgc::Vec2f end = cmd.p2;
                for (int i = 1; i <= segs; ++i)
                {
                    const float t = static_cast<float>(i) / static_cast<float>(segs);
                    const float u = 1.0f - t;
                    const float u2 = u * u;
                    const float t2 = t * t;
                    const float x = u2 * u * start.x + 3.0f * u2 * t * c1.x
                                  + 3.0f * u * t2 * c2.x + t2 * t * end.x;
                    const float y = u2 * u * start.y + 3.0f * u2 * t * c1.y
                                  + 3.0f * u * t2 * c2.y + t2 * t * end.y;
                    pts.push_back({x, y});
                }
                cursor = end;
                break;
            }

            case PathCommand::Close:
                // subpath を閉じる: 最初の点へ線を戻す
                if (!pts.empty())
                {
                    cursor = pts.front();
                }
                break;
            }
        }

        return pts;
    }

    // ── Bounds ヘルパー ──────────────────────────────────────

    static sgc::Rectf triangleBounds(sgc::Vec2f p0, sgc::Vec2f p1, sgc::Vec2f p2)
    {
        const float minX = std::min({p0.x, p1.x, p2.x});
        const float minY = std::min({p0.y, p1.y, p2.y});
        const float maxX = std::max({p0.x, p1.x, p2.x});
        const float maxY = std::max({p0.y, p1.y, p2.y});
        return {minX, minY, maxX - minX, maxY - minY};
    }

    static sgc::Rectf polygonBounds(const std::vector<sgc::Vec2f>& pts)
    {
        if (pts.empty()) return {0, 0, 0, 0};
        float minX = pts[0].x, minY = pts[0].y;
        float maxX = pts[0].x, maxY = pts[0].y;
        for (const auto& p : pts)
        {
            minX = std::min(minX, p.x); minY = std::min(minY, p.y);
            maxX = std::max(maxX, p.x); maxY = std::max(maxY, p.y);
        }
        return {minX, minY, maxX - minX, maxY - minY};
    }
};

} // namespace mitiru::render
