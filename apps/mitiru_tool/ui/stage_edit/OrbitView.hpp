#pragma once
// 3D で見回す図。物を線で描き、ドラッグで回り、ホイールで寄り引きし、クリックで物を選ぶ。
// 選んだ物にはギズモ (軸、平面、向きの輪) が出て、つかんで動かし、回せる (ADR 0115)。
// 世界は右手系で y が上。方位 0 は -z の側から +z を向く (遊ぶときのカメラと同じ側)。

#include "Canvas.hpp"
#include "Gizmo.hpp"
#include "StageDoc.hpp"
#include "View3d.hpp"

#include <optional>

namespace mitiru::tool::stageedit
{

class OrbitView
{
public:
	OrbitView(const Schema& schema, int width, int height);

	[[nodiscard]] int width() const noexcept { return m_w; }
	[[nodiscard]] int height() const noexcept { return m_h; }

	/// 物が全部収まるように見る位置を決める
	void frame(const StageDoc& doc);
	void focusOn(const Vec3& target) { m_target = target; }
	void orbit(double dxPixels, double dyPixels);
	void zoom(double factor);

	[[nodiscard]] Canvas render(const StageDoc& doc, GizmoPart active = GizmoPart::None) const;
	[[nodiscard]] int pick(const StageDoc& doc, double sx, double sy) const;
	/// 選んでいる物のギズモの枠。何も選んでいなければ nullopt
	[[nodiscard]] std::optional<GizmoFrame> gizmoFrame(const StageDoc& doc) const;
	[[nodiscard]] GizmoPart pickGizmo(const StageDoc& doc, double sx, double sy) const;

	[[nodiscard]] ViewCam view() const;
	[[nodiscard]] double azimuthDeg() const noexcept { return m_azimuth; }
	[[nodiscard]] double pitchDeg() const noexcept { return m_pitch; }
	[[nodiscard]] double distance() const noexcept { return m_dist; }

private:
	void drawGeom(Canvas& canvas, const ViewCam& cam, const Geom& g, Rgba color, int thickness, bool fillFaces) const;

	const Schema* m_schema;
	int m_w;
	int m_h;
	Vec3 m_target;
	double m_azimuth = 20.0;
	double m_pitch = 35.0;
	double m_dist = 80.0;
};

} // namespace mitiru::tool::stageedit
