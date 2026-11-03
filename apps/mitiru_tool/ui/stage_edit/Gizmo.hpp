#pragma once
// 3D の図の移動と回転のギズモ。選んだ物の枠の中心に 3 本の軸、xz の平面、向きの輪を置き、
// 画面空間で当たりを取り、押した時と今のレイの差から移動量と角度を出す。描画は Canvas に線で。
// ここは純関数だけで、文書にもゲームにも触れない (ADR 0115)。

#include "Canvas.hpp"
#include "EditSchema.hpp"
#include "Geometry.hpp"
#include "View3d.hpp"

#include <optional>
#include <utility>

namespace mitiru::tool::stageedit
{

enum class GizmoPart { None, AxisX, AxisY, AxisZ, PlaneXZ, Yaw };

struct GizmoFrame
{
	Vec3 anchor;          ///< 物の枠の中心
	double scale = 1.0;   ///< 軸 1 本の world での長さ (画面で一定の px になるように視点から決める)
	bool hasYaw = false;
	double yawDeg = 0.0;
};

/// 押した時と今の差。移動は world の m、回転は度
struct GizmoDelta
{
	double dx = 0.0;
	double dy = 0.0;
	double dz = 0.0;
	double yawDeg = 0.0;
};

[[nodiscard]] GizmoFrame gizmoFrameOf(const Geom& g, const ViewCam& cam);

/// (sx, sy) にある取っ手。無ければ None。視線とほぼ平行で画面にほとんど映らない軸は選ばない
[[nodiscard]] GizmoPart pickGizmo(const GizmoFrame& f, const ViewCam& cam, double sx, double sy);

/// 取っ手をつかむ world の点 (台本と試験が画面位置を出すために使う)
[[nodiscard]] Vec3 gizmoGrabPoint(const GizmoFrame& f, GizmoPart part);

/// 押した時のレイと今のレイから差を出す。レイが軸や面と平行なら 0
[[nodiscard]] GizmoDelta gizmoDelta(const GizmoFrame& f, GizmoPart part, const Ray& down, const Ray& now);

/// 基準点 ref が刻みの目に乗るように刻んだ移動量 (x と z は snap.move、y は snap.height)
[[nodiscard]] Vec3 gizmoSnappedMove(const Vec3& ref, const GizmoDelta& d, const Snap& snap);

/// 差を刻んで形に当てる。移動の基準点は Geom::a か折れ線の先頭
[[nodiscard]] Geom gizmoApply(const Geom& base, GizmoPart part, const GizmoDelta& d, const Snap& snap);

void drawGizmo(Canvas& canvas, const ViewCam& cam, const GizmoFrame& f, GizmoPart active);

} // namespace mitiru::tool::stageedit
