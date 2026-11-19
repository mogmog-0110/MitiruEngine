#include "TileView.hpp"

#include <algorithm>
#include <cmath>

namespace mitiru::tool::tilemapedit
{

namespace
{

using stageedit::Canvas;
using stageedit::Rgba;

constexpr double kMinCell = 3.0;
constexpr double kMaxCell = 96.0;
constexpr double kGridFrom = 8.0;
constexpr int kOtherLayerAlpha = 110;

const Rgba kBackground{ 36, 36, 40, 255 };
const Rgba kMapFloor{ 52, 52, 58, 255 };
const Rgba kGridLine{ 255, 255, 255, 28 };
const Rgba kFrame{ 200, 200, 210, 255 };
const Rgba kAccent{ 90, 170, 255, 255 };

} // namespace

void TileView::fit(int mapWidth, int mapHeight)
{
	const double margin = 12.0;
	m_cell = std::clamp(std::floor(std::min((m_w - 2 * margin) / std::max(1, mapWidth), (m_h - 2 * margin) / std::max(1, mapHeight))), kMinCell, kMaxCell);
	m_ox = std::round((m_w - m_cell * mapWidth) / 2.0);
	m_oy = std::round((m_h - m_cell * mapHeight) / 2.0);
}

void TileView::zoomAt(double sx, double sy, double factor)
{
	const double next = std::clamp(m_cell * factor, kMinCell, kMaxCell);
	const double f = next / m_cell;
	m_ox = sx - (sx - m_ox) * f;
	m_oy = sy - (sy - m_oy) * f;
	m_cell = next;
}

std::pair<int, int> TileView::cellAt(double sx, double sy) const
{
	return { static_cast<int>(std::floor((sx - m_ox) / m_cell)), static_cast<int>(std::floor((sy - m_oy) / m_cell)) };
}

std::pair<double, double> TileView::toScreen(double x, double y) const
{
	return { m_ox + x * m_cell, m_oy + y * m_cell };
}

std::pair<double, double> TileView::centerOf(int x, int y) const
{
	return { m_ox + (x + 0.5) * m_cell, m_oy + (y + 0.5) * m_cell };
}

void TileView::drawTiles(Canvas& canvas, const TileDoc& doc, const TilePalette& palette, int activeLayer) const
{
	const auto [c0x, c0y] = cellAt(0, 0);
	const auto [c1x, c1y] = cellAt(m_w - 1, m_h - 1);
	for (int layer = 0; layer < doc.layerCount(); ++layer)
	{
		for (int y = std::max(0, c0y); y <= std::min(doc.height(layer) - 1, c1y); ++y)
		{
			for (int x = std::max(0, c0x); x <= std::min(doc.width(layer) - 1, c1x); ++x)
			{
				const MaybeCell cell = doc.at(layer, x, y);
				if (!cell || cell->id == 0) { continue; }
				const auto [px, py] = toScreen(x, y);
				palette.paint(canvas, px, py, m_cell, *cell, layer == activeLayer ? 255 : kOtherLayerAlpha);
			}
		}
	}
}

void TileView::drawGrid(Canvas& canvas, int mapW, int mapH) const
{
	const auto [fx, fy] = toScreen(0, 0);
	if (m_cell >= kGridFrom)
	{
		const auto [c0x, c0y] = cellAt(0, 0);
		const auto [c1x, c1y] = cellAt(m_w - 1, m_h - 1);
		for (int x = std::max(0, c0x); x <= std::min(mapW, c1x + 1); ++x) { canvas.line(fx + x * m_cell, fy, fx + x * m_cell, fy + mapH * m_cell, kGridLine); }
		for (int y = std::max(0, c0y); y <= std::min(mapH, c1y + 1); ++y) { canvas.line(fx, fy + y * m_cell, fx + mapW * m_cell, fy + y * m_cell, kGridLine); }
	}
	canvas.strokeRect(fx, fy, fx + mapW * m_cell, fy + mapH * m_cell, kFrame);
}

void TileView::drawOverlay(Canvas& canvas, const TilePalette& palette, const Overlay& overlay) const
{
	if (overlay.ghost != nullptr && !overlay.ghost->empty())
	{
		for (int y = 0; y < overlay.ghost->height; ++y)
		{
			for (int x = 0; x < overlay.ghost->width; ++x)
			{
				const MaybeCell& cell = overlay.ghost->at(x, y);
				if (!cell || cell->id == 0) { continue; }
				const auto [px, py] = toScreen(overlay.ghostX + x, overlay.ghostY + y);
				palette.paint(canvas, px, py, m_cell, *cell, 170);
			}
		}
		const auto [gx, gy] = toScreen(overlay.ghostX, overlay.ghostY);
		canvas.strokeRect(gx, gy, gx + overlay.ghost->width * m_cell, gy + overlay.ghost->height * m_cell, Rgba{ 255, 210, 90, 255 }, 2);
	}
	if (overlay.selection)
	{
		const auto [sx, sy] = toScreen(overlay.selection->x0, overlay.selection->y0);
		const double ex = sx + overlay.selection->width() * m_cell;
		const double ey = sy + overlay.selection->height() * m_cell;
		canvas.fillRect(sx, sy, ex, ey, Rgba{ kAccent.r, kAccent.g, kAccent.b, 40 });
		canvas.strokeRect(sx, sy, ex, ey, kAccent, 2);
	}
}

Canvas TileView::render(const TileDoc& doc, const TilePalette& palette, int activeLayer, const Overlay& overlay) const
{
	Canvas canvas(m_w, m_h);
	canvas.clear(kBackground);
	const int mapW = doc.width(activeLayer);
	const int mapH = doc.height(activeLayer);
	const auto [fx, fy] = toScreen(0, 0);
	canvas.fillRect(fx, fy, fx + mapW * m_cell, fy + mapH * m_cell, kMapFloor);
	drawTiles(canvas, doc, palette, activeLayer);
	drawGrid(canvas, mapW, mapH);
	drawOverlay(canvas, palette, overlay);
	return canvas;
}

} // namespace mitiru::tool::tilemapedit
