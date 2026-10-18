#include "Geometry.hpp"

#include <algorithm>
#include <cmath>

namespace mitiru::tool::stageedit
{

namespace
{

using Json = nlohmann::ordered_json;
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;

const Json* subOf(const Json& item, const std::string& key)
{
	if (key.empty()) { return &item; }
	if (!item.is_object()) { return nullptr; }
	const auto it = item.find(key);
	return it == item.end() ? nullptr : &*it;
}

bool numbersOf(const Json* j, std::size_t count, double* out)
{
	if (j == nullptr || !j->is_array() || j->size() < count) { return false; }
	for (std::size_t i = 0; i < count; ++i)
	{
		if (!(*j)[i].is_number()) { return false; }
		out[i] = (*j)[i].get<double>();
	}
	return true;
}

bool vec3Of(const Json* j, Vec3& out)
{
	double v[3];
	if (!numbersOf(j, 3, v)) { return false; }
	out = Vec3{ v[0], v[1], v[2] };
	return true;
}

bool vec2Of(const Json* j, Vec3& out)
{
	double v[2];
	if (!numbersOf(j, 2, v)) { return false; }
	out = Vec3{ v[0], 0.0, v[1] };
	return true;
}

bool scalarOf(const Json& item, const Gizmo& gizmo, const char* role, double& out)
{
	const std::string* key = gizmo.key(role);
	if (key == nullptr) { return false; }
	const Json* j = subOf(item, *key);
	if (j == nullptr || !j->is_number()) { return false; }
	out = j->get<double>();
	return true;
}

void setVec3(mitiru::data::JsonText& doc, const std::string& item, const std::string& key, const Vec3& v)
{
	doc.setNumber(pointerFor(item, key, 0), v.x);
	doc.setNumber(pointerFor(item, key, 1), v.y);
	doc.setNumber(pointerFor(item, key, 2), v.z);
}

void setVec2(mitiru::data::JsonText& doc, const std::string& item, const std::string& key, const Vec3& v)
{
	doc.setNumber(pointerFor(item, key, 0), v.x);
	doc.setNumber(pointerFor(item, key, 1), v.z);
}

void setScalar(mitiru::data::JsonText& doc, const std::string& item, const Gizmo& gizmo, const char* role, double v)
{
	const std::string* key = gizmo.key(role);
	if (key != nullptr && !key->empty()) { doc.setNumber(pointerFor(item, *key), v); }
}

std::pair<double, double> forward(double yawDeg)
{
	const double r = yawDeg * kDegToRad;
	return { std::sin(r), std::cos(r) };
}

double distanceToSegment(double px, double pz, double ax, double az, double bx, double bz)
{
	const double dx = bx - ax;
	const double dz = bz - az;
	const double len2 = dx * dx + dz * dz;
	const double t = len2 <= 1e-12 ? 0.0 : std::clamp(((px - ax) * dx + (pz - az) * dz) / len2, 0.0, 1.0);
	return std::hypot(px - (ax + dx * t), pz - (az + dz * t));
}

double distanceToRect(double px, double pz, double x0, double z0, double x1, double z1)
{
	const double dx = std::max({ x0 - px, 0.0, px - x1 });
	const double dz = std::max({ z0 - pz, 0.0, pz - z1 });
	return std::hypot(dx, dz);
}

double minSize(const Snap& snap) { return std::max(snap.size, 0.05); }

} // namespace

std::string pointerFor(const std::string& itemPointer, const std::string& key, int idx)
{
	std::string out = itemPointer;
	if (!key.empty()) { out += "/" + key; }
	if (idx >= 0) { out += "/" + std::to_string(idx); }
	return out;
}

double snapTo(double v, double step)
{
	return step <= 0.0 ? v : std::round(v / step) * step;
}

double normalizedYaw(double deg)
{
	double d = std::fmod(deg, 360.0);
	if (d > 180.0) { d -= 360.0; }
	if (d <= -180.0) { d += 360.0; }
	return d;
}

bool readGeom(const Kind& kind, const Json& item, Geom& out)
{
	const Gizmo& g = kind.gizmo;
	out = Geom{};
	out.shape = g.shape;
	const auto part = [&](const char* role) -> const Json* {
		const std::string* key = g.key(role);
		return key == nullptr ? nullptr : subOf(item, *key);
	};
	switch (g.shape)
	{
	case Shape::Box:
		return vec3Of(part("min"), out.a) && vec3Of(part("max"), out.b);
	case Shape::OrientedBox:
		out.hasYaw = true;
		scalarOf(item, g, "yaw", out.yaw);
		return vec3Of(part("center"), out.a) && vec3Of(part("size"), out.b);
	case Shape::Point:
		if (!vec3Of(part("at"), out.a)) { return false; }
		out.hasYaw = scalarOf(item, g, "yaw", out.yaw);
		out.hasRadius = scalarOf(item, g, "radius", out.radius);
		return true;
	case Shape::Segment:
		return vec3Of(part("from"), out.a) && vec3Of(part("to"), out.b);
	case Shape::Wall:
		if (!vec2Of(part("from"), out.a) || !vec2Of(part("to"), out.b)) { return false; }
		return scalarOf(item, g, "bottom", out.bottom) && scalarOf(item, g, "top", out.top);
	case Shape::Polyline:
	{
		const Json* pts = part("points");
		if (pts == nullptr || !pts->is_array()) { return false; }
		for (const Json& p : *pts)
		{
			Vec3 v;
			if (!vec3Of(&p, v)) { return false; }
			out.pts.push_back(v);
		}
		return !out.pts.empty();
	}
	}
	return false;
}

void writeGeom(mitiru::data::JsonText& doc, const std::string& itemPointer, const Kind& kind, const Geom& geom)
{
	const Gizmo& g = kind.gizmo;
	const auto keyOf = [&](const char* role) { const std::string* k = g.key(role); return k == nullptr ? std::string() : *k; };
	switch (g.shape)
	{
	case Shape::Box:
		setVec3(doc, itemPointer, keyOf("min"), geom.a);
		setVec3(doc, itemPointer, keyOf("max"), geom.b);
		break;
	case Shape::OrientedBox:
		setVec3(doc, itemPointer, keyOf("center"), geom.a);
		setVec3(doc, itemPointer, keyOf("size"), geom.b);
		setScalar(doc, itemPointer, g, "yaw", geom.yaw);
		break;
	case Shape::Point:
		setVec3(doc, itemPointer, keyOf("at"), geom.a);
		if (geom.hasYaw) { setScalar(doc, itemPointer, g, "yaw", geom.yaw); }
		if (geom.hasRadius) { setScalar(doc, itemPointer, g, "radius", geom.radius); }
		break;
	case Shape::Segment:
		setVec3(doc, itemPointer, keyOf("from"), geom.a);
		setVec3(doc, itemPointer, keyOf("to"), geom.b);
		break;
	case Shape::Wall:
		setVec2(doc, itemPointer, keyOf("from"), geom.a);
		setVec2(doc, itemPointer, keyOf("to"), geom.b);
		setScalar(doc, itemPointer, g, "bottom", geom.bottom);
		setScalar(doc, itemPointer, g, "top", geom.top);
		break;
	case Shape::Polyline:
		for (std::size_t i = 0; i < geom.pts.size(); ++i)
		{
			const std::string p = pointerFor(itemPointer, keyOf("points")) + "/" + std::to_string(i);
			doc.setNumber(p + "/0", geom.pts[i].x);
			doc.setNumber(p + "/1", geom.pts[i].y);
			doc.setNumber(p + "/2", geom.pts[i].z);
		}
		break;
	}
}

Geom translated(const Geom& g, double dx, double dy, double dz)
{
	Geom out = g;
	const auto move = [&](Vec3& v) { v.x += dx; v.y += dy; v.z += dz; };
	move(out.a);
	if (g.shape != Shape::OrientedBox && g.shape != Shape::Point) { move(out.b); }
	for (Vec3& p : out.pts) { move(p); }
	if (g.shape == Shape::Wall) { out.bottom += dy; out.top += dy; out.a.y = 0.0; out.b.y = 0.0; }
	return out;
}

std::array<std::pair<double, double>, 4> orientedCorners(const Geom& g)
{
	const auto [fx, fz] = forward(g.yaw);
	const double rx = fz;
	const double rz = -fx;
	const double hw = g.b.x * 0.5;
	const double hl = g.b.z * 0.5;
	const auto corner = [&](double sw, double sl) {
		return std::pair<double, double>{ g.a.x + rx * hw * sw + fx * hl * sl, g.a.z + rz * hw * sw + fz * hl * sl };
	};
	return { corner(-1, -1), corner(1, -1), corner(1, 1), corner(-1, 1) };
}

Aabb boundsOf(const Geom& g)
{
	Aabb b{ g.a, g.a };
	const auto grow = [&](double x, double y, double z) {
		b.lo.x = std::min(b.lo.x, x); b.lo.y = std::min(b.lo.y, y); b.lo.z = std::min(b.lo.z, z);
		b.hi.x = std::max(b.hi.x, x); b.hi.y = std::max(b.hi.y, y); b.hi.z = std::max(b.hi.z, z);
	};
	switch (g.shape)
	{
	case Shape::Box:
		b = Aabb{ { std::min(g.a.x, g.b.x), std::min(g.a.y, g.b.y), std::min(g.a.z, g.b.z) },
		          { std::max(g.a.x, g.b.x), std::max(g.a.y, g.b.y), std::max(g.a.z, g.b.z) } };
		break;
	case Shape::OrientedBox:
		for (const auto& [x, z] : orientedCorners(g)) { grow(x, g.a.y, z); grow(x, g.a.y + g.b.y, z); }
		break;
	case Shape::Point:
		grow(g.a.x - g.radius, g.a.y, g.a.z - g.radius);
		grow(g.a.x + g.radius, g.a.y, g.a.z + g.radius);
		break;
	case Shape::Segment:
		grow(g.b.x, g.b.y, g.b.z);
		break;
	case Shape::Wall:
		b = Aabb{ { std::min(g.a.x, g.b.x), g.bottom, std::min(g.a.z, g.b.z) }, { std::max(g.a.x, g.b.x), g.top, std::max(g.a.z, g.b.z) } };
		break;
	case Shape::Polyline:
		for (const Vec3& p : g.pts) { grow(p.x, p.y, p.z); }
		break;
	}
	return b;
}

std::vector<Handle> handlesOf(const Geom& g)
{
	std::vector<Handle> out;
	using T = Handle::Type;
	switch (g.shape)
	{
	case Shape::Box:
	{
		const double x0 = std::min(g.a.x, g.b.x), x1 = std::max(g.a.x, g.b.x);
		const double z0 = std::min(g.a.z, g.b.z), z1 = std::max(g.a.z, g.b.z);
		const double mx = (x0 + x1) * 0.5, mz = (z0 + z1) * 0.5;
		out = { { T::BoxSide, 1 | 4, x0, z0 }, { T::BoxSide, 2 | 4, x1, z0 }, { T::BoxSide, 1 | 8, x0, z1 }, { T::BoxSide, 2 | 8, x1, z1 },
		        { T::BoxSide, 1, x0, mz }, { T::BoxSide, 2, x1, mz }, { T::BoxSide, 4, mx, z0 }, { T::BoxSide, 8, mx, z1 } };
		break;
	}
	case Shape::OrientedBox:
	{
		const auto [fx, fz] = forward(g.yaw);
		const double reach = g.b.z * 0.5 + 1.5;
		out.push_back({ T::Yaw, 0, g.a.x + fx * reach, g.a.z + fz * reach });
		break;
	}
	case Shape::Point:
		if (g.hasYaw)
		{
			const auto [fx, fz] = forward(g.yaw);
			const double reach = std::max(2.0, g.radius * 0.5);
			out.push_back({ T::Yaw, 0, g.a.x + fx * reach, g.a.z + fz * reach });
		}
		if (g.hasRadius && g.radius > 0.0) { out.push_back({ T::Radius, 0, g.a.x + g.radius, g.a.z }); }
		break;
	case Shape::Segment:
	case Shape::Wall:
		out = { { T::Vertex, 0, g.a.x, g.a.z }, { T::Vertex, 1, g.b.x, g.b.z } };
		break;
	case Shape::Polyline:
		for (std::size_t i = 0; i < g.pts.size(); ++i) { out.push_back({ T::Vertex, static_cast<int>(i), g.pts[i].x, g.pts[i].z }); }
		break;
	}
	return out;
}

Geom dragged(const Geom& base, const Handle& h, double wx, double wz, const Snap& snap)
{
	Geom out = base;
	switch (h.type)
	{
	case Handle::Type::BoxSide:
	{
		Vec3 lo{ std::min(base.a.x, base.b.x), base.a.y, std::min(base.a.z, base.b.z) };
		Vec3 hi{ std::max(base.a.x, base.b.x), base.b.y, std::max(base.a.z, base.b.z) };
		const double sx = snapTo(wx, snap.size);
		const double sz = snapTo(wz, snap.size);
		const double gap = minSize(snap);
		if ((h.id & 1) != 0) { lo.x = std::min(sx, hi.x - gap); }
		if ((h.id & 2) != 0) { hi.x = std::max(sx, lo.x + gap); }
		if ((h.id & 4) != 0) { lo.z = std::min(sz, hi.z - gap); }
		if ((h.id & 8) != 0) { hi.z = std::max(sz, lo.z + gap); }
		out.a = Vec3{ lo.x, std::min(base.a.y, base.b.y), lo.z };
		out.b = Vec3{ hi.x, std::max(base.a.y, base.b.y), hi.z };
		break;
	}
	case Handle::Type::Vertex:
	{
		const double sx = snapTo(wx, snap.move);
		const double sz = snapTo(wz, snap.move);
		if (base.shape == Shape::Polyline)
		{
			if (h.id >= 0 && static_cast<std::size_t>(h.id) < out.pts.size()) { out.pts[static_cast<std::size_t>(h.id)].x = sx; out.pts[static_cast<std::size_t>(h.id)].z = sz; }
		}
		else
		{
			Vec3& v = h.id == 0 ? out.a : out.b;
			v.x = sx;
			v.z = sz;
		}
		break;
	}
	case Handle::Type::Yaw:
		out.yaw = normalizedYaw(snapTo(std::atan2(wx - base.a.x, wz - base.a.z) / kDegToRad, snap.angle));
		break;
	case Handle::Type::Radius:
		out.radius = std::max(snap.size, snapTo(std::hypot(wx - base.a.x, wz - base.a.z), snap.size));
		break;
	}
	return out;
}

double distanceTo(const Geom& g, double wx, double wz, double* area)
{
	double ignored = 0.0;
	double& a = area != nullptr ? *area : ignored;
	switch (g.shape)
	{
	case Shape::Box:
	{
		const Aabb b = boundsOf(g);
		a = (b.hi.x - b.lo.x) * (b.hi.z - b.lo.z);
		return distanceToRect(wx, wz, b.lo.x, b.lo.z, b.hi.x, b.hi.z);
	}
	case Shape::OrientedBox:
	{
		const auto [fx, fz] = forward(g.yaw);
		const double dx = wx - g.a.x;
		const double dz = wz - g.a.z;
		const double lx = dx * fz - dz * fx;
		const double lz = dx * fx + dz * fz;
		a = g.b.x * g.b.z;
		return distanceToRect(lx, lz, -g.b.x * 0.5, -g.b.z * 0.5, g.b.x * 0.5, g.b.z * 0.5);
	}
	case Shape::Point:
	{
		const double d = std::hypot(wx - g.a.x, wz - g.a.z);
		a = 0.0;
		return g.hasRadius && g.radius > 0.0 ? std::min(d, std::fabs(d - g.radius)) : d;
	}
	case Shape::Segment:
	case Shape::Wall:
		a = 0.0;
		return distanceToSegment(wx, wz, g.a.x, g.a.z, g.b.x, g.b.z);
	case Shape::Polyline:
	{
		a = 0.0;
		double best = 1e18;
		for (std::size_t i = 0; i < g.pts.size(); ++i)
		{
			const Vec3& p = g.pts[i];
			const Vec3& q = g.pts[i + 1 < g.pts.size() ? i + 1 : i];
			best = std::min(best, distanceToSegment(wx, wz, p.x, p.z, q.x, q.z));
		}
		return best;
	}
	}
	return 1e18;
}

} // namespace mitiru::tool::stageedit
