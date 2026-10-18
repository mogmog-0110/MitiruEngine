#include "TopView.hpp"

#include "../ToolPalette.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace mitiru::tool::stageedit
{

namespace
{

Rgba rgba(const PaletteRgb& p) { return Rgba{ p[0], p[1], p[2], 255 }; }

const Rgba kBackground = rgba(toolPalette().paper);
const Rgba kGridMinor = rgba(toolPalette().gridMinor);
const Rgba kGridMajor = rgba(toolPalette().grid);
const Rgba kAxis = rgba(toolPalette().edge);
const Rgba kLabel = rgba(toolPalette().mute);
const Rgba kHighlight = rgba(toolPalette().highlight);
constexpr Rgba kHandleFill{ 255, 255, 255, 255 };
constexpr Rgba kHandleEdge{ 30, 32, 36, 255 };
constexpr double kPi = 3.14159265358979323846;
constexpr double kPickPixels = 7.0;
constexpr double kHandlePixels = 8.0;
constexpr double kMinPpm = 0.05;
constexpr double kMaxPpm = 400.0;

using Pt = std::pair<double, double>;

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

Rgba withAlpha(Rgb c, int a, double shade = 1.0)
{
	const auto ch = [&](unsigned char v) { return static_cast<std::uint8_t>(std::clamp(static_cast<int>(v * shade), 0, 255)); };
	return Rgba{ ch(c.r), ch(c.g), ch(c.b), static_cast<std::uint8_t>(std::clamp(a, 0, 255)) };
}

} // namespace

TopView::TopView(const Schema& schema, int width, int height)
	: m_schema(&schema), m_w(std::max(64, width)), m_h(std::max(64, height))
{
}

Pt TopView::toScreen(double x, double z) const
{
	const double rs = m_schema->top.rightIsNegX ? -1.0 : 1.0;
	const double us = m_schema->top.upIsNegZ ? -1.0 : 1.0;
	return { m_w * 0.5 + rs * (x - m_cx) * m_ppm, m_h * 0.5 - us * (z - m_cz) * m_ppm };
}

Pt TopView::toWorld(double sx, double sy) const
{
	const double rs = m_schema->top.rightIsNegX ? -1.0 : 1.0;
	const double us = m_schema->top.upIsNegZ ? -1.0 : 1.0;
	return { m_cx + rs * (sx - m_w * 0.5) / m_ppm, m_cz - us * (sy - m_h * 0.5) / m_ppm };
}

void TopView::fit(const StageDoc& doc)
{
	double x0 = std::numeric_limits<double>::max(), x1 = std::numeric_limits<double>::lowest();
	double z0 = x0, z1 = x1;
	for (const Item& item : doc.items())
	{
		const Aabb b = boundsOf(item.geom);
		x0 = std::min(x0, b.lo.x); x1 = std::max(x1, b.hi.x);
		z0 = std::min(z0, b.lo.z); z1 = std::max(z1, b.hi.z);
	}
	if (x0 > x1) { m_cx = 0.0; m_cz = 0.0; m_ppm = 4.0; return; }
	m_cx = (x0 + x1) * 0.5;
	m_cz = (z0 + z1) * 0.5;
	const double spanX = std::max(x1 - x0, 4.0) * 1.1;
	const double spanZ = std::max(z1 - z0, 4.0) * 1.1;
	m_ppm = std::clamp(std::min(m_w / spanX, m_h / spanZ), kMinPpm, kMaxPpm);
}

void TopView::zoomAt(double sx, double sy, double factor)
{
	const auto [wx, wz] = toWorld(sx, sy);
	m_ppm = std::clamp(m_ppm * factor, kMinPpm, kMaxPpm);
	const auto [nx, nz] = toWorld(sx, sy);
	m_cx += wx - nx;
	m_cz += wz - nz;
}

void TopView::panPixels(double dx, double dy)
{
	const double rs = m_schema->top.rightIsNegX ? -1.0 : 1.0;
	const double us = m_schema->top.upIsNegZ ? -1.0 : 1.0;
	m_cx -= rs * dx / m_ppm;
	m_cz += us * dy / m_ppm;
}

double TopView::gridStep() const
{
	static constexpr double kSteps[] = { 0.5, 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000 };
	for (const double s : kSteps) { if (s * m_ppm >= 44.0) { return s; } }
	return 1000.0;
}

void TopView::drawGrid(Canvas& canvas) const
{
	const double step = gridStep();
	const double minor = step / 5.0;
	const auto [wx0, wz0] = toWorld(0, 0);
	const auto [wx1, wz1] = toWorld(m_w, m_h);
	const double xLo = std::min(wx0, wx1), xHi = std::max(wx0, wx1);
	const double zLo = std::min(wz0, wz1), zHi = std::max(wz0, wz1);
	const auto lines = [&](double lo, double hi, double gap, bool vertical, Rgba color) {
		if (gap * m_ppm < 7.0 || (hi - lo) / gap > 800.0) { return; }
		for (double v = std::ceil(lo / gap) * gap; v <= hi; v += gap)
		{
			const Pt p = vertical ? toScreen(v, 0.0) : toScreen(0.0, v);
			if (vertical) { canvas.line(std::round(p.first), 0, std::round(p.first), m_h, color); }
			else { canvas.line(0, std::round(p.second), m_w, std::round(p.second), color); }
		}
	};
	lines(xLo, xHi, minor, true, kGridMinor);
	lines(zLo, zHi, minor, false, kGridMinor);
	lines(xLo, xHi, step, true, kGridMajor);
	lines(zLo, zHi, step, false, kGridMajor);
	const Pt origin = toScreen(0.0, 0.0);
	canvas.line(std::round(origin.first), 0, std::round(origin.first), m_h, kAxis);
	canvas.line(0, std::round(origin.second), m_w, std::round(origin.second), kAxis);
	char buf[32];
	for (double v = std::ceil(xLo / step) * step; v <= xHi && (xHi - xLo) / step < 200.0; v += step)
	{
		std::snprintf(buf, sizeof(buf), "%g", v);
		canvas.digits(static_cast<int>(toScreen(v, 0.0).first) + 3, 3, buf, kLabel);
	}
	for (double v = std::ceil(zLo / step) * step; v <= zHi && (zHi - zLo) / step < 200.0; v += step)
	{
		std::snprintf(buf, sizeof(buf), "%g", v);
		canvas.digits(3, static_cast<int>(toScreen(0.0, v).second) + 3, buf, kLabel);
	}
}

namespace
{

void arrow(Canvas& canvas, const Pt& from, const Pt& to, Rgba color, int thickness)
{
	canvas.line(from.first, from.second, to.first, to.second, color, thickness);
	const double ang = std::atan2(to.second - from.second, to.first - from.first);
	for (const double side : { 2.6, -2.6 })
	{
		canvas.line(to.first, to.second, to.first - std::cos(ang + side * 0.3) * 8.0, to.second - std::sin(ang + side * 0.3) * 8.0, color, thickness);
	}
}

} // namespace

void TopView::drawItem(Canvas& canvas, const StageDoc& doc, const Item& item, bool selected, double yLow, double yHigh) const
{
	const Kind& kind = doc.schema().kinds[static_cast<std::size_t>(item.kind)];
	const Geom& g = item.geom;
	const Aabb b = boundsOf(g);
	const double t = yHigh > yLow ? std::clamp((b.hi.y - yLow) / (yHigh - yLow), 0.0, 1.0) : 1.0;
	const double shade = 0.7 + 0.3 * t;
	const Rgba fill = withAlpha(kind.color, selected ? 150 : 95, shade);
	const Rgba edge = selected ? kHighlight : withAlpha(kind.color, 235, 0.85);
	const int width = selected ? 2 : 1;
	const auto toS = [&](const Vec3& v) { return toScreen(v.x, v.z); };
	switch (g.shape)
	{
	case Shape::Box:
	{
		const Pt p = toScreen(b.lo.x, b.lo.z);
		const Pt q = toScreen(b.hi.x, b.hi.z);
		canvas.fillRect(p.first, p.second, q.first, q.second, fill);
		canvas.strokeRect(p.first, p.second, q.first, q.second, edge, width);
		break;
	}
	case Shape::OrientedBox:
	{
		std::vector<Pt> poly;
		for (const auto& [x, z] : orientedCorners(g)) { poly.push_back(toScreen(x, z)); }
		canvas.fillPolygon(poly, fill);
		for (std::size_t i = 0; i < poly.size(); ++i) { canvas.line(poly[i].first, poly[i].second, poly[(i + 1) % poly.size()].first, poly[(i + 1) % poly.size()].second, edge, width); }
		const double r = g.yaw * kPi / 180.0;
		arrow(canvas, toScreen(g.a.x, g.a.z), toScreen(g.a.x + std::sin(r) * g.b.z * 0.4, g.a.z + std::cos(r) * g.b.z * 0.4), edge, width);
		break;
	}
	case Shape::Point:
	{
		const Pt c = toS(g.a);
		if (g.hasRadius && g.radius > 0.0)
		{
			canvas.fillCircle(c.first, c.second, g.radius * m_ppm, withAlpha(kind.color, 40));
			canvas.strokeCircle(c.first, c.second, g.radius * m_ppm, edge, width);
		}
		if (g.hasYaw)
		{
			const double r = g.yaw * kPi / 180.0;
			arrow(canvas, c, toScreen(g.a.x + std::sin(r) * 2.4, g.a.z + std::cos(r) * 2.4), edge, width + 1);
		}
		canvas.fillCircle(c.first, c.second, g.hasYaw || g.hasRadius ? 4.0 : 3.0, withAlpha(kind.color, 255));
		if (selected) { canvas.strokeCircle(c.first, c.second, 6.0, kHighlight, 2); }
		break;
	}
	case Shape::Segment:
	case Shape::Wall:
	{
		const Pt p = toS(g.a);
		const Pt q = toS(g.b);
		canvas.line(p.first, p.second, q.first, q.second, edge, width + 2);
		canvas.fillCircle(p.first, p.second, 3.0, withAlpha(kind.color, 255));
		canvas.fillCircle(q.first, q.second, 3.0, withAlpha(kind.color, 255));
		break;
	}
	case Shape::Polyline:
		for (std::size_t i = 0; i < g.pts.size(); ++i)
		{
			const Pt p = toS(g.pts[i]);
			if (i + 1 < g.pts.size())
			{
				const Pt q = toS(g.pts[i + 1]);
				canvas.line(p.first, p.second, q.first, q.second, edge, width + 1);
			}
			canvas.fillCircle(p.first, p.second, 2.5, withAlpha(kind.color, 255));
		}
		break;
	}
}

Canvas TopView::render(const StageDoc& doc) const
{
	Canvas canvas(m_w, m_h);
	canvas.clear(kBackground);
	drawGrid(canvas);
	double yLow = std::numeric_limits<double>::max();
	double yHigh = std::numeric_limits<double>::lowest();
	for (const Item& item : doc.items())
	{
		const Aabb b = boundsOf(item.geom);
		yLow = std::min(yLow, b.lo.y);
		yHigh = std::max(yHigh, b.hi.y);
	}
	std::vector<const Item*> order;
	for (const Item& item : doc.items()) { order.push_back(&item); }
	std::stable_sort(order.begin(), order.end(), [](const Item* a, const Item* c) {
		const int ra = shapeRank(a->geom.shape);
		const int rc = shapeRank(c->geom.shape);
		if (ra != rc) { return ra > rc; }
		double aa = 0.0, ac = 0.0;
		(void)distanceTo(a->geom, 0.0, 0.0, &aa);
		(void)distanceTo(c->geom, 0.0, 0.0, &ac);
		return aa > ac;
	});
	const Item* chosen = doc.selected() >= 0 ? &doc.items()[static_cast<std::size_t>(doc.selected())] : nullptr;
	for (const Item* item : order) { if (item != chosen) { drawItem(canvas, doc, *item, false, yLow, yHigh); } }
	if (chosen != nullptr)
	{
		drawItem(canvas, doc, *chosen, true, yLow, yHigh);
		for (const Handle& h : handlesOf(chosen->geom))
		{
			const Pt p = toScreen(h.x, h.z);
			canvas.fillRect(p.first - 4, p.second - 4, p.first + 4, p.second + 4, kHandleFill);
			canvas.strokeRect(p.first - 4, p.second - 4, p.first + 4, p.second + 4, kHandleEdge, 1);
		}
	}
	return canvas;
}

int TopView::pick(const StageDoc& doc, double sx, double sy) const
{
	const auto [wx, wz] = toWorld(sx, sy);
	const double tol = kPickPixels / m_ppm;
	int best = -1;
	double bestD = 0.0;
	double bestArea = 0.0;
	int bestRank = 99;
	for (std::size_t i = 0; i < doc.items().size(); ++i)
	{
		double area = 0.0;
		const Geom& g = doc.items()[i].geom;
		const double d = distanceTo(g, wx, wz, &area);
		if (d > tol) { continue; }
		const int rank = shapeRank(g.shape);
		const bool better = best < 0 || rank < bestRank || (rank == bestRank && (d < bestD - 1e-9 || (std::fabs(d - bestD) <= 1e-9 && area < bestArea)));
		if (better) { best = static_cast<int>(i); bestD = d; bestArea = area; bestRank = rank; }
	}
	return best;
}

std::optional<Handle> TopView::pickHandle(const StageDoc& doc, int selected, double sx, double sy) const
{
	if (selected < 0 || selected >= static_cast<int>(doc.items().size())) { return std::nullopt; }
	std::optional<Handle> best;
	double bestD = kHandlePixels;
	for (const Handle& h : handlesOf(doc.items()[static_cast<std::size_t>(selected)].geom))
	{
		const Pt p = toScreen(h.x, h.z);
		const double d = std::hypot(p.first - sx, p.second - sy);
		if (d <= bestD) { bestD = d; best = h; }
	}
	return best;
}

} // namespace mitiru::tool::stageedit
