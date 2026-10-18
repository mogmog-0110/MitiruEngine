#include "TraceView.hpp"

#include "../ToolPalette.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mitiru::tool::motiontune
{

namespace
{

using stageedit::Canvas;
using stageedit::Rgba;

Rgba rgba(const PaletteRgb& p) { return Rgba{ p[0], p[1], p[2], 255 }; }

const Rgba kPaper = rgba(toolPalette().paper);
const Rgba kGrid = rgba(toolPalette().gridMinor);
const Rgba kFrame = rgba(toolPalette().grid);
const Rgba kInk = rgba(toolPalette().mute);
const Rgba kBefore = rgba(toolPalette().faint);
const Rgba kAfter = rgba(toolPalette().accent);
constexpr double kMinSpanMeters = 4.0;
constexpr int kMargin = 22;
constexpr int kGap = 18;

struct Span
{
	double lo = 0.0;
	double hi = 1.0;
};

struct Panel
{
	int x0 = 0;
	int y0 = 0;
	int x1 = 0;
	int y1 = 0;
	Span u;   ///< 横の範囲
	Span v;   ///< 縦の範囲
	[[nodiscard]] double px(double value) const { return x0 + (value - u.lo) / (u.hi - u.lo) * (x1 - x0); }
	[[nodiscard]] double py(double value) const { return y1 - (value - v.lo) / (v.hi - v.lo) * (y1 - y0); }
};

void grow(Span& s, double value, bool& first)
{
	if (first) { s.lo = s.hi = value; first = false; return; }
	s.lo = std::min(s.lo, value);
	s.hi = std::max(s.hi, value);
}

Span padded(Span s, double minSpan)
{
	double mid = (s.lo + s.hi) * 0.5;
	double span = std::max(s.hi - s.lo, minSpan) * 1.15;
	return { mid - span * 0.5, mid + span * 0.5 };
}

std::string meters(double v)
{
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%.1f", v);
	return buf;
}

void frame(Canvas& c, const Panel& p)
{
	c.fillRect(p.x0, p.y0, p.x1, p.y1, kPaper);
	for (int i = 1; i < 4; ++i)
	{
		const double gx = p.x0 + (p.x1 - p.x0) * i / 4.0;
		const double gy = p.y0 + (p.y1 - p.y0) * i / 4.0;
		c.line(gx, p.y0, gx, p.y1, kGrid);
		c.line(p.x0, gy, p.x1, gy, kGrid);
	}
	c.strokeRect(p.x0, p.y0, p.x1, p.y1, kFrame);
}

void polyline(Canvas& c, const Panel& p, const std::vector<std::pair<double, double>>& pts, Rgba color)
{
	for (std::size_t i = 1; i < pts.size(); ++i)
	{
		c.line(p.px(pts[i - 1].first), p.py(pts[i - 1].second), p.px(pts[i].first), p.py(pts[i].second), color, 2);
	}
	if (!pts.empty())
	{
		c.fillCircle(p.px(pts.front().first), p.py(pts.front().second), 3.5, color);
		c.strokeCircle(p.px(pts.back().first), p.py(pts.back().second), 4.5, color, 2);
	}
}

std::vector<std::pair<double, double>> topPoints(const Trace& t)
{
	std::vector<std::pair<double, double>> out;
	for (const TracePoint& p : t.points) { out.emplace_back(p.x, p.z); }
	return out;
}

std::vector<std::pair<double, double>> heightPoints(const Trace& t)
{
	std::vector<std::pair<double, double>> out;
	const double y0 = t.points.empty() ? 0.0 : t.points.front().y;
	for (std::size_t i = 0; i < t.points.size(); ++i) { out.emplace_back(static_cast<double>(i) * t.dt, t.points[i].y - y0); }
	return out;
}

void apexMark(Canvas& c, const Panel& p, const Trace& t, Rgba color)
{
	if (t.points.empty()) { return; }
	const TraceStats s = statsOf(t);
	const double x = s.apexFrame * t.dt;
	c.line(p.px(x), p.py(s.apexRise), p.px(x), p.y1, color);
	c.fillCircle(p.px(x), p.py(s.apexRise), 3.5, color);
}

} // namespace

bool parseTrace(const nlohmann::json& body, bool hasSpeed, Trace& out, std::string& error)
{
	out = Trace{};
	if (!body.is_object() || !body.contains("rows") || !body["rows"].is_array())
	{
		error = std::string("The response has no rows");
		if (body.is_object() && body.contains("error")) { error = body["error"].is_string() ? body["error"].get<std::string>() : body["error"].dump(); }
		return false;
	}
	const std::size_t need = hasSpeed ? 4 : 3;
	out.dt = body.value("dt", 1.0 / 60.0);
	for (const nlohmann::json& row : body["rows"])
	{
		if (!row.is_array() || row.size() < need) { error = "A row is too short"; return false; }
		for (std::size_t i = 0; i < need; ++i) { if (!row[i].is_number()) { error = "A row has a non-number"; return false; } }
		TracePoint p;
		p.x = row[0].get<double>();
		p.y = row[1].get<double>();
		p.z = row[2].get<double>();
		p.speed = hasSpeed ? row[3].get<double>() : 0.0;
		out.points.push_back(p);
	}
	if (out.points.size() < 2) { error = "The trace has only one point"; return false; }
	return true;
}

TraceStats statsOf(const Trace& trace)
{
	TraceStats s;
	if (trace.points.empty()) { return s; }
	const TracePoint& first = trace.points.front();
	for (std::size_t i = 0; i < trace.points.size(); ++i)
	{
		const double rise = trace.points[i].y - first.y;
		if (rise > s.apexRise) { s.apexRise = rise; s.apexFrame = static_cast<int>(i); }
		s.peakSpeed = std::max(s.peakSpeed, trace.points[i].speed);
	}
	const TracePoint& last = trace.points.back();
	s.travel = std::hypot(last.x - first.x, last.z - first.z);
	s.endRise = last.y - first.y;
	return s;
}

Canvas renderTrace(const Trace* base, const Trace* tuned, int width, int height)
{
	Canvas c(width, height);
	c.clear(kPaper);
	const int half = (width - kMargin * 2 - kGap) / 2;
	Panel top{ kMargin, kMargin, kMargin + half, height - kMargin, {}, {} };
	Panel side{ kMargin + half + kGap, kMargin, width - kMargin, height - kMargin, {}, {} };

	Span ux, uz, ut, uy;
	bool fx = true, fz = true, ft = true, fy = true;
	for (const Trace* t : { base, tuned })
	{
		if (t == nullptr) { continue; }
		const double y0 = t->points.empty() ? 0.0 : t->points.front().y;
		for (std::size_t i = 0; i < t->points.size(); ++i)
		{
			grow(ux, t->points[i].x, fx);
			grow(uz, t->points[i].z, fz);
			grow(ut, static_cast<double>(i) * t->dt, ft);
			grow(uy, t->points[i].y - y0, fy);
		}
	}
	if (fx) { return c; }
	ux = padded(ux, kMinSpanMeters);
	uz = padded(uz, kMinSpanMeters);
	// 足あとが歪まないように、上からの図は縦横の縮尺をそろえる。
	const double scaleX = (ux.hi - ux.lo) / (top.x1 - top.x0);
	const double scaleZ = (uz.hi - uz.lo) / (top.y1 - top.y0);
	const double scale = std::max(scaleX, scaleZ);
	const double cx = (ux.lo + ux.hi) * 0.5, cz = (uz.lo + uz.hi) * 0.5;
	top.u = { cx - scale * (top.x1 - top.x0) * 0.5, cx + scale * (top.x1 - top.x0) * 0.5 };
	top.v = { cz - scale * (top.y1 - top.y0) * 0.5, cz + scale * (top.y1 - top.y0) * 0.5 };
	side.u = { 0.0, std::max(ut.hi, 0.1) };
	side.v = padded({ std::min(uy.lo, 0.0), std::max(uy.hi, 0.0) }, kMinSpanMeters);

	frame(c, top);
	frame(c, side);
	const double zeroY = side.py(0.0);
	c.line(side.x0, zeroY, side.x1, zeroY, kFrame, 1);
	for (const auto& [trace, color] : { std::pair<const Trace*, Rgba>{ base, kBefore }, std::pair<const Trace*, Rgba>{ tuned, kAfter } })
	{
		if (trace == nullptr) { continue; }
		polyline(c, top, topPoints(*trace), color);
		polyline(c, side, heightPoints(*trace), color);
		apexMark(c, side, *trace, color);
	}
	c.digits(top.x0 + 3, top.y1 + 5, meters(top.u.lo), kInk, 1);
	c.digits(top.x1 - Canvas::digitsWidth(meters(top.u.hi), 1) - 1, top.y1 + 5, meters(top.u.hi), kInk, 1);
	c.digits(side.x1 - Canvas::digitsWidth(meters(side.u.hi), 1) - 1, side.y1 + 5, meters(side.u.hi), kInk, 1);
	c.digits(side.x0 - 1 - Canvas::digitsWidth(meters(side.v.hi), 1), side.y0, meters(side.v.hi), kInk, 1);
	c.digits(side.x0 - 1 - Canvas::digitsWidth(meters(side.v.lo), 1), side.y1 - 5, meters(side.v.lo), kInk, 1);
	return c;
}

} // namespace mitiru::tool::motiontune
