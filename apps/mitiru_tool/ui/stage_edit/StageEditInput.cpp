// stage_edit のマウスとキー。上からの図は「選ぶ・動かす・取っ手を引く・つかんで平行移動」、3D の図は「回る・選ぶ」。
// 動かしている間は文書を基準から書き換え直すだけで、取り消しには離した時に 1 回だけ積む。

#include "StageEditPage.hpp"

#include <cmath>

namespace mitiru::tool
{

namespace
{

constexpr double kZoomStep = 1.18;
constexpr double kOrbitZoomStep = 1.14;
constexpr double kClickSlop = 3.0;

std::string number(double v)
{
	return mitiru::data::JsonText::formatNumber(v, "0.0");
}

} // namespace

void StageEditPage::onPointer(std::string_view elementId, const PointerEvent& ev)
{
	if (!m_loaded) { return; }
	const bool top = elementId == "top";
	const double w = top ? m_top->width() : m_orbit->width();
	const double h = top ? m_top->height() : m_orbit->height();
	const double sx = ev.width > 0.0f ? ev.x / ev.width * w : ev.x;
	const double sy = ev.height > 0.0f ? ev.y / ev.height * h : ev.y;
	if (ev.kind == PointerEvent::Kind::Wheel)
	{
		if (top) { m_top->zoomAt(sx, sy, ev.wheel < 0.0f ? kZoomStep : 1.0 / kZoomStep); }
		else { m_orbit->zoom(ev.wheel < 0.0f ? 1.0 / kOrbitZoomStep : kOrbitZoomStep); }
		m_imagesDirty = true;
		if (top) { pushStatus(); }
		return;
	}
	if (top) { topPointer(ev, sx, sy); }
	else { orbitPointer(ev, sx, sy); }
}

void StageEditPage::beginMove(int item, double wx, double wz)
{
	m_doc.beginGesture(item);
	const stageedit::Geom& g = m_doc.items()[static_cast<std::size_t>(item)].geom;
	m_drag.kind = DragKind::Move;
	m_drag.downX = wx;
	m_drag.downZ = wz;
	m_drag.ref = g.shape == stageedit::Shape::Polyline && !g.pts.empty() ? g.pts.front() : g.a;
	pushPanel();
}

void StageEditPage::dragTo(double wx, double wz)
{
	const double step = m_schema.snap.move;
	const double dx = stageedit::snapTo(m_drag.ref.x + (wx - m_drag.downX), step) - m_drag.ref.x;
	const double dz = stageedit::snapTo(m_drag.ref.z + (wz - m_drag.downZ), step) - m_drag.ref.z;
	m_doc.gestureMove(dx, 0.0, dz);
	m_status = "Moving: x " + number(dx) + " m, z " + number(dz) + " m";
	changed(false);
}

void StageEditPage::endDrag()
{
	const DragKind kind = m_drag.kind;
	m_drag = Drag{};
	if (kind != DragKind::Move && kind != DragKind::Handle) { return; }
	const bool did = m_doc.endGesture();
	m_status = did ? "Moved" : m_status;
	changed(true);
}

void StageEditPage::topPointer(const PointerEvent& ev, double sx, double sy)
{
	using Kind = PointerEvent::Kind;
	if (ev.kind == Kind::Down)
	{
		m_drag = Drag{};
		m_drag.lastX = sx;
		m_drag.lastY = sy;
		if (ev.button != 0) { m_drag.kind = DragKind::Pan; return; }
		if (const auto handle = m_top->pickHandle(m_doc, m_doc.selected(), sx, sy))
		{
			m_doc.beginGesture(m_doc.selected());
			m_drag.kind = DragKind::Handle;
			m_drag.handle = *handle;
			return;
		}
		const auto [wx, wz] = m_top->toWorld(sx, sy);
		const int item = m_top->pick(m_doc, sx, sy);
		if (item >= 0) { beginMove(item, wx, wz); return; }
		m_doc.select(-1);
		m_drag.kind = DragKind::Pan;
		changed(true);
		return;
	}
	if (m_drag.kind == DragKind::None) { return; }
	if (ev.kind == Kind::Up) { endDrag(); return; }
	const auto [wx, wz] = m_top->toWorld(sx, sy);
	if (m_drag.kind == DragKind::Pan)
	{
		m_top->panPixels(sx - m_drag.lastX, sy - m_drag.lastY);
		m_drag.lastX = sx;
		m_drag.lastY = sy;
		m_imagesDirty = true;
	}
	else if (m_drag.kind == DragKind::Move) { dragTo(wx, wz); }
	else if (m_drag.kind == DragKind::Handle)
	{
		m_doc.gestureHandle(m_drag.handle, wx, wz);
		m_status = "Dragging handle (" + number(wx) + ", " + number(wz) + ")";
		changed(false);
	}
}

void StageEditPage::orbitPointer(const PointerEvent& ev, double sx, double sy)
{
	using Kind = PointerEvent::Kind;
	if (ev.kind == Kind::Down)
	{
		m_drag = Drag{};
		m_drag.kind = DragKind::Orbit;
		m_drag.lastX = sx;
		m_drag.lastY = sy;
		m_drag.downX = sx;
		m_drag.downZ = sy;
		m_drag.handle.id = ev.button;
		return;
	}
	if (m_drag.kind != DragKind::Orbit) { return; }
	if (ev.kind == Kind::Move)
	{
		m_orbit->orbit(sx - m_drag.lastX, sy - m_drag.lastY);
		m_drag.lastX = sx;
		m_drag.lastY = sy;
		m_drag.moved = m_drag.moved || std::hypot(sx - m_drag.downX, sy - m_drag.downZ) > kClickSlop;
		m_imagesDirty = true;
		return;
	}
	const bool click = !m_drag.moved && m_drag.handle.id == 0;
	m_drag = Drag{};
	if (!click) { return; }
	m_doc.select(m_orbit->pick(m_doc, sx, sy));
	changed(true);
}

void StageEditPage::nudgeKey(const KeyEvent& ev)
{
	using Key = KeyEvent::Key;
	const double rs = m_schema.top.rightIsNegX ? -1.0 : 1.0;
	const double us = m_schema.top.upIsNegZ ? -1.0 : 1.0;
	const double step = m_schema.snap.move;
	double dx = 0.0, dy = 0.0, dz = 0.0;
	switch (ev.key)
	{
	case Key::Right: dx = rs * step; break;
	case Key::Left: dx = -rs * step; break;
	case Key::Up: dz = us * step; break;
	case Key::Down: dz = -us * step; break;
	case Key::PageUp: dy = m_schema.snap.height; break;
	case Key::PageDown: dy = -m_schema.snap.height; break;
	default: return;
	}
	m_status = m_doc.nudge(dx, dy, dz) ? "Nudged" : "Nothing selected";
	changed(true);
}

void StageEditPage::onKey(const KeyEvent& ev)
{
	using Key = KeyEvent::Key;
	if (!m_loaded) { return; }
	if (ev.ctrl && ev.key == Key::Z) { undoRedo(!ev.shift); return; }
	if (ev.ctrl && ev.key == Key::Y) { undoRedo(false); return; }
	if (ev.ctrl && ev.key == Key::D) { onAction("se.dup", nlohmann::json::object()); return; }
	switch (ev.key)
	{
	case Key::Delete: onAction("se.del", nlohmann::json::object()); return;
	case Key::Escape: m_doc.select(-1); changed(true); return;
	case Key::Home:
	case Key::F: onAction("se.fit", nlohmann::json::object()); return;
	case Key::Q:
	case Key::E:
		m_status = m_doc.rotate(ev.key == Key::E ? m_schema.snap.angle : -m_schema.snap.angle) ? "Rotated" : "This shape can't rotate";
		changed(true);
		return;
	default: nudgeKey(ev); return;
	}
}

} // namespace mitiru::tool
