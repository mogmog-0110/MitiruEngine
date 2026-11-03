#include "OrbitView.hpp"

#include "../ToolPalette.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mitiru::tool::stageedit
{

namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kFovDeg = 50.0;
constexpr double kNear = 0.3;
Rgba rgba(const PaletteRgb& p) { return Rgba{ p[0], p[1], p[2], 255 }; }

const Rgba kBackground = rgba(toolPalette().paper);
const Rgba kGround = rgba(toolPalette().grid);
const Rgba kHighlight = rgba(toolPalette().highlight);

int shapeRank(Shape s)
{
	switch (s)
	{
	case Shape::Point: return 0;
	case Shape::Segment:
	case Shape::Polyline: return 1;
	default: return 2;
	}
}

std::vector<Vec3> corners(const Geom& g, double y0, double y1)
{
	std::vector<Vec3> out;
	if (g.shape == Shape::Box)
	{
		const Aabb b = boundsOf(g);
		out = { { b.lo.x, b.lo.y, b.lo.z }, { b.hi.x, b.lo.y, b.lo.z }, { b.hi.x, b.lo.y, b.hi.z }, { b.lo.x, b.lo.y, b.hi.z },
		        { b.lo.x, b.hi.y, b.lo.z }, { b.hi.x, b.hi.y, b.lo.z }, { b.hi.x, b.hi.y, b.hi.z }, { b.lo.x, b.hi.y, b.hi.z } };
	}
	else if (g.shape == Shape::OrientedBox)
	{
		for (const double y : { g.a.y, g.a.y + g.b.y }) { for (const auto& [x, z] : orientedCorners(g)) { out.push_back({ x, y, z }); } }
	}
	else if (g.shape == Shape::Wall)
	{
		out = { { g.a.x, y0, g.a.z }, { g.b.x, y0, g.b.z }, { g.b.x, y1, g.b.z }, { g.a.x, y1, g.a.z } };
	}
	return out;
}

} // namespace

OrbitView::OrbitView(const Schema& schema, int width, int height)
	: m_schema(&schema), m_w(std::max(64, width)), m_h(std::max(64, height))
{
}

ViewCam OrbitView::view() const
{
	const double az = m_azimuth * kDeg;
	const double p = m_pitch * kDeg;
	ViewCam c;
	c.pos = { m_target.x + m_dist * std::sin(az) * std::cos(p), m_target.y + m_dist * std::sin(p), m_target.z - m_dist * std::cos(az) * std::cos(p) };
	c.fwd = v3unit(v3sub(m_target, c.pos));
	c.right = v3unit(v3cross(c.fwd, Vec3{ 0, 1, 0 }));
	c.up = v3cross(c.right, c.fwd);
	c.focal = (m_h * 0.5) / std::tan(kFovDeg * 0.5 * kDeg);
	c.w = m_w;
	c.h = m_h;
	c.nearZ = kNear;
	return c;
}

void OrbitView::frame(const StageDoc& doc)
{
	Aabb all{ { std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max() },
	          { std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest() } };
	for (const Item& item : doc.items())
	{
		const Aabb b = boundsOf(item.geom);
		all.lo = { std::min(all.lo.x, b.lo.x), std::min(all.lo.y, b.lo.y), std::min(all.lo.z, b.lo.z) };
		all.hi = { std::max(all.hi.x, b.hi.x), std::max(all.hi.y, b.hi.y), std::max(all.hi.z, b.hi.z) };
	}
	if (all.lo.x > all.hi.x) { m_target = {}; m_dist = 60.0; return; }
	m_target = { (all.lo.x + all.hi.x) * 0.5, (all.lo.y + all.hi.y) * 0.5, (all.lo.z + all.hi.z) * 0.5 };
	const double radius = 0.5 * v3len(v3sub(all.hi, all.lo));
	m_dist = std::max(12.0, radius / std::sin(kFovDeg * 0.5 * kDeg) * 0.8);
}

void OrbitView::orbit(double dx, double dy)
{
	m_azimuth = std::fmod(m_azimuth - dx * 0.4, 360.0);
	m_pitch = std::clamp(m_pitch + dy * 0.4, -85.0, 85.0);
}

void OrbitView::zoom(double factor)
{
	m_dist = std::clamp(m_dist * factor, 3.0, 5000.0);
}

void OrbitView::drawGeom(Canvas& canvas, const ViewCam& cam, const Geom& g, Rgba color, int thickness, bool fillFaces) const
{
	const auto cs = corners(g, g.bottom, g.top);
	if (cs.size() == 8 || cs.size() == 4)
	{
		static constexpr int kEdges8[12][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
		static constexpr int kEdges4[4][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 } };
		const int n = cs.size() == 8 ? 12 : 4;
		for (int i = 0; i < n; ++i)
		{
			const int* e = cs.size() == 8 ? kEdges8[i] : kEdges4[i];
			drawLine3(canvas, cam, cs[static_cast<std::size_t>(e[0])], cs[static_cast<std::size_t>(e[1])], color, thickness);
		}
		if (fillFaces)
		{
			std::vector<std::pair<double, double>> top;
			bool visible = true;
			for (std::size_t i = cs.size() == 8 ? 4 : 0; i < cs.size(); ++i)
			{
				const auto s = cam.project(cs[i]);
				if (!s) { visible = false; break; }
				top.push_back(*s);
			}
			if (visible) { canvas.fillPolygon(top, Rgba{ color.r, color.g, color.b, 60 }); }
		}
		return;
	}
	switch (g.shape)
	{
	case Shape::Point:
	{
		const double r = g.hasRadius ? g.radius : 0.5;
		drawLine3(canvas, cam, { g.a.x - r, g.a.y, g.a.z }, { g.a.x + r, g.a.y, g.a.z }, color, thickness);
		drawLine3(canvas, cam, { g.a.x, g.a.y, g.a.z - r }, { g.a.x, g.a.y, g.a.z + r }, color, thickness);
		drawLine3(canvas, cam, { g.a.x, g.a.y - r, g.a.z }, { g.a.x, g.a.y + r, g.a.z }, color, thickness);
		if (g.hasRadius && g.radius > 0.0)
		{
			for (int i = 0; i < 32; ++i)
			{
				const double a0 = 2.0 * kPi * i / 32.0;
				const double a1 = 2.0 * kPi * (i + 1) / 32.0;
				drawLine3(canvas, cam, { g.a.x + std::cos(a0) * r, g.a.y, g.a.z + std::sin(a0) * r }, { g.a.x + std::cos(a1) * r, g.a.y, g.a.z + std::sin(a1) * r }, color, thickness);
			}
		}
		if (g.hasYaw)
		{
			const double yr = g.yaw * kDeg;
			drawLine3(canvas, cam, g.a, { g.a.x + std::sin(yr) * 3.0, g.a.y, g.a.z + std::cos(yr) * 3.0 }, color, thickness + 1);
			drawLine3(canvas, cam, g.a, { g.a.x, g.a.y + 3.0, g.a.z }, color, thickness);
		}
		break;
	}
	case Shape::Segment:
		drawLine3(canvas, cam, g.a, g.b, color, thickness + 1);
		break;
	case Shape::Polyline:
		for (std::size_t i = 0; i + 1 < g.pts.size(); ++i) { drawLine3(canvas, cam, g.pts[i], g.pts[i + 1], color, thickness + 1); }
		break;
	default:
		break;
	}
}

Canvas OrbitView::render(const StageDoc& doc, GizmoPart active) const
{
	Canvas canvas(m_w, m_h);
	canvas.clear(kBackground);
	const ViewCam cam = view();
	double groundY = std::numeric_limits<double>::max();
	double x0 = groundY, x1 = std::numeric_limits<double>::lowest(), z0 = groundY, z1 = x1;
	for (const Item& item : doc.items())
	{
		const Aabb b = boundsOf(item.geom);
		groundY = std::min(groundY, b.lo.y);
		x0 = std::min(x0, b.lo.x); x1 = std::max(x1, b.hi.x);
		z0 = std::min(z0, b.lo.z); z1 = std::max(z1, b.hi.z);
	}
	if (x0 <= x1)
	{
		const double span = std::max(x1 - x0, z1 - z0);
		static constexpr double kSteps[] = { 1, 2, 5, 10, 20, 50, 100, 200, 500 };
		double step = 500.0;
		for (const double s : kSteps) { if (span / s <= 30.0) { step = s; break; } }
		const double gx0 = std::floor((x0 - step) / step) * step, gx1 = std::ceil((x1 + step) / step) * step;
		const double gz0 = std::floor((z0 - step) / step) * step, gz1 = std::ceil((z1 + step) / step) * step;
		for (double x = gx0; x <= gx1; x += step) { drawLine3(canvas, cam, { x, groundY, gz0 }, { x, groundY, gz1 }, kGround, 1); }
		for (double z = gz0; z <= gz1; z += step) { drawLine3(canvas, cam, { gx0, groundY, z }, { gx1, groundY, z }, kGround, 1); }
	}
	for (std::size_t i = 0; i < doc.items().size(); ++i)
	{
		if (static_cast<int>(i) == doc.selected()) { continue; }
		const Item& item = doc.items()[i];
		const Rgb k = doc.schema().kinds[static_cast<std::size_t>(item.kind)].color;
		drawGeom(canvas, cam, item.geom, Rgba{ k.r, k.g, k.b, 230 }, 1, true);
	}
	if (doc.selected() >= 0)
	{
		drawGeom(canvas, cam, doc.items()[static_cast<std::size_t>(doc.selected())].geom, kHighlight, 2, true);
		if (const auto f = gizmoFrame(doc)) { drawGizmo(canvas, cam, *f, active); }
	}
	return canvas;
}

std::optional<GizmoFrame> OrbitView::gizmoFrame(const StageDoc& doc) const
{
	if (doc.selected() < 0) { return std::nullopt; }
	return gizmoFrameOf(doc.items()[static_cast<std::size_t>(doc.selected())].geom, view());
}

GizmoPart OrbitView::pickGizmo(const StageDoc& doc, double sx, double sy) const
{
	const auto f = gizmoFrame(doc);
	return f ? stageedit::pickGizmo(*f, view(), sx, sy) : GizmoPart::None;
}

int OrbitView::pick(const StageDoc& doc, double sx, double sy) const
{
	const ViewCam cam = view();
	int best = -1;
	int bestRank = 99;
	double bestScore = 0.0;
	for (std::size_t i = 0; i < doc.items().size(); ++i)
	{
		const Geom& g = doc.items()[i].geom;
		std::vector<Vec3> pts = corners(g, g.bottom, g.top);
		const bool solid = !pts.empty();
		if (!solid)
		{
			pts = g.shape == Shape::Polyline ? g.pts : std::vector<Vec3>{ g.a };
			if (g.shape == Shape::Segment) { pts.push_back(g.b); }
		}
		std::vector<std::pair<double, double>> sp;
		bool behind = false;
		for (const Vec3& p : pts)
		{
			const auto s = cam.project(p);
			if (!s) { behind = true; break; }
			sp.push_back(*s);
		}
		if (behind || sp.empty()) { continue; }
		double score = 0.0;
		if (solid)
		{
			double lx = 1e18, ly = 1e18, hx = -1e18, hy = -1e18;
			for (const auto& p : sp) { lx = std::min(lx, p.first); hx = std::max(hx, p.first); ly = std::min(ly, p.second); hy = std::max(hy, p.second); }
			if (sx < lx || sx > hx || sy < ly || sy > hy) { continue; }
			score = (hx - lx) * (hy - ly);
		}
		else
		{
			double d = 1e18;
			for (std::size_t k = 0; k < sp.size(); ++k)
			{
				const auto& p = sp[k];
				const auto& q = sp[std::min(k + 1, sp.size() - 1)];
				const double ex = q.first - p.first, ey = q.second - p.second;
				const double l2 = ex * ex + ey * ey;
				const double t = l2 < 1e-9 ? 0.0 : std::clamp(((sx - p.first) * ex + (sy - p.second) * ey) / l2, 0.0, 1.0);
				d = std::min(d, std::hypot(sx - (p.first + ex * t), sy - (p.second + ey * t)));
			}
			if (d > 8.0) { continue; }
			score = d;
		}
		const int rank = shapeRank(g.shape);
		if (best < 0 || rank < bestRank || (rank == bestRank && score < bestScore)) { best = static_cast<int>(i); bestRank = rank; bestScore = score; }
	}
	return best;
}

} // namespace mitiru::tool::stageedit
