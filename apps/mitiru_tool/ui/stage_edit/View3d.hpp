#pragma once
// 3D の図の視点。world から画面への投影と、画面の 1 点を通るレイ。
// 画面は x が右、y が下。カメラ座標は right / up / fwd の成分で、fwd が正のとき前にある。

#include "Canvas.hpp"
#include "Geometry.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace mitiru::tool::stageedit
{

struct Ray
{
	Vec3 origin;
	Vec3 dir;   ///< 単位ベクトル
};

inline Vec3 v3sub(const Vec3& a, const Vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vec3 v3add(const Vec3& a, const Vec3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vec3 v3mul(const Vec3& a, double s) { return { a.x * s, a.y * s, a.z * s }; }
inline double v3dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 v3cross(const Vec3& a, const Vec3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
inline double v3len(const Vec3& a) { return std::sqrt(v3dot(a, a)); }
inline Vec3 v3unit(const Vec3& a)
{
	const double l = v3len(a);
	return l < 1e-12 ? Vec3{ 0, 0, 1 } : Vec3{ a.x / l, a.y / l, a.z / l };
}

struct ViewCam
{
	Vec3 pos;
	Vec3 right;
	Vec3 up;
	Vec3 fwd;
	double focal = 1.0;   ///< px。画面の高さの半分 / tan(fov / 2)
	int w = 1;
	int h = 1;
	double nearZ = 0.3;

	[[nodiscard]] Vec3 toCam(const Vec3& p) const
	{
		const Vec3 v = v3sub(p, pos);
		return { v3dot(v, right), v3dot(v, up), v3dot(v, fwd) };
	}

	[[nodiscard]] std::pair<double, double> screenOf(const Vec3& c) const
	{
		return { w * 0.5 + c.x / c.z * focal, h * 0.5 - c.y / c.z * focal };
	}

	/// 近い面より手前の点は投影しない
	[[nodiscard]] std::optional<std::pair<double, double>> project(const Vec3& p) const
	{
		const Vec3 c = toCam(p);
		if (c.z < nearZ) { return std::nullopt; }
		return screenOf(c);
	}

	[[nodiscard]] Ray rayAt(double sx, double sy) const
	{
		const double cx = (sx - w * 0.5) / focal;
		const double cy = -(sy - h * 0.5) / focal;
		return { pos, v3unit(v3add(v3add(v3mul(right, cx), v3mul(up, cy)), fwd)) };
	}

	/// p の奥行きで、画面の 1 px が world で何 m か
	[[nodiscard]] double metersPerDotAt(const Vec3& p) const
	{
		return std::max(nearZ, toCam(p).z) / focal;
	}
};

/// world の線分を、近い面で切ってから描く。両端とも手前なら描かない
inline void drawLine3(Canvas& canvas, const ViewCam& cam, const Vec3& a, const Vec3& b, Rgba color, int thickness)
{
	Vec3 ca = cam.toCam(a);
	Vec3 cb = cam.toCam(b);
	const double n = cam.nearZ;
	if (ca.z < n && cb.z < n) { return; }
	if (ca.z < n) { const double t = (n - ca.z) / (cb.z - ca.z); ca = { ca.x + (cb.x - ca.x) * t, ca.y + (cb.y - ca.y) * t, n }; }
	if (cb.z < n) { const double t = (n - cb.z) / (ca.z - cb.z); cb = { cb.x + (ca.x - cb.x) * t, cb.y + (ca.y - cb.y) * t, n }; }
	const auto p = cam.screenOf(ca);
	const auto q = cam.screenOf(cb);
	canvas.line(p.first, p.second, q.first, q.second, color, thickness);
}

} // namespace mitiru::tool::stageedit
