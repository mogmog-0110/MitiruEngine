#pragma once

/// @file TriangleSweep.hpp
/// @brief 球とカプセルを動かしたときの、三角形 1 枚との最初の接触 (取りこぼし無しの厳密解)。
/// @details 球は ① 半径だけずらした平面との交点が三角形の内側か ② 3 辺の円柱 ③ 3 頂点の球、の最小の時刻。
///          カプセルは ① 両端の球の掃引 ② 三角形の頂点を逆向きに動かしたレイと線分の円柱
///          ③ 線分の内部と辺の内部が半径の距離になる時刻、の最小。二分探索は使わないので、同じ入力から同じ時刻が出る。
///          時刻 t は delta に対する割合 (0..1)。開始時に深く重なっている場合の扱いは呼び出し側が先に重なりで決める。

#include <cmath>

#include <mitiru/action/TriangleTests.hpp>

namespace mitiru::action::geom
{

struct SweepContact
{
	float t = 1.0f;
	Vec3  point{};   ///< 三角形上の接触点
	Vec3  normal{};  ///< 形状の中心の側を向く
};

/// @brief レイ (o + d t, t は 0..1) が球へ外から入る最初の t。始点が中で中心へ近づくなら 0
[[nodiscard]] inline bool raySphere(const Vec3& o, const Vec3& d, const Vec3& center, float r, float& outT) noexcept
{
	const Vec3  m = o - center;
	const float b = dot(m, d);
	const float c = lengthSq(m) - r * r;
	if (c <= 0.0f)
	{
		if (b < 0.0f) { outT = 0.0f; return true; }
		return false;
	}
	const float a = lengthSq(d);
	if (b >= 0.0f || !(a > kTinyDenom)) { return false; }
	const float disc = b * b - a * c;
	if (disc < 0.0f) { return false; }
	const float t = (-b - std::sqrt(disc)) / a;
	if (!(t <= 1.0f)) { return false; }
	outT = maxf(t, 0.0f);
	return true;
}

/// @brief レイ (o + d t, t は 0..1) vs 軸 p-q の円柱の胴 (ふたは端の球に任せる)
[[nodiscard]] inline bool rayCylinder(const Vec3& o, const Vec3& d, const Vec3& p, const Vec3& q, float r,
                                      float& outT) noexcept
{
	const Vec3  ab = q - p;
	const float L2 = lengthSq(ab);
	if (!(L2 > 1e-20f)) { return false; }
	const float L    = std::sqrt(L2);
	const Vec3  axis = ab * (1.0f / L);
	const Vec3  ao   = o - p;
	const float aoA = dot(ao, axis), dA = dot(d, axis);
	const Vec3  aoP = ao - axis * aoA, dP = d - axis * dA;
	const float A = lengthSq(dP), B = dot(aoP, dP), C = lengthSq(aoP) - r * r;
	if (C <= 0.0f)
	{
		if (aoA >= 0.0f && aoA <= L && B < 0.0f) { outT = 0.0f; return true; }
		return false;
	}
	if (B >= 0.0f || !(A > kTinyDenom)) { return false; }
	const float disc = B * B - A * C;
	if (disc < 0.0f) { return false; }
	const float t = (-B - std::sqrt(disc)) / A;
	const float s = aoA + dA * t;
	if (!(t >= 0.0f && t <= 1.0f) || !(s >= 0.0f && s <= L)) { return false; }
	outT = t;
	return true;
}

namespace detail
{

enum class FaceSweep : std::uint8_t
{
	Hit,
	Miss,     ///< 平面にも届かない。辺と頂点も調べなくてよい
	TryEdges,
};

/// @brief 球の掃引の面の部分。三角形の内側で確定すれば Hit
[[nodiscard]] inline FaceSweep sweepSphereFace(const Vec3& c0, float r, const Vec3& delta, const Vec3& a,
                                               const Vec3& b, const Vec3& c, const Vec3& n, SweepContact& out) noexcept
{
	const float dist0 = dot(c0 - a, n);
	const Vec3  ns    = dist0 >= 0.0f ? n : -n;
	const float adist = absf(dist0);
	const float vn    = dot(delta, ns);
	out.normal        = ns;
	if (adist >= r)
	{
		if (!(vn < 0.0f)) { return FaceSweep::Miss; }
		const float t = (adist - r) / (-vn);
		if (t > 1.0f) { return FaceSweep::Miss; }
		const Vec3 p = c0 + delta * t - ns * r;
		if (!pointInTriangle(p, a, b, c, n)) { return FaceSweep::TryEdges; }
		out.t     = t;
		out.point = p;
		return FaceSweep::Hit;
	}
	const Vec3 proj = c0 - n * dist0;
	if (!pointInTriangle(proj, a, b, c, n)) { return FaceSweep::TryEdges; }
	if (!(vn < 0.0f)) { return FaceSweep::Miss; }
	out.t     = 0.0f;
	out.point = proj;
	return FaceSweep::Hit;
}

} // namespace detail

/// @brief 球 (中心 c0、半径 r) を delta 動かしたときの三角形との最初の接触
[[nodiscard]] inline bool sweepSphereTriangle(const Vec3& c0, float r, const Vec3& delta, const Vec3& a, const Vec3& b,
                                              const Vec3& c, SweepContact& out) noexcept
{
	const Vec3  nRaw = triangleNormalRaw(a, b, c);
	const float nLen = length(nRaw);
	if (!(nLen > 1e-20f)) { return false; }
	const Vec3 n = nRaw * (1.0f / nLen);
	SweepContact face;
	const detail::FaceSweep fs = detail::sweepSphereFace(c0, r, delta, a, b, c, n, face);
	if (fs == detail::FaceSweep::Hit) { out = face; return true; }
	if (fs == detail::FaceSweep::Miss) { return false; }

	bool        found = false;
	float       bestT = 2.0f;
	Vec3        bestP{};
	const Vec3* v[3]  = {&a, &b, &c};
	for (int i = 0; i < 3; ++i)
	{
		float t = 0.0f;
		if (rayCylinder(c0, delta, *v[i], *v[(i + 1) % 3], r, t) && t < bestT)
		{
			bestT = t;
			bestP = closestPointOnSegment(c0 + delta * t, *v[i], *v[(i + 1) % 3]);
			found = true;
		}
	}
	for (int i = 0; i < 3; ++i)
	{
		float t = 0.0f;
		if (raySphere(c0, delta, *v[i], r, t) && t < bestT) { bestT = t; bestP = *v[i]; found = true; }
	}
	if (!found) { return false; }
	out.t      = bestT;
	out.point  = bestP;
	out.normal = normalizeOr(c0 + delta * bestT - bestP, face.normal);
	return true;
}

/// @brief 動く線分 (p0-q0 を delta 動かす) と止まった線分 e0-e1 の内部同士が距離 r になる最初の時刻
/// @details 2 直線がほぼ平行なら false。そのときは端点と頂点の判定が同時に当たる。
[[nodiscard]] inline bool sweepSegmentEdge(const Vec3& p0, const Vec3& q0, const Vec3& delta, const Vec3& e0,
                                           const Vec3& e1, float r, float& outT, Vec3& outSeg, Vec3& outEdge) noexcept
{
	constexpr float kTol = 1e-5f;
	const Vec3      D1 = q0 - p0, D2 = e1 - e0;
	const Vec3      m  = cross(D1, D2);
	const float     m2 = lengthSq(m), a = lengthSq(D1), e = lengthSq(D2);
	if (!(m2 > a * e * 1e-10f) || !(m2 > 1e-30f)) { return false; }
	const Vec3  nn   = m * (1.0f / std::sqrt(m2));
	const float d0   = dot(p0 - e0, nn);
	const float rate = dot(delta, nn) * (d0 >= 0.0f ? 1.0f : -1.0f);
	if (!(rate < 0.0f)) { return false; }
	const float t = absf(d0) < r ? 0.0f : (absf(d0) - r) / (-rate);
	if (!(t <= 1.0f)) { return false; }

	const Vec3  rr    = p0 + delta * t - e0;
	const float b     = dot(D1, D2);
	const float denom = a * e - b * b;
	if (!(denom > kTinyDenom)) { return false; }
	const float s = (b * dot(D2, rr) - dot(D1, rr) * e) / denom;
	const float u = (b * s + dot(D2, rr)) / e;
	if (!(s >= -kTol && s <= 1.0f + kTol && u >= -kTol && u <= 1.0f + kTol)) { return false; }
	outT    = t;
	outSeg  = p0 + delta * t + D1 * saturate(s);
	outEdge = e0 + D2 * saturate(u);
	return true;
}

/// @brief カプセル (線分 p0-q0、半径 r) を delta 動かしたときの三角形との最初の接触
[[nodiscard]] inline bool sweepCapsuleTriangle(const Vec3& p0, const Vec3& q0, float r, const Vec3& delta,
                                               const Vec3& a, const Vec3& b, const Vec3& c, SweepContact& out) noexcept
{
	const Vec3 n = normalizeOr(triangleNormalRaw(a, b, c), Vec3{});
	if (!(lengthSq(n) > 0.5f)) { return false; }
	SweepContact best;
	best.t         = 2.0f;
	bool fromEdges = false;
	Vec3 shapePoint{};
	const Vec3* ends[2] = {&p0, &q0};
	for (const Vec3* end : ends)
	{
		SweepContact sc;
		if (sweepSphereTriangle(*end, r, delta, a, b, c, sc) && sc.t < best.t) { best = sc; fromEdges = false; }
	}
	const Vec3* v[3] = {&a, &b, &c};
	for (const Vec3* vert : v)
	{
		float t = 0.0f;
		if (rayCylinder(*vert, -delta, p0, q0, r, t) && t < best.t)
		{
			best.t     = t;
			best.point = *vert;
			shapePoint = closestPointOnSegment(*vert, p0 + delta * t, q0 + delta * t);
			fromEdges  = true;
		}
	}
	for (int i = 0; i < 3; ++i)
	{
		float t = 0.0f;
		Vec3  sp{}, ep{};
		if (sweepSegmentEdge(p0, q0, delta, *v[i], *v[(i + 1) % 3], r, t, sp, ep) && t < best.t)
		{
			best.t     = t;
			best.point = ep;
			shapePoint = sp;
			fromEdges  = true;
		}
	}
	if (!(best.t <= 1.0f)) { return false; }
	if (fromEdges)
	{
		const Vec3 mid = (p0 + q0) * 0.5f + delta * best.t;
		best.normal    = normalizeOr(shapePoint - best.point, dot(mid - a, n) >= 0.0f ? n : -n);
	}
	out = best;
	return true;
}

} // namespace mitiru::action::geom
