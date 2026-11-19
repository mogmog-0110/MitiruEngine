#pragma once
// 使っているレイヤー以外は薄く重ねる。文書は読むだけで、画面の画素とマスの番号の変換を持つ

#include "TileDoc.hpp"
#include "TilePalette.hpp"

#include <optional>
#include <utility>

namespace mitiru::tool::tilemapedit
{

struct Overlay
{
	std::optional<Rect> selection;
	const Clip* ghost = nullptr;
	int ghostX = 0;
	int ghostY = 0;
};

class TileView
{
public:
	TileView(int width, int height) : m_w(width), m_h(height) {}

	[[nodiscard]] int width() const noexcept { return m_w; }
	[[nodiscard]] int height() const noexcept { return m_h; }
	[[nodiscard]] double cellSize() const noexcept { return m_cell; }

	void fit(int mapWidth, int mapHeight);
	void zoomAt(double sx, double sy, double factor);
	void pan(double dx, double dy) { m_ox += dx; m_oy += dy; }

	[[nodiscard]] std::pair<int, int> cellAt(double sx, double sy) const;
	[[nodiscard]] std::pair<double, double> toScreen(double x, double y) const;
	/// スクリプトと試験で、マスを押す位置を作るために使う
	[[nodiscard]] std::pair<double, double> centerOf(int x, int y) const;

	[[nodiscard]] stageedit::Canvas render(const TileDoc& doc, const TilePalette& palette, int activeLayer, const Overlay& overlay) const;

private:
	void drawTiles(stageedit::Canvas& canvas, const TileDoc& doc, const TilePalette& palette, int activeLayer) const;
	void drawGrid(stageedit::Canvas& canvas, int mapW, int mapH) const;
	void drawOverlay(stageedit::Canvas& canvas, const TilePalette& palette, const Overlay& overlay) const;

	int m_w;
	int m_h;
	double m_cell = 16.0;
	double m_ox = 0.0;    ///< マス (0, 0) の左上
	double m_oy = 0.0;
};

} // namespace mitiru::tool::tilemapedit
