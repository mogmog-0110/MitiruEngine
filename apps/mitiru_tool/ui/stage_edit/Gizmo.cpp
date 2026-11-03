#include "Gizmo.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mitiru::tool::stageedit
{

namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kAxisPx = 70.0;      ///< 軸 1 本の画面での長さ
constexpr double kPickPx = 8.0;
constexpr double kMinAxisPx = 12.0;   ///< これより短く映る軸はつかめない
constexpr double kRingRatio = 0.8;
constexpr double kPlaneLo = 0.3;
constexpr double kPlaneHi = 0.55;
constexpr double kGrabRatio = 0.6;
constexpr int kRingSegments = 48;

constexpr Rgba kAxisX{ 222, 72, 72, 255 };
constexpr Rgba kAxisY{ 70, 182, 86, 255 };
constexpr Rgba kAxisZ{ 70, 118, 232, 255 };
constexpr Rgba kPlane{ 232, 196, 60, 255 };
constexpr Rgba kRing{ 236, 236, 236, 255 };
constexpr Rgba kActive{ 255, 255, 255, 255 };

using Pt = std::pair<double, double>;

Vec3 axisOf(GizmoPart part)
{
	switch (part)
	{
	case GizmoPart::AxisX: return { 1, 0, 0 };
	case GizmoPart::AxisY: return { 0, 1, 0 };
	default: return { 0, 0, 1 };
	}
}

Rgba colorOf(GizmoPart part, GizmoPart active)
{
	if (part == active) { return kActive; }
	switch (part)
	{
	case GizmoPart::AxisX: return kAxisX;
	case GizmoPart::AxisY: return kAxisY;
	case GizmoPart::AxisZ: return kAxisZ;
	case GizmoPart::PlaneXZ: return kPlane;
	default: return kRing;
	}
}

double distanceToSegment(const Pt& p, const Pt& a, const Pt& b)
{
	const double ex = b.first - a.first, ey = b.second - a.second;
	const double l2 = ex * ex + ey * ey;
	const double t = l2 < 1e-9 ? 0.0 : std::clamp(((p.first - a.first) * ex + (p.second - a.second) * ey) / l2, 0.0, 1.0);
	return std::hypot(p.first - (a.first + ex * t), p.second - (a.second + ey * t));
}

bool insidePolygon(const Pt& p, const std::vector<Pt>& poly)
{
	bool in = false;
	for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
	{
		const Pt& a = poly[i];
		const Pt& b = poly[j];
		if ((a.second > p.second) != (b.second > p.second) && p.first < (b.first - a.first) * (p.second - a.second) / (b.second - a.second) + a.first) { in = !in; }
	}
	return in;
}

std::vector<Vec3> planeCorners(const GizmoFrame& f)
{
	const double lo = f.scale * kPlaneLo, hi = f.scale * kPlaneHi;
	const Vec3& a = f.anchor;
	return { { a.x + lo, a.y, a.z + lo }, { a.x + hi, a.y, a.z + lo }, { a.x + hi, a.y, a.z + hi }, { a.x + lo, a.y, a.z + hi } };
}

Vec3 ringPoint(const GizmoFrame& f, double angleRad)
{
	const double r = f.scale * kRingRatio;
	return { f.anchor.x + std::sin(angleRad) * r, f.anchor.y, f.anchor.z + std::cos(angleRad) * r };
}

std::vector<Vec3> ringPoints(const GizmoFrame& f)
{
	std::vector<Vec3> out;
	out.reserve(kRingSegments + 1);
	for (int i = 0; i <= kRingSegments; ++i) { out.push_back(ringPoint(f, 2.0 * kPi * i / kRingSegments)); }
	return out;
}

/// 投影できた点だけを返す。1 つでも手前なら空
std::vector<Pt> projectAll(const ViewCam& cam, const std::vector<Vec3>& pts)
{
	std::vector<Pt> out;
	for (const Vec3& p : pts)
	{
		const auto s = cam.project(p);
		if (!s) { return {}; }
		out.push_back(*s);
	}
	return out;
}

/// 軸の直線 anchor + axis * s の上で、レイに一番近い点の s。平行なら nullopt
std::optional<double> closestOnAxis(const Vec3& anchor, const Vec3& axis, const Ray& ray)
{
	const Vec3 w0 = v3sub(anchor, ray.origin);
	const double c = v3dot(axis, ray.dir);
	const double denom = 1.0 - c * c;
	if (denom < 1e-6) { return std::nullopt; }
	return (c * v3dot(ray.dir, w0) - v3dot(axis, w0)) / denom;
}

std::optional<Vec3> hitPlaneY(double y, const Ray& ray)
{
	if (std::fabs(ray.dir.y) < 1e-6) { return std::nullopt; }
	const double t = (y - ray.origin.y) / ray.dir.y;
	if (t < 0.0) { return std::nullopt; }
	return v3add(ray.origin, v3mul(ray.dir, t));
}

double yawOf(const Vec3& anchor, const Vec3& p)
{
	return std::atan2(p.x - anchor.x, p.z - anchor.z) / kDeg;
}

} // namespace

GizmoFrame gizmoFrameOf(const Geom& g, const ViewCam& cam)
{
	const Aabb b = boundsOf(g);
	GizmoFrame f;
	f.anchor = { (b.lo.x + b.hi.x) * 0.5, (b.lo.y + b.hi.y) * 0.5, (b.lo.z + b.hi.z) * 0.5 };
	f.scale = cam.metersPerDotAt(f.anchor) * kAxisPx;
	f.hasYaw = g.hasYaw;
	f.yawDeg = g.yaw;
	return f;
}

GizmoPart pickGizmo(const GizmoFrame& f, const ViewCam& cam, double sx, double sy)
{
	const auto anchorS = cam.project(f.anchor);
	if (!anchorS) { return GizmoPart::None; }
	const Pt p{ sx, sy };
	GizmoPart best = GizmoPart::None;
	double bestD = kPickPx;
	const auto consider = [&](GizmoPart part, double d) { if (d < bestD) { bestD = d; best = part; } };
	if (const auto corners = projectAll(cam, planeCorners(f)); corners.size() == 4 && insidePolygon(p, corners)) { consider(GizmoPart::PlaneXZ, 0.0); }
	if (f.hasYaw)
	{
		const auto ring = projectAll(cam, ringPoints(f));
		double d = 1e18;
		for (std::size_t i = 0; i + 1 < ring.size(); ++i) { d = std::min(d, distanceToSegment(p, ring[i], ring[i + 1])); }
		consider(GizmoPart::Yaw, d);
	}
	for (const GizmoPart part : { GizmoPart::AxisX, GizmoPart::AxisY, GizmoPart::AxisZ })
	{
		const auto endS = cam.project(v3add(f.anchor, v3mul(axisOf(part), f.scale)));
		if (!endS || std::hypot(endS->first - anchorS->first, endS->second - anchorS->second) < kMinAxisPx) { continue; }
		consider(part, distanceToSegment(p, *anchorS, *endS));
	}
	return best;
}

Vec3 gizmoGrabPoint(const GizmoFrame& f, GizmoPart part)
{
	switch (part)
	{
	case GizmoPart::AxisX:
	case GizmoPart::AxisY:
	case GizmoPart::AxisZ: return v3add(f.anchor, v3mul(axisOf(part), f.scale * kGrabRatio));
	case GizmoPart::PlaneXZ:
	{
		const double m = f.scale * (kPlaneLo + kPlaneHi) * 0.5;
		return { f.anchor.x + m, f.anchor.y, f.anchor.z + m };
	}
	case GizmoPart::Yaw: return ringPoint(f, f.yawDeg * kDeg);
	default: return f.anchor;
	}
}

GizmoDelta gizmoDelta(const GizmoFrame& f, GizmoPart part, const Ray& down, const Ray& now)
{
	GizmoDelta d;
	switch (part)
	{
	case GizmoPart::AxisX:
	case GizmoPart::AxisY:
	case GizmoPart::AxisZ:
	{
		const Vec3 axis = axisOf(part);
		const auto s0 = closestOnAxis(f.anchor, axis, down);
		const auto s1 = closestOnAxis(f.anchor, axis, now);
		if (!s0 || !s1) { return d; }
		const Vec3 move = v3mul(axis, *s1 - *s0);
		d.dx = move.x; d.dy = move.y; d.dz = move.z;
		return d;
	}
	case GizmoPart::PlaneXZ:
	{
		const auto p0 = hitPlaneY(f.anchor.y, down);
		const auto p1 = hitPlaneY(f.anchor.y, now);
		if (!p0 || !p1) { return d; }
		d.dx = p1->x - p0->x;
		d.dz = p1->z - p0->z;
		return d;
	}
	case GizmoPart::Yaw:
	{
		const auto p0 = hitPlaneY(f.anchor.y, down);
		const auto p1 = hitPlaneY(f.anchor.y, now);
		if (!p0 || !p1) { return d; }
		d.yawDeg = normalizedYaw(yawOf(f.anchor, *p1) - yawOf(f.anchor, *p0));
		return d;
	}
	default: return d;
	}
}

Geom gizmoApply(const Geom& base, GizmoPart part, const GizmoDelta& d, const Snap& snap)
{
	if (part == GizmoPart::None) { return base; }
	if (part == GizmoPart::Yaw)
	{
		if (!base.hasYaw) { return base; }
		Geom out = base;
		out.yaw = normalizedYaw(snapTo(base.yaw + d.yawDeg, snap.angle));
		return out;
	}
	const Vec3 ref = base.shape == Shape::Polyline && !base.pts.empty() ? base.pts.front() : base.a;
	const Vec3 m = gizmoSnappedMove(ref, d, snap);
	return translated(base, m.x, m.y, m.z);
}

Vec3 gizmoSnappedMove(const Vec3& ref, const GizmoDelta& d, const Snap& snap)
{
	return { snapTo(ref.x + d.dx, snap.move) - ref.x, snapTo(ref.y + d.dy, snap.height) - ref.y, snapTo(ref.z + d.dz, snap.move) - ref.z };
}

void drawGizmo(Canvas& canvas, const ViewCam& cam, const GizmoFrame& f, GizmoPart active)
{
	const auto anchorS = cam.project(f.anchor);
	if (!anchorS) { return; }
	if (const auto corners = projectAll(cam, planeCorners(f)); corners.size() == 4)
	{
		const Rgba c = colorOf(GizmoPart::PlaneXZ, active);
		canvas.fillPolygon(corners, Rgba{ c.r, c.g, c.b, 70 });
		for (std::size_t i = 0; i < 4; ++i) { canvas.line(corners[i].first, corners[i].second, corners[(i + 1) % 4].first, corners[(i + 1) % 4].second, c, 1); }
	}
	if (f.hasYaw)
	{
		const Rgba c = colorOf(GizmoPart::Yaw, active);
		const auto ring = ringPoints(f);
		const int thick = active == GizmoPart::Yaw ? 3 : 1;
		for (std::size_t i = 0; i + 1 < ring.size(); ++i) { drawLine3(canvas, cam, ring[i], ring[i + 1], c, thick); }
		drawLine3(canvas, cam, f.anchor, ringPoint(f, f.yawDeg * kDeg), c, thick + 1);
	}
	for (const GizmoPart part : { GizmoPart::AxisX, GizmoPart::AxisY, GizmoPart::AxisZ })
	{
		const Rgba c = colorOf(part, active);
		const Vec3 end = v3add(f.anchor, v3mul(axisOf(part), f.scale));
		drawLine3(canvas, cam, f.anchor, end, c, active == part ? 4 : 2);
		if (const auto endS = cam.project(end)) { canvas.fillCircle(endS->first, endS->second, active == part ? 5.0 : 4.0, c); }
	}
	canvas.fillCircle(anchorS->first, anchorS->second, 3.0, kActive);
}

} // namespace mitiru::tool::stageedit
