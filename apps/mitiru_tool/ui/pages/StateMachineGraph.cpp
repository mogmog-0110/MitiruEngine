#include "StateMachineGraph.hpp"

#include "../ToolPalette.hpp"

#include <algorithm>
#include <cmath>

namespace mitiru::tool
{

namespace
{

constexpr float kPi = 3.14159265358979f;

struct Rgb
{
	float r, g, b;
};

Rgb rgbf(const PaletteRgb& p)
{
	return { static_cast<float>(p[0]) / 255.0f, static_cast<float>(p[1]) / 255.0f, static_cast<float>(p[2]) / 255.0f };
}

const Rgb kPaper = rgbf(toolPalette().paper);

struct Stroke
{
	Rgb color;
	float width;   // dp
};

Stroke strokeFor(const GraphEdge& e)
{
	const float heavy = std::min(2.0f, std::log10(1.0f + static_cast<float>(e.count)) * 0.9f);
	switch (e.look)
	{
	case EdgeLook::Idle: return { rgbf(toolPalette().fill), 1.0f };
	case EdgeLook::Seen: return { rgbf(toolPalette().faint), 1.0f + heavy };
	case EdgeLook::Recent: return { rgbf(toolPalette().mute), 1.5f + heavy };
	case EdgeLook::Outgoing: return { rgbf(toolPalette().accent), 1.4f + heavy };
	default: return { rgbf(toolPalette().critical), 1.8f + heavy };
	}
}

int drawOrder(EdgeLook l)
{
	switch (l)
	{
	case EdgeLook::Idle: return 0;
	case EdgeLook::Seen: return 1;
	case EdgeLook::Recent: return 2;
	case EdgeLook::Outgoing: return 3;
	default: return 4;
	}
}

struct Canvas
{
	GraphImage& image;
	float scale;

	void blend(int x, int y, float coverage, Rgb c)
	{
		if (x < 0 || y < 0 || x >= image.width || y >= image.height || coverage <= 0.0f) { return; }
		const float a = std::min(coverage, 1.0f);
		std::uint8_t* p = &image.rgba[(static_cast<std::size_t>(y) * image.width + x) * 4];
		const float rgb[3] = { c.r, c.g, c.b };
		for (int i = 0; i < 3; ++i)
		{
			p[i] = static_cast<std::uint8_t>(std::lround((p[i] / 255.0f * (1.0f - a) + rgb[i] * a) * 255.0f));
		}
	}

	void line(float x0, float y0, float x1, float y1, float widthDp, Rgb c)
	{
		const float half = widthDp * scale * 0.5f;
		x0 *= scale; y0 *= scale; x1 *= scale; y1 *= scale;
		const float dx = x1 - x0, dy = y1 - y0;
		const float len2 = std::max(dx * dx + dy * dy, 1e-6f);
		const int minX = static_cast<int>(std::floor(std::min(x0, x1) - half - 1.0f));
		const int maxX = static_cast<int>(std::ceil(std::max(x0, x1) + half + 1.0f));
		const int minY = static_cast<int>(std::floor(std::min(y0, y1) - half - 1.0f));
		const int maxY = static_cast<int>(std::ceil(std::max(y0, y1) + half + 1.0f));
		for (int y = minY; y <= maxY; ++y)
		{
			for (int x = minX; x <= maxX; ++x)
			{
				const float px = x + 0.5f - x0, py = y + 0.5f - y0;
				const float t = std::clamp((px * dx + py * dy) / len2, 0.0f, 1.0f);
				const float dist = std::hypot(px - t * dx, py - t * dy);
				blend(x, y, half + 0.5f - dist, c);
			}
		}
	}

	void arrow(float tx, float ty, float ux, float uy, float sizeDp, Rgb c)
	{
		const float bx = tx - ux * sizeDp, by = ty - uy * sizeDp;
		const float nx = -uy * sizeDp * 0.45f, ny = ux * sizeDp * 0.45f;
		const float ax[3] = { tx * scale, (bx + nx) * scale, (bx - nx) * scale };
		const float ay[3] = { ty * scale, (by + ny) * scale, (by - ny) * scale };
		const int minX = static_cast<int>(std::floor(*std::min_element(ax, ax + 3)));
		const int maxX = static_cast<int>(std::ceil(*std::max_element(ax, ax + 3)));
		const int minY = static_cast<int>(std::floor(*std::min_element(ay, ay + 3)));
		const int maxY = static_cast<int>(std::ceil(*std::max_element(ay, ay + 3)));
		const float area = (ax[1] - ax[0]) * (ay[2] - ay[0]) - (ax[2] - ax[0]) * (ay[1] - ay[0]);
		if (std::fabs(area) < 1e-6f) { return; }
		for (int y = minY; y <= maxY; ++y)
		{
			for (int x = minX; x <= maxX; ++x)
			{
				const float px = x + 0.5f, py = y + 0.5f;
				const float w0 = ((ax[1] - px) * (ay[2] - py) - (ax[2] - px) * (ay[1] - py)) / area;
				const float w1 = ((ax[2] - px) * (ay[0] - py) - (ax[0] - px) * (ay[2] - py)) / area;
				const float inside = std::min({ w0, w1, 1.0f - w0 - w1 });
				blend(x, y, std::clamp(inside * sizeDp * scale * 3.0f + 0.5f, 0.0f, 1.0f), c);
			}
		}
	}
};

float exitDistance(float ux, float uy, float halfW, float halfH)
{
	const float tx = std::fabs(ux) > 1e-5f ? halfW / std::fabs(ux) : 1e9f;
	const float ty = std::fabs(uy) > 1e-5f ? halfH / std::fabs(uy) : 1e9f;
	return std::min(tx, ty);
}

} // namespace

GraphLayout layoutEllipse(std::size_t n, float width, float height, float boxW, float boxH)
{
	GraphLayout out;
	out.width = width;
	out.height = height;
	out.boxW = boxW;
	out.boxH = boxH;
	const float rx = std::max((width - boxW) * 0.5f - 2.0f, 0.0f);
	const float ry = std::max((height - boxH) * 0.5f - 2.0f, 0.0f);
	for (std::size_t i = 0; i < n; ++i)
	{
		if (n == 1) { out.boxes.push_back({ width * 0.5f, height * 0.5f }); continue; }
		const float a = -kPi * 0.5f + 2.0f * kPi * static_cast<float>(i) / static_cast<float>(n);
		out.boxes.push_back({ width * 0.5f + rx * std::cos(a), height * 0.5f + ry * std::sin(a) });
	}
	return out;
}

GraphImage renderGraphEdges(const GraphLayout& layout, const std::vector<GraphEdge>& edges, float scale)
{
	GraphImage img;
	img.width = std::max(1, static_cast<int>(std::lround(layout.width * scale)));
	img.height = std::max(1, static_cast<int>(std::lround(layout.height * scale)));
	img.rgba.resize(static_cast<std::size_t>(img.width) * img.height * 4);
	for (std::size_t i = 0; i < img.rgba.size(); i += 4)
	{
		img.rgba[i] = static_cast<std::uint8_t>(std::lround(kPaper.r * 255.0f));
		img.rgba[i + 1] = static_cast<std::uint8_t>(std::lround(kPaper.g * 255.0f));
		img.rgba[i + 2] = static_cast<std::uint8_t>(std::lround(kPaper.b * 255.0f));
		img.rgba[i + 3] = 255;
	}
	const int n = static_cast<int>(layout.boxes.size());
	std::vector<const GraphEdge*> order;
	for (const GraphEdge& e : edges)
	{
		if (e.from >= 0 && e.to >= 0 && e.from < n && e.to < n && e.from != e.to) { order.push_back(&e); }
	}
	std::stable_sort(order.begin(), order.end(), [](const GraphEdge* a, const GraphEdge* b) { return drawOrder(a->look) < drawOrder(b->look); });

	Canvas canvas{ img, scale };
	const float halfW = layout.boxW * 0.5f + 1.5f, halfH = layout.boxH * 0.5f + 1.5f;
	for (const GraphEdge* e : order)
	{
		const GraphBox& a = layout.boxes[static_cast<std::size_t>(e->from)];
		const GraphBox& b = layout.boxes[static_cast<std::size_t>(e->to)];
		const float len = std::hypot(b.cx - a.cx, b.cy - a.cy);
		if (len < 1e-3f) { continue; }
		const float ux = (b.cx - a.cx) / len, uy = (b.cy - a.cy) / len;
		const bool paired = std::any_of(edges.begin(), edges.end(), [&](const GraphEdge& o) { return o.from == e->to && o.to == e->from; });
		const float lane = paired ? 3.5f : 0.0f;
		const float ox = uy * lane, oy = -ux * lane;
		const float edge = exitDistance(ux, uy, halfW, halfH);
		if (len - edge <= edge + 2.0f) { continue; }
		const Stroke s = strokeFor(*e);
		const float tipX = a.cx + ux * (len - edge) + ox, tipY = a.cy + uy * (len - edge) + oy;
		const float head = 5.0f + s.width;
		canvas.line(a.cx + ux * edge + ox, a.cy + uy * edge + oy, tipX - ux * head * 0.6f, tipY - uy * head * 0.6f, s.width, s.color);
		canvas.arrow(tipX, tipY, ux, uy, head, s.color);
	}
	return img;
}

} // namespace mitiru::tool
