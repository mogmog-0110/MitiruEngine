#include "Canvas.hpp"

#include <algorithm>
#include <cmath>

namespace mitiru::tool::stageedit
{

namespace
{

// 3 列 x 5 行。列は上位ビットから
constexpr std::uint8_t kGlyphs[12][5] = {
	{ 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 }, { 7, 1, 7, 4, 7 }, { 7, 1, 7, 1, 7 }, { 5, 5, 7, 1, 1 },
	{ 7, 4, 7, 1, 7 }, { 7, 4, 7, 5, 7 }, { 7, 1, 1, 1, 1 }, { 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 },
	{ 0, 0, 7, 0, 0 }, { 0, 0, 0, 0, 2 } };

int glyphIndex(char ch)
{
	if (ch >= '0' && ch <= '9') { return ch - '0'; }
	if (ch == '-') { return 10; }
	if (ch == '.') { return 11; }
	return -1;
}

} // namespace

Canvas::Canvas(int width, int height)
	: m_w(std::max(1, width)), m_h(std::max(1, height)), m_px(static_cast<std::size_t>(m_w) * static_cast<std::size_t>(m_h) * 4, 0)
{
}

Rgba Canvas::at(int x, int y) const
{
	if (x < 0 || y < 0 || x >= m_w || y >= m_h) { return {}; }
	const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(m_w) + static_cast<std::size_t>(x)) * 4;
	return Rgba{ m_px[i], m_px[i + 1], m_px[i + 2], m_px[i + 3] };
}

void Canvas::clear(Rgba c)
{
	for (std::size_t i = 0; i + 3 < m_px.size(); i += 4) { m_px[i] = c.r; m_px[i + 1] = c.g; m_px[i + 2] = c.b; m_px[i + 3] = c.a; }
}

void Canvas::blend(int x, int y, Rgba c)
{
	if (x < 0 || y < 0 || x >= m_w || y >= m_h || c.a == 0) { return; }
	const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(m_w) + static_cast<std::size_t>(x)) * 4;
	if (c.a == 255) { m_px[i] = c.r; m_px[i + 1] = c.g; m_px[i + 2] = c.b; m_px[i + 3] = 255; return; }
	const int a = c.a;
	m_px[i] = static_cast<std::uint8_t>((c.r * a + m_px[i] * (255 - a)) / 255);
	m_px[i + 1] = static_cast<std::uint8_t>((c.g * a + m_px[i + 1] * (255 - a)) / 255);
	m_px[i + 2] = static_cast<std::uint8_t>((c.b * a + m_px[i + 2] * (255 - a)) / 255);
	m_px[i + 3] = 255;
}

void Canvas::line(double x0, double y0, double x1, double y1, Rgba c, int thickness)
{
	const double dx = x1 - x0;
	const double dy = y1 - y0;
	const int steps = std::max(1, static_cast<int>(std::ceil(std::max(std::fabs(dx), std::fabs(dy)))));
	if (steps > 20000) { return; }
	const int half = thickness / 2;
	for (int s = 0; s <= steps; ++s)
	{
		const double t = static_cast<double>(s) / steps;
		const int px = static_cast<int>(std::lround(x0 + dx * t));
		const int py = static_cast<int>(std::lround(y0 + dy * t));
		for (int oy = -half; oy < thickness - half; ++oy)
		{
			for (int ox = -half; ox < thickness - half; ++ox) { blend(px + ox, py + oy, c); }
		}
	}
}

void Canvas::fillRect(double x0, double y0, double x1, double y1, Rgba c)
{
	const int ax = std::max(0, static_cast<int>(std::floor(std::min(x0, x1))));
	const int bx = std::min(m_w - 1, static_cast<int>(std::ceil(std::max(x0, x1))) - 1);
	const int ay = std::max(0, static_cast<int>(std::floor(std::min(y0, y1))));
	const int by = std::min(m_h - 1, static_cast<int>(std::ceil(std::max(y0, y1))) - 1);
	for (int y = ay; y <= by; ++y) { for (int x = ax; x <= bx; ++x) { blend(x, y, c); } }
}

void Canvas::strokeRect(double x0, double y0, double x1, double y1, Rgba c, int thickness)
{
	line(x0, y0, x1, y0, c, thickness);
	line(x1, y0, x1, y1, c, thickness);
	line(x1, y1, x0, y1, c, thickness);
	line(x0, y1, x0, y0, c, thickness);
}

void Canvas::fillCircle(double cx, double cy, double r, Rgba c)
{
	const int ay = std::max(0, static_cast<int>(std::floor(cy - r)));
	const int by = std::min(m_h - 1, static_cast<int>(std::ceil(cy + r)));
	for (int y = ay; y <= by; ++y)
	{
		const double h = r * r - (y + 0.5 - cy) * (y + 0.5 - cy);
		if (h < 0.0) { continue; }
		const double w = std::sqrt(h);
		const int ax = std::max(0, static_cast<int>(std::floor(cx - w)));
		const int bx = std::min(m_w - 1, static_cast<int>(std::ceil(cx + w)));
		for (int x = ax; x <= bx; ++x) { blend(x, y, c); }
	}
}

void Canvas::strokeCircle(double cx, double cy, double r, Rgba c, int thickness)
{
	if (r <= 0.0 || r > 20000.0) { return; }
	const int segments = std::clamp(static_cast<int>(r * 0.8), 16, 360);
	constexpr double kTau = 6.28318530717958647692;
	for (int i = 0; i < segments; ++i)
	{
		const double a0 = kTau * i / segments;
		const double a1 = kTau * (i + 1) / segments;
		line(cx + std::cos(a0) * r, cy + std::sin(a0) * r, cx + std::cos(a1) * r, cy + std::sin(a1) * r, c, thickness);
	}
}

void Canvas::fillPolygon(const std::vector<std::pair<double, double>>& pts, Rgba c)
{
	if (pts.size() < 3) { return; }
	double lo = pts[0].second;
	double hi = pts[0].second;
	for (const auto& p : pts) { lo = std::min(lo, p.second); hi = std::max(hi, p.second); }
	const int ay = std::max(0, static_cast<int>(std::floor(lo)));
	const int by = std::min(m_h - 1, static_cast<int>(std::ceil(hi)));
	std::vector<double> xs;
	for (int y = ay; y <= by; ++y)
	{
		const double sy = y + 0.5;
		xs.clear();
		for (std::size_t i = 0; i < pts.size(); ++i)
		{
			const auto& p = pts[i];
			const auto& q = pts[(i + 1) % pts.size()];
			if ((p.second <= sy && q.second > sy) || (q.second <= sy && p.second > sy))
			{
				xs.push_back(p.first + (sy - p.second) / (q.second - p.second) * (q.first - p.first));
			}
		}
		std::sort(xs.begin(), xs.end());
		for (std::size_t i = 0; i + 1 < xs.size(); i += 2)
		{
			const int ax = std::max(0, static_cast<int>(std::floor(xs[i])));
			const int bx = std::min(m_w - 1, static_cast<int>(std::ceil(xs[i + 1])) - 1);
			for (int x = ax; x <= bx; ++x) { blend(x, y, c); }
		}
	}
}

int Canvas::digitsWidth(std::string_view text, int scale)
{
	return text.empty() ? 0 : static_cast<int>(text.size()) * 4 * scale - scale;
}

void Canvas::digits(int x, int y, std::string_view text, Rgba c, int scale)
{
	int pen = x;
	for (const char ch : text)
	{
		const int g = glyphIndex(ch);
		if (g >= 0)
		{
			for (int row = 0; row < 5; ++row)
			{
				for (int col = 0; col < 3; ++col)
				{
					if ((kGlyphs[g][row] >> (2 - col) & 1) == 0) { continue; }
					fillRect(pen + col * scale, y + row * scale, pen + (col + 1) * scale, y + (row + 1) * scale, c);
				}
			}
		}
		pen += 4 * scale;
	}
}

} // namespace mitiru::tool::stageedit
