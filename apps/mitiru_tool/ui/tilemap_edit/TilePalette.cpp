#include "TilePalette.hpp"

#include <algorithm>
#include <cmath>

namespace mitiru::tool::tilemapedit
{

namespace
{

using stageedit::Canvas;
using stageedit::Rgb;
using stageedit::Rgba;

constexpr int kMaxTilesetTiles = 4096;

Rgb derivedColor(int id)
{
	// 黄金角で色相をずらすと、続いた番号が似た色にならない
	const double hue = std::fmod(id * 137.508, 360.0) / 60.0;
	const double s = 0.45;
	const double v = 0.82;
	const double c = v * s;
	const double x = c * (1.0 - std::fabs(std::fmod(hue, 2.0) - 1.0));
	const double m = v - c;
	double r = 0, g = 0, b = 0;
	switch (static_cast<int>(hue))
	{
	case 0: r = c; g = x; break;
	case 1: r = x; g = c; break;
	case 2: g = c; b = x; break;
	case 3: g = x; b = c; break;
	case 4: r = x; b = c; break;
	default: r = c; b = x; break;
	}
	const auto to8 = [&](double f) { return static_cast<unsigned char>(std::lround((f + m) * 255.0)); };
	return Rgb{ to8(r), to8(g), to8(b) };
}

int rotationSteps(float degrees)
{
	return ((static_cast<int>(std::lround(degrees / 90.0f)) % 4) + 4) % 4;
}

} // namespace

TilePalette::TilePalette(const TileSchema& schema, std::optional<DecodedImage> image) : m_schema(&schema), m_image(std::move(image))
{
	if (m_image && schema.tileset.tileWidth > 0 && schema.tileset.tileHeight > 0)
	{
		const int cols = m_image->width / schema.tileset.tileWidth;
		const int rows = m_image->height / schema.tileset.tileHeight;
		m_tilesetCount = std::min(cols * rows, kMaxTilesetTiles);
		for (int i = 0; i < m_tilesetCount; ++i) { m_ids.push_back(schema.tileset.firstId + i); }
	}
	for (const TileKind& kind : schema.tiles)
	{
		if (std::find(m_ids.begin(), m_ids.end(), kind.id) == m_ids.end()) { m_ids.push_back(kind.id); }
	}
}

std::string TilePalette::label(int id) const
{
	for (const TileKind& kind : m_schema->tiles) { if (kind.id == id) { return kind.label; } }
	return std::to_string(id);
}

Rgb TilePalette::color(int id) const
{
	for (const TileKind& kind : m_schema->tiles) { if (kind.id == id) { return kind.color; } }
	return derivedColor(id);
}

bool TilePalette::imageCell(int id, int& sx, int& sy) const
{
	const int index = id - m_schema->tileset.firstId;
	if (!m_image || index < 0 || index >= m_tilesetCount) { return false; }
	const int cols = m_image->width / m_schema->tileset.tileWidth;
	sx = (index % cols) * m_schema->tileset.tileWidth;
	sy = (index / cols) * m_schema->tileset.tileHeight;
	return true;
}

void TilePalette::paint(Canvas& canvas, double x, double y, double size, const Cell& cell, int alpha) const
{
	const int steps = rotationSteps(cell.rotation);
	int sx = 0, sy = 0;
	if (imageCell(cell.id, sx, sy))
	{
		const int tw = m_schema->tileset.tileWidth;
		const int th = m_schema->tileset.tileHeight;
		for (int py = static_cast<int>(std::floor(y)); py < static_cast<int>(std::ceil(y + size)); ++py)
		{
			for (int px = static_cast<int>(std::floor(x)); px < static_cast<int>(std::ceil(x + size)); ++px)
			{
				double u = (px + 0.5 - x) / size;
				double v = (py + 0.5 - y) / size;
				if (u < 0.0 || u >= 1.0 || v < 0.0 || v >= 1.0) { continue; }
				for (int k = 0; k < steps; ++k) { const double t = u; u = v; v = 1.0 - t; }
				if (cell.flipX) { u = 1.0 - u; }
				if (cell.flipY) { v = 1.0 - v; }
				const int tx = sx + std::min(tw - 1, static_cast<int>(u * tw));
				const int ty = sy + std::min(th - 1, static_cast<int>(v * th));
				const std::uint8_t* p = &m_image->rgba[(static_cast<std::size_t>(ty) * static_cast<std::size_t>(m_image->width) + static_cast<std::size_t>(tx)) * 4];
				canvas.blend(px, py, Rgba{ p[0], p[1], p[2], static_cast<std::uint8_t>(p[3] * alpha / 255) });
			}
		}
		return;
	}
	const Rgb c = color(cell.id);
	canvas.fillRect(x, y, x + size, y + size, Rgba{ c.r, c.g, c.b, static_cast<std::uint8_t>(alpha) });
	const bool light = (c.r * 299 + c.g * 587 + c.b * 114) / 1000 > 140;
	const Rgba ink = light ? Rgba{ 20, 20, 24, static_cast<std::uint8_t>(alpha) } : Rgba{ 245, 245, 250, static_cast<std::uint8_t>(alpha) };
	if (size >= 14.0) { canvas.digits(static_cast<int>(x) + 2, static_cast<int>(y) + 2, std::to_string(cell.id), ink, 1); }
	if (cell.flipX || cell.flipY || steps != 0)
	{
		// 向きが分かるように、タイルの左上だった角に小さな四角を置く (反転してから回す)
		double u = cell.flipX ? 1.0 : 0.0;
		double v = cell.flipY ? 1.0 : 0.0;
		for (int k = 0; k < steps; ++k) { const double t = u; u = 1.0 - v; v = t; }
		const double m = std::max(2.0, size / 4.0);
		const double mx = x + u * (size - m);
		const double my = y + v * (size - m);
		canvas.fillRect(mx, my, mx + m, my + m, ink);
	}
}

int TilePalette::columns(int width) const
{
	return std::max(1, (width - kGap) / (kSwatch + kGap));
}

int TilePalette::pickerHeight(int width) const
{
	const int count = static_cast<int>(m_ids.size()) + 1;
	const int cols = columns(width);
	const int rows = (count + cols - 1) / cols;
	return rows * (kSwatch + kGap) + kGap;
}

Canvas TilePalette::renderPicker(int width, const Cell& brush) const
{
	Canvas canvas(width, pickerHeight(width));
	canvas.clear(Rgba{ 0, 0, 0, 0 });
	const int cols = columns(width);
	for (std::size_t i = 0; i <= m_ids.size(); ++i)
	{
		const int cx = kGap + static_cast<int>(i % static_cast<std::size_t>(cols)) * (kSwatch + kGap);
		const int cy = kGap + static_cast<int>(i / static_cast<std::size_t>(cols)) * (kSwatch + kGap);
		const int id = i == 0 ? 0 : m_ids[i - 1];
		if (id == 0)
		{
			canvas.fillRect(cx, cy, cx + kSwatch, cy + kSwatch, Rgba{ 70, 70, 76, 255 });
			canvas.line(cx + 6, cy + 6, cx + kSwatch - 6, cy + kSwatch - 6, Rgba{ 235, 90, 90, 255 }, 2);
			canvas.line(cx + kSwatch - 6, cy + 6, cx + 6, cy + kSwatch - 6, Rgba{ 235, 90, 90, 255 }, 2);
		}
		else
		{
			canvas.fillRect(cx, cy, cx + kSwatch, cy + kSwatch, Rgba{ 70, 70, 76, 255 });
			paint(canvas, cx, cy, kSwatch, Cell{ id, 0.0f, false, false });
		}
		if (id == brush.id) { canvas.strokeRect(cx - 1, cy - 1, cx + kSwatch, cy + kSwatch, Rgba{ 255, 255, 255, 255 }, 2); }
	}
	return canvas;
}

std::optional<int> TilePalette::pickAt(int width, double sx, double sy) const
{
	if (sx < kGap || sy < kGap) { return std::nullopt; }
	const int cols = columns(width);
	const int col = static_cast<int>((sx - kGap) / (kSwatch + kGap));
	const int row = static_cast<int>((sy - kGap) / (kSwatch + kGap));
	const double inX = std::fmod(sx - kGap, kSwatch + kGap);
	const double inY = std::fmod(sy - kGap, kSwatch + kGap);
	if (col >= cols || inX >= kSwatch || inY >= kSwatch) { return std::nullopt; }
	const std::size_t index = static_cast<std::size_t>(row) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(col);
	if (index > m_ids.size()) { return std::nullopt; }
	return index == 0 ? 0 : m_ids[index - 1];
}

} // namespace mitiru::tool::tilemapedit
