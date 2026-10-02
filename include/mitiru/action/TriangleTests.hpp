#pragma once

/// @file TriangleTests.hpp
/// @brief 三角形 1 枚に対する最近点・レイ・球とカプセルの重なり。
/// @details 判定は両面で、返す法線は形状の中心 (レイなら始点) の側を向く。面で当たれば面法線、
///          辺か頂点なら (中心 - 接触点) を正規化した向き。割り算は分母を確かめてから行い、NaN を作らない。
///          最近点の求め方は Ericson「Real-Time Collision Detection」5.1.5 と 5.1.9。

#include <cmath>
#include <cstdint>

#include <mitiru/action/ActionMath.hpp>

namespace mitiru::action::geom
{

enum class TriFeature : std::uint8_t
{
	Face,
	Edge,
	Vertex,
};

struct TriClosest
{
	Vec3       point{};
	TriFeature feature = TriFeature::Face;
};

struct SegSegClosest
{
	float s = 0.0f;
	float t = 0.0f;
	Vec3  c1{};
	Vec3  c2{};
	float distSq = 0.0f;
};

struct SegTriClosest
{
	Vec3  onSegment{};
	Vec3  onTriangle{};
	float distSq     = 0.0f;
	bool  intersects = false;
};

/// @brief 三角形 1 枚との重なり。normal は形状を押し出す向き
struct TriContact
{
	Vec3  point{};
	Vec3  normal{};
	float depth = 0.0f;
};

inline constexpr float kTinyDenom = 1e-30f;

[[nodiscard]] constexpr Vec3 triangleNormalRaw(const Vec3& a, const Vec3& b, const Vec3& c) noexcept
{
	return cross(b - a, c - a);
}

/// @brief 面積が最長辺に対してほぼ 0 の三角形。BVH に入れると法線が定まらないので構築時に捨てる
[[nodiscard]] inline bool isDegenerateTriangle(const Vec3& a, const Vec3& b, const Vec3& c) noexcept
{
	const float n2   = lengthSq(triangleNormalRaw(a, b, c));
	const float emax = maxf(lengthSq(b - a), maxf(lengthSq(c - b), lengthSq(a - c)));
	return !(n2 > 1e-24f) || !(n2 > emax * emax * 1e-12f);
}

/// @brief スラブ判定用の逆数。0 成分は巨大値にして inf と NaN を避ける
[[nodiscard]] inline Vec3 safeInverse(const Vec3& d) noexcept
{
	const auto inv = [](float v) -> float {
		if (absf(v) > 1e-20f) { return 1.0f / v; }
		return v < 0.0f ? -1e30f : 1e30f;
	};
	return {inv(d.x), inv(d.y), inv(d.z)};
}

/// @brief p を三角形の平面へ射影したときに内側か (n の向きは問わない)
[[nodiscard]] constexpr bool pointInTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c,
                                             const Vec3& n) noexcept
{
	const float e0 = dot(cross(b - a, p - a), n);
	const float e1 = dot(cross(c - b, p - b), n);
	const float e2 = dot(cross(a - c, p - c), n);
	return (e0 >= 0.0f && e1 >= 0.0f && e2 >= 0.0f) || (e0 <= 0.0f && e1 <= 0.0f && e2 <= 0.0f);
}

[[nodiscard]] constexpr Vec3 closestPointOnSegment(const Vec3& p, const Vec3& a, const Vec3& b) noexcept
{
	const Vec3  ab = b - a;
	const float l2 = lengthSq(ab);
	return l2 > kTinyDenom ? a + ab * saturate(dot(p - a, ab) / l2) : a;
}

[[nodiscard]] inline TriClosest closestPointOnTriangle(const Vec3& p, const Vec3& a, const Vec3& b,
                                                       const Vec3& c) noexcept
{
	const Vec3  ab = b - a, ac = c - a, ap = p - a;
	const float d1 = dot(ab, ap), d2 = dot(ac, ap);
	if (d1 <= 0.0f && d2 <= 0.0f) { return {a, TriFeature::Vertex}; }
	const Vec3  bp = p - b;
	const float d3 = dot(ab, bp), d4 = dot(ac, bp);
	if (d3 >= 0.0f && d4 <= d3) { return {b, TriFeature::Vertex}; }
	const float vc = d1 * d4 - d3 * d2;
	if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
	{
		const float den = d1 - d3;
		return {a + ab * (den > kTinyDenom ? d1 / den : 0.0f), TriFeature::Edge};
	}
	const Vec3  cp = p - c;
	const float d5 = dot(ab, cp), d6 = dot(ac, cp);
	if (d6 >= 0.0f && d5 <= d6) { return {c, TriFeature::Vertex}; }
	const float vb = d5 * d2 - d1 * d6;
	if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
	{
		const float den = d2 - d6;
		return {a + ac * (den > kTinyDenom ? d2 / den : 0.0f), TriFeature::Edge};
	}
	const float va = d3 * d6 - d5 * d4;
	if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
	{
		const float den = (d4 - d3) + (d5 - d6);
		return {b + (c - b) * (den > kTinyDenom ? (d4 - d3) / den : 0.0f), TriFeature::Edge};
	}
	const float sum = va + vb + vc;
	if (!(absf(sum) > kTinyDenom)) { return {a, TriFeature::Vertex}; }
	return {a + ab * (vb / sum) + ac * (vc / sum), TriFeature::Face};
}

[[nodiscard]] inline SegSegClosest closestSegmentSegment(const Vec3& p1, const Vec3& q1, const Vec3& p2,
                                                         const Vec3& q2) noexcept
{
	const Vec3  d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
	const float a = lengthSq(d1), e = lengthSq(d2), f = dot(d2, r);
	float       s = 0.0f, t = 0.0f;
	if (a <= kTinyDenom && e > kTinyDenom) { t = saturate(f / e); }
	else if (a > kTinyDenom)
	{
		const float c = dot(d1, r);
		if (e <= kTinyDenom) { s = saturate(-c / a); }
		else
		{
			const float b     = dot(d1, d2);
			const float denom = a * e - b * b;
			s = denom > kTinyDenom ? saturate((b * f - c * e) / denom) : 0.0f;
			t = (b * s + f) / e;
			if (t < 0.0f) { t = 0.0f; s = saturate(-c / a); }
			else if (t > 1.0f) { t = 1.0f; s = saturate((b - c) / a); }
		}
	}
	SegSegClosest out;
	out.s      = s;
	out.t      = t;
	out.c1     = p1 + d1 * s;
	out.c2     = p2 + d2 * t;
	out.distSq = lengthSq(out.c1 - out.c2);
	return out;
}

/// @brief 線分と三角形の最近点。貫いていれば intersects
[[nodiscard]] inline SegTriClosest closestSegmentTriangle(const Vec3& p, const Vec3& q, const Vec3& a,
                                                          const Vec3& b, const Vec3& c) noexcept
{
	SegTriClosest out;
	const Vec3    n     = triangleNormalRaw(a, b, c);
	const Vec3    d     = q - p;
	const float   denom = dot(d, n);
	if (absf(denom) > kTinyDenom)
	{
		const float t = dot(a - p, n) / denom;
		const Vec3  x = p + d * t;
		if (t >= 0.0f && t <= 1.0f && pointInTriangle(x, a, b, c, n))
		{
			out.onSegment = out.onTriangle = x;
			out.intersects = true;
			return out;
		}
	}
	const TriClosest cp = closestPointOnTriangle(p, a, b, c);
	out.onSegment  = p;
	out.onTriangle = cp.point;
	out.distSq     = lengthSq(p - cp.point);
	const TriClosest cq = closestPointOnTriangle(q, a, b, c);
	if (lengthSq(q - cq.point) < out.distSq)
	{
		out.onSegment  = q;
		out.onTriangle = cq.point;
		out.distSq     = lengthSq(q - cq.point);
	}
	const Vec3 ev[3][2] = {{a, b}, {b, c}, {c, a}};
	for (const auto& e : ev)
	{
		const SegSegClosest s = closestSegmentSegment(p, q, e[0], e[1]);
		if (s.distSq < out.distSq)
		{
			out.onSegment  = s.c1;
			out.onTriangle = s.c2;
			out.distSq     = s.distSq;
		}
	}
	return out;
}

/// @brief Moller-Trumbore 法。d は正規化しなくてよい (t は d の長さ単位)
[[nodiscard]] inline bool rayTriangle(const Vec3& o, const Vec3& d, const Vec3& a, const Vec3& b, const Vec3& c,
                                      float tMax, bool cullBackface, float& outT) noexcept
{
	constexpr float kBaryEps = 1e-7f;
	const Vec3      e1 = b - a, e2 = c - a;
	const Vec3      pv  = cross(d, e2);
	const float     det = dot(e1, pv);
	if (cullBackface ? !(det > 1e-20f) : !(absf(det) > 1e-20f)) { return false; }
	const float inv = 1.0f / det;
	const Vec3  tv  = o - a;
	const float u   = dot(tv, pv) * inv;
	if (!(u >= -kBaryEps && u <= 1.0f + kBaryEps)) { return false; }
	const Vec3  qv = cross(tv, e1);
	const float v  = dot(d, qv) * inv;
	if (!(v >= -kBaryEps && u + v <= 1.0f + kBaryEps)) { return false; }
	const float t = dot(e2, qv) * inv;
	if (!(t >= 0.0f && t <= tMax)) { return false; }
	outT = t;
	return true;
}

/// @brief レイ vs 箱 (スラブ)。入る時刻 (0 未満は 0) を outEnter に返す
[[nodiscard]] inline bool rayAabb(const Vec3& o, const Vec3& invD, const Aabb& box, float tMax,
                                  float& outEnter) noexcept
{
	const float tx1 = (box.min.x - o.x) * invD.x, tx2 = (box.max.x - o.x) * invD.x;
	const float ty1 = (box.min.y - o.y) * invD.y, ty2 = (box.max.y - o.y) * invD.y;
	const float tz1 = (box.min.z - o.z) * invD.z, tz2 = (box.max.z - o.z) * invD.z;
	const float tmin = maxf(maxf(minf(tx1, tx2), minf(ty1, ty2)), maxf(minf(tz1, tz2), 0.0f));
	const float tmax = minf(minf(maxf(tx1, tx2), maxf(ty1, ty2)), minf(maxf(tz1, tz2), tMax));
	outEnter         = tmin;
	return tmin <= tmax;
}

/// @brief 球 vs 三角形。中心がちょうど面上なら表 (反時計回り) の側へ押す
[[nodiscard]] inline bool sphereTriangle(const Vec3& center, float r, const Vec3& a, const Vec3& b, const Vec3& c,
                                         TriContact& out) noexcept
{
	const TriClosest cp = closestPointOnTriangle(center, a, b, c);
	const Vec3       dv = center - cp.point;
	const float      d2 = lengthSq(dv);
	if (!(d2 < r * r)) { return false; }
	const Vec3  n = normalizeOr(triangleNormalRaw(a, b, c), Vec3{0.0f, 1.0f, 0.0f});
	const float d = std::sqrt(d2);
	if (cp.feature == TriFeature::Face) { out.normal = dot(dv, n) < 0.0f ? -n : n; }
	else { out.normal = d > 1e-7f ? dv * (1.0f / d) : n; }
	out.point = cp.point;
	out.depth = r - d;
	return true;
}

/// @brief カプセル (線分 p-q と半径) vs 三角形。線分が面を貫くときは押し出しの短い側へ押す
[[nodiscard]] inline bool capsuleTriangle(const Vec3& p, const Vec3& q, float r, const Vec3& a, const Vec3& b,
                                          const Vec3& c, TriContact& out) noexcept
{
	const SegTriClosest cl = closestSegmentTriangle(p, q, a, b, c);
	if (!(cl.distSq < r * r)) { return false; }
	const Vec3 n = normalizeOr(triangleNormalRaw(a, b, c), Vec3{0.0f, 1.0f, 0.0f});
	out.point    = cl.onTriangle;
	if (cl.intersects || cl.distSq <= 1e-14f)
	{
		const float e0 = dot(p - a, n), e1 = dot(q - a, n);
		const float up = r - minf(e0, e1), down = r + maxf(e0, e1);
		out.normal = up <= down ? n : -n;
		out.depth  = minf(up, down);
		return true;
	}
	const float d = std::sqrt(cl.distSq);
	out.normal    = (cl.onSegment - cl.onTriangle) * (1.0f / d);
	out.depth     = r - d;
	return true;
}

} // namespace mitiru::action::geom
