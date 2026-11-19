// 押している間に起きた編集は、離したときに取り消しの 1 手にまとめる

#include "TilemapEditPage.hpp"

#include <algorithm>
#include <cmath>

namespace mitiru::tool
{

namespace
{

using tilemapedit::Cell;
using tilemapedit::Rect;

constexpr double kZoomStep = 1.18;
constexpr double kClickSlop = 3.0;
using tilemapedit::kPickerWidth;

} // namespace

std::pair<int, int> TilemapEditPage::clampedCell(double sx, double sy) const
{
	const auto [x, y] = m_viewport->cellAt(sx, sy);
	return { std::clamp(x, 0, m_doc.width(m_layer) - 1), std::clamp(y, 0, m_doc.height(m_layer) - 1) };
}

void TilemapEditPage::onPointer(std::string_view elementId, const PointerEvent& ev)
{
	if (!m_loaded) { return; }
	const bool map = elementId == "map";
	const double w = map ? m_viewport->width() : kPickerWidth;
	const double h = map ? m_viewport->height() : m_palette->pickerHeight(kPickerWidth);
	const double sx = ev.width > 0.0f ? ev.x / ev.width * w : ev.x;
	const double sy = ev.height > 0.0f ? ev.y / ev.height * h : ev.y;
	if (!map) { tilesPointer(ev, sx, sy); return; }
	if (ev.kind == PointerEvent::Kind::Wheel)
	{
		m_viewport->zoomAt(sx, sy, ev.wheel < 0.0f ? kZoomStep : 1.0 / kZoomStep);
		m_imagesDirty = true;
		pushStatus();
		return;
	}
	mapPointer(ev, sx, sy);
}

void TilemapEditPage::tilesPointer(const PointerEvent& ev, double sx, double sy)
{
	if (ev.kind != PointerEvent::Kind::Down || ev.button != 0) { return; }
	const auto id = m_palette->pickAt(kPickerWidth, sx, sy);
	if (!id) { return; }
	m_brush.id = *id;
	if (*id == 0) { m_brush = Cell{}; }
	m_tool = Tool::Paint;
	m_pasting = false;
	m_status = *id == 0 ? "Eraser" : "Tile " + m_palette->label(*id);
	changed(true);
}

void TilemapEditPage::paintCell(int x, int y)
{
	if (m_brush.id == 0) { m_doc.set(m_layer, x, y, std::nullopt); }
	else { m_doc.set(m_layer, x, y, Cell{ m_brush.id, m_brush.rotation, m_brush.flipX, m_brush.flipY }); }
}

void TilemapEditPage::paintLine(int x0, int y0, int x1, int y1)
{
	// 速く動かしても途切れないよう、通ったマスをすべて塗る
	const int dx = std::abs(x1 - x0);
	const int dy = std::abs(y1 - y0);
	const int sx = x0 < x1 ? 1 : -1;
	const int sy = y0 < y1 ? 1 : -1;
	int err = dx - dy;
	for (;;)
	{
		paintCell(x0, y0);
		if (x0 == x1 && y0 == y1) { break; }
		const int e2 = 2 * err;
		if (e2 > -dy) { err -= dy; x0 += sx; }
		if (e2 < dx) { err += dx; y0 += sy; }
	}
}

void TilemapEditPage::pickAt(int x, int y)
{
	if (!m_doc.inside(m_layer, x, y)) { return; }
	const tilemapedit::MaybeCell cell = m_doc.at(m_layer, x, y);
	m_brush = cell && cell->id != 0 ? *cell : Cell{};
	m_tool = Tool::Paint;
	m_pasting = false;
	m_status = m_brush.id == 0 ? "Picked an empty cell: the eraser" : "Picked tile " + m_palette->label(m_brush.id);
	changed(true);
}

void TilemapEditPage::mapPointer(const PointerEvent& ev, double sx, double sy)
{
	using Kind = PointerEvent::Kind;
	if (ev.kind == Kind::Down) { mapDown(ev.button, sx, sy); return; }
	if (m_drag.kind == DragKind::None) { return; }
	if (ev.kind == Kind::Up) { endDrag(); return; }
	mapMove(sx, sy);
}

void TilemapEditPage::mapDown(int button, double sx, double sy)
{
	m_drag = Drag{};
	m_drag.downX = m_drag.lastX = sx;
	m_drag.downY = m_drag.lastY = sy;
	if (button == 2) { m_drag.kind = DragKind::Pan; return; }
	if (button != 0) { m_drag.kind = DragKind::RightPending; return; }
	const auto [cx, cy] = m_viewport->cellAt(sx, sy);
	m_drag.lastCellX = cx;
	m_drag.lastCellY = cy;
	if (m_pasting)
	{
		m_drag.kind = DragKind::Place;
		m_ghostX = cx;
		m_ghostY = cy;
		changed(false);
		return;
	}
	if (m_tool == Tool::Select)
	{
		const auto [ax, ay] = clampedCell(sx, sy);
		m_drag.kind = DragKind::Select;
		m_drag.anchorX = ax;
		m_drag.anchorY = ay;
		m_selection = Rect::of(ax, ay, ax, ay);
		changed(true);
		return;
	}
	m_drag.kind = DragKind::Paint;
	m_doc.beginGroup();
	paintCell(cx, cy);
	changed(false);
}

void TilemapEditPage::mapMove(double sx, double sy)
{
	const auto [cx, cy] = m_viewport->cellAt(sx, sy);
	switch (m_drag.kind)
	{
	case DragKind::Paint:
		if (cx != m_drag.lastCellX || cy != m_drag.lastCellY)
		{
			paintLine(m_drag.lastCellX, m_drag.lastCellY, cx, cy);
			m_drag.lastCellX = cx;
			m_drag.lastCellY = cy;
			changed(false);
		}
		break;
	case DragKind::Select:
	{
		const auto [ex, ey] = clampedCell(sx, sy);
		m_selection = Rect::of(m_drag.anchorX, m_drag.anchorY, ex, ey);
		changed(false);
		break;
	}
	case DragKind::Place:
		m_ghostX = cx;
		m_ghostY = cy;
		changed(false);
		break;
	case DragKind::RightPending:
		if (!m_drag.moved && std::hypot(sx - m_drag.downX, sy - m_drag.downY) <= kClickSlop) { break; }
		m_drag.moved = true;
		[[fallthrough]];
	case DragKind::Pan:
		m_viewport->pan(sx - m_drag.lastX, sy - m_drag.lastY);
		m_drag.lastX = sx;
		m_drag.lastY = sy;
		m_imagesDirty = true;
		break;
	case DragKind::None:
		break;
	}
}

void TilemapEditPage::endDrag()
{
	const Drag drag = m_drag;
	m_drag = Drag{};
	switch (drag.kind)
	{
	case DragKind::Paint:
		m_status = m_doc.endGroup() ? "Painted" : m_status;
		changed(true);
		break;
	case DragKind::Place:
		m_status = m_doc.paste(m_layer, m_ghostX, m_ghostY, m_clip) ? "Pasted at (" + std::to_string(m_ghostX) + ", " + std::to_string(m_ghostY) + ")" : "Nothing changed";
		changed(true);
		break;
	case DragKind::Select:
		m_status = "Selected " + std::to_string(m_selection->width()) + "x" + std::to_string(m_selection->height());
		changed(true);
		break;
	case DragKind::RightPending:
		if (!drag.moved)
		{
			const auto [x, y] = m_viewport->cellAt(drag.downX, drag.downY);
			pickAt(x, y);
		}
		break;
	default:
		break;
	}
}

} // namespace mitiru::tool
