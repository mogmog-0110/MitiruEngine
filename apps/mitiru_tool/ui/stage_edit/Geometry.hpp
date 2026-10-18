#pragma once
// stage_edit の形。スキーマの gizmo に従って、JSON の要素から世界座標の形を読み、動かした形を JSON に書き戻す。
// 座標は m、+Y が上、yaw は真上から見た角度で 0 が +Z、90 が +X。

#include "EditSchema.hpp"

#include <mitiru/data/JsonText.hpp>

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace mitiru::tool::stageedit
{

struct Vec3
{
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
};

struct Aabb
{
	Vec3 lo;
	Vec3 hi;
};

struct Geom
{
	Shape shape = Shape::Point;
	Vec3 a;                    ///< Box: min / OrientedBox: center / Point: at / Segment, Wall: from
	Vec3 b;                    ///< Box: max / OrientedBox: size / Segment, Wall: to
	double yaw = 0.0;
	bool hasYaw = false;
	double radius = 0.0;
	bool hasRadius = false;
	double bottom = 0.0;       ///< Wall
	double top = 0.0;
	std::vector<Vec3> pts;     ///< Polyline
};

/// 図の上の取っ手。Box は 4 辺の中点と 4 隅 (id は 1=minX 2=maxX 4=minZ 8=maxZ の組)
struct Handle
{
	enum class Type { BoxSide, Vertex, Yaw, Radius };
	Type type = Type::Vertex;
	int id = 0;
	double x = 0.0;
	double z = 0.0;
};

/// 要素の JSON から形を読む。必要な数が揃わなければ false
[[nodiscard]] bool readGeom(const Kind& kind, const nlohmann::ordered_json& item, Geom& out);

/// 形を要素の位置 itemPointer へ書く。値の変わった数だけが書き換わる
void writeGeom(mitiru::data::JsonText& doc, const std::string& itemPointer, const Kind& kind, const Geom& g);

/// 要素を (dx, dy, dz) 動かした形
[[nodiscard]] Geom translated(const Geom& g, double dx, double dy, double dz);

[[nodiscard]] Aabb boundsOf(const Geom& g);

/// 真上から見た形を、取っ手の位置つきで
[[nodiscard]] std::vector<Handle> handlesOf(const Geom& g);

/// 取っ手を (wx, wz) まで引いた形。snap で刻む
[[nodiscard]] Geom dragged(const Geom& base, const Handle& handle, double wx, double wz, const Snap& snap);

/// 真上から (wx, wz) までの距離 (中なら 0)。area は大きさの目安 (重なったときに小さい方を選ぶため)
[[nodiscard]] double distanceTo(const Geom& g, double wx, double wz, double* area = nullptr);

/// 向きのある箱の底の 4 隅 (x, z)
[[nodiscard]] std::array<std::pair<double, double>, 4> orientedCorners(const Geom& g);

[[nodiscard]] double snapTo(double v, double step);

[[nodiscard]] double normalizedYaw(double deg);

/// 要素の鍵 (key) と成分 (idx) の JSON の位置。key が空なら要素そのものの成分
[[nodiscard]] std::string pointerFor(const std::string& itemPointer, const std::string& key, int idx = -1);

} // namespace mitiru::tool::stageedit
