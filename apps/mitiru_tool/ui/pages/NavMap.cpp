#include "NavMap.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>

#ifdef MITIRU_HAS_DETOUR
#include <mitiru/nav/DynamicNavMesh.hpp>
#include <mitiru/nav/NavMesh.hpp>
#endif

namespace mitiru::tool
{

namespace
{

using Rgb = std::array<std::uint8_t, 3>;

constexpr Rgb kPaper{ 0xfc, 0xfc, 0xfb };
constexpr Rgb kGrid{ 0xee, 0xef, 0xf1 };
constexpr Rgb kMesh{ 0xe0, 0xe2, 0xe6 };
constexpr Rgb kMeshEdge{ 0x99, 0x9d, 0xa4 };
constexpr Rgb kBlockFill{ 0x6e, 0x70, 0x77 };
constexpr Rgb kInk{ 0x1a, 0x1c, 0x1f };
constexpr Rgb kOpenLine{ 0xc9, 0xcb, 0xd0 };
constexpr Rgb kIdle{ 0x6e, 0x70, 0x77 };

#ifdef MITIRU_HAS_DETOUR
void addTile(const dtMeshTile& tile, NavFootprint& out)
{
	for (int p = 0; p < tile.header->polyCount; ++p)
	{
		const dtPoly& poly = tile.polys[p];
		if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) { continue; }
		const auto at = [&](int k) { return &tile.verts[poly.verts[k] * 3]; };
		for (int k = 2; k < poly.vertCount; ++k)
		{
			for (const int v : { 0, k - 1, k }) { out.tris.push_back(at(v)[0]); out.tris.push_back(at(v)[2]); }
		}
		for (int k = 0; k < poly.vertCount; ++k)
		{
			if (poly.neis[k] != 0) { continue; }
			const float* a = at(k);
			const float* b = at((k + 1) % poly.vertCount);
			out.edges.insert(out.edges.end(), { a[0], a[2], b[0], b[2] });
		}
	}
}

void collect(const dtNavMesh& mesh, NavFootprint& out)
{
	for (int i = 0; i < mesh.getMaxTiles(); ++i)
	{
		const dtMeshTile* tile = mesh.getTile(i);
		if (tile != nullptr && tile->header != nullptr) { addTile(*tile, out); }
	}
}
#endif

/// 世界の (x, z) を画素へ写す。
struct View
{
	float minX = 0.0f;
	float minZ = 0.0f;
	float scale = 1.0f;
	float margin = 12.0f;
	[[nodiscard]] float px(float x) const noexcept { return (x - minX) * scale + margin; }
	[[nodiscard]] float py(float z) const noexcept { return (z - minZ) * scale + margin; }
};

class Canvas
{
public:
	Canvas(int w, int h) : m_w(w), m_h(h), m_rgba(static_cast<std::size_t>(w) * h * 4, 0xFF) { fill(kPaper); }

	void fill(const Rgb& c)
	{
		for (std::size_t i = 0; i < m_rgba.size(); i += 4) { std::copy(c.begin(), c.end(), m_rgba.begin() + static_cast<std::ptrdiff_t>(i)); }
	}

	void dot(int x, int y, const Rgb& c)
	{
		if (x < 0 || y < 0 || x >= m_w || y >= m_h) { return; }
		std::copy(c.begin(), c.end(), m_rgba.begin() + (static_cast<std::ptrdiff_t>(y) * m_w + x) * 4);
	}

	void line(float x0, float y0, float x1, float y1, const Rgb& c)
	{
		const int steps = static_cast<int>(std::ceil(std::max(std::fabs(x1 - x0), std::fabs(y1 - y0)))) + 1;
		for (int i = 0; i <= steps; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(steps);
			dot(static_cast<int>(std::lround(x0 + (x1 - x0) * t)), static_cast<int>(std::lround(y0 + (y1 - y0) * t)), c);
		}
	}

	/// 辺関数で塗る。向きは問わない。
	void triangle(const float* a, const float* b, const float* c, const Rgb& color)
	{
		const int x0 = std::max(0, static_cast<int>(std::floor(std::min({ a[0], b[0], c[0] }))));
		const int x1 = std::min(m_w - 1, static_cast<int>(std::ceil(std::max({ a[0], b[0], c[0] }))));
		const int y0 = std::max(0, static_cast<int>(std::floor(std::min({ a[1], b[1], c[1] }))));
		const int y1 = std::min(m_h - 1, static_cast<int>(std::ceil(std::max({ a[1], b[1], c[1] }))));
		const auto edge = [](const float* p, const float* q, float x, float y) { return (q[0] - p[0]) * (y - p[1]) - (q[1] - p[1]) * (x - p[0]); };
		for (int y = y0; y <= y1; ++y)
		{
			for (int x = x0; x <= x1; ++x)
			{
				const float fx = static_cast<float>(x) + 0.5f;
				const float fy = static_cast<float>(y) + 0.5f;
				const float e0 = edge(a, b, fx, fy), e1 = edge(b, c, fx, fy), e2 = edge(c, a, fx, fy);
				if ((e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0)) { dot(x, y, color); }
			}
		}
	}

	void disc(float cx, float cy, float r, const Rgb& c, bool hollow)
	{
		for (int y = static_cast<int>(cy - r) - 1; y <= static_cast<int>(cy + r) + 1; ++y)
		{
			for (int x = static_cast<int>(cx - r) - 1; x <= static_cast<int>(cx + r) + 1; ++x)
			{
				const float d = std::hypot(static_cast<float>(x) + 0.5f - cx, static_cast<float>(y) + 0.5f - cy);
				if (d <= r && (!hollow || d >= r - 1.5f)) { dot(x, y, c); }
			}
		}
	}

	[[nodiscard]] std::vector<std::uint8_t> take() { return std::move(m_rgba); }

private:
	int m_w;
	int m_h;
	std::vector<std::uint8_t> m_rgba;
};

struct Bounds
{
	float minX = 1e30f, maxX = -1e30f, minZ = 1e30f, maxZ = -1e30f;
	void add(float x, float z) noexcept
	{
		minX = std::min(minX, x); maxX = std::max(maxX, x);
		minZ = std::min(minZ, z); maxZ = std::max(maxZ, z);
	}
	[[nodiscard]] bool empty() const noexcept { return minX > maxX; }
};

Bounds sceneBounds(const NavFootprint& mesh, const std::vector<MapAgent>& agents, const std::vector<MapObstacle>& obstacles)
{
	Bounds b;
	for (std::size_t i = 0; i + 1 < mesh.tris.size(); i += 2) { b.add(mesh.tris[i], mesh.tris[i + 1]); }
	for (const MapAgent& a : agents) { b.add(a.x, a.z); }
	for (const MapObstacle& o : obstacles)
	{
		const float r = std::hypot(o.hx, o.hz);
		b.add(o.cx - r, o.cz - r);
		b.add(o.cx + r, o.cz + r);
	}
	if (b.empty()) { b.add(-10.0f, -10.0f); b.add(10.0f, 10.0f); }
	b.add(b.minX - 1.0f, b.minZ - 1.0f);
	b.add(b.maxX + 1.0f, b.maxZ + 1.0f);
	return b;
}

float gridStep(float span) noexcept
{
	for (const float g : { 1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f, 100.0f }) { if (span / g <= 24.0f) { return g; } }
	return 200.0f;
}

void drawGrid(Canvas& c, const View& v, const Bounds& b, float step, int w, int h)
{
	for (float x = std::ceil(b.minX / step) * step; x <= b.maxX; x += step) { c.line(v.px(x), 0, v.px(x), static_cast<float>(h - 1), kGrid); }
	for (float z = std::ceil(b.minZ / step) * step; z <= b.maxZ; z += step) { c.line(0, v.py(z), static_cast<float>(w - 1), v.py(z), kGrid); }
}

void drawMesh(Canvas& c, const View& v, const NavFootprint& mesh)
{
	for (std::size_t i = 0; i + 5 < mesh.tris.size(); i += 6)
	{
		const float a[2] = { v.px(mesh.tris[i]), v.py(mesh.tris[i + 1]) };
		const float b[2] = { v.px(mesh.tris[i + 2]), v.py(mesh.tris[i + 3]) };
		const float d[2] = { v.px(mesh.tris[i + 4]), v.py(mesh.tris[i + 5]) };
		c.triangle(a, b, d, kMesh);
	}
	for (std::size_t i = 0; i + 3 < mesh.edges.size(); i += 4)
	{
		c.line(v.px(mesh.edges[i]), v.py(mesh.edges[i + 1]), v.px(mesh.edges[i + 2]), v.py(mesh.edges[i + 3]), kMeshEdge);
	}
}

void drawObstacle(Canvas& c, const View& v, const MapObstacle& o)
{
	const Rgb& edge = o.enabled ? kInk : kOpenLine;
	if (o.cylinder)
	{
		if (o.enabled) { c.disc(v.px(o.cx), v.py(o.cz), o.hx * v.scale, kBlockFill, false); }
		c.disc(v.px(o.cx), v.py(o.cz), o.hx * v.scale, edge, true);
		return;
	}
	const float cs = std::cos(o.yaw), sn = std::sin(o.yaw);
	float p[4][2];
	const float lx[4] = { -o.hx, o.hx, o.hx, -o.hx };
	const float lz[4] = { -o.hz, -o.hz, o.hz, o.hz };
	for (int k = 0; k < 4; ++k)
	{
		p[k][0] = v.px(o.cx + cs * lx[k] + sn * lz[k]);
		p[k][1] = v.py(o.cz - sn * lx[k] + cs * lz[k]);
	}
	if (o.enabled) { c.triangle(p[0], p[1], p[2], kBlockFill); c.triangle(p[0], p[2], p[3], kBlockFill); }
	for (int k = 0; k < 4; ++k) { c.line(p[k][0], p[k][1], p[(k + 1) % 4][0], p[(k + 1) % 4][1], edge); }
}

void drawAgent(Canvas& c, const View& v, const MapAgent& a)
{
	const float x = v.px(a.x), y = v.py(a.z);
	c.line(x, y, v.px(a.x + a.vx * 0.5f), v.py(a.z + a.vz * 0.5f), kInk);
	c.disc(x, y, 3.5f, a.moving ? kInk : kIdle, a.stuck);
}

} // namespace

std::string loadNavFootprint(const std::string& path, NavFootprint& out)
{
	out = NavFootprint{};
#ifdef MITIRU_HAS_DETOUR
	std::string error;
	nav::NavMesh mesh;
	if (mesh.loadFile(path.c_str(), &error))
	{
		collect(*mesh.query().detour()->getAttachedNavMesh(), out);
		return {};
	}
	nav::DynamicNavMesh dynamic;
	std::string dynamicError;
	if (dynamic.loadFile(path.c_str(), &dynamicError))
	{
		collect(*dynamic.detour(), out);
		return {};
	}
	return error + " / " + dynamicError;
#else
	(void)path;
	return "この mitiru_tool は Detour 無しでビルドされたのでナビメッシュを読めない";
#endif
}

MapImage renderNavMap(const NavFootprint& mesh, const std::vector<MapAgent>& agents,
                      const std::vector<MapObstacle>& obstacles, int width)
{
	const Bounds b = sceneBounds(mesh, agents, obstacles);
	View v{ b.minX, b.minZ, 1.0f, 12.0f };
	const float dx = b.maxX - b.minX, dz = b.maxZ - b.minZ;
	const float inner = static_cast<float>(width) - 2.0f * v.margin;
	v.scale = std::min(inner / dx, 640.0f / dz);
	const int height = static_cast<int>(std::ceil(dz * v.scale + 2.0f * v.margin));

	Canvas c(width, height);
	const float step = gridStep(std::max(dx, dz));
	drawGrid(c, v, b, step, width, height);
	drawMesh(c, v, mesh);
	for (const MapObstacle& o : obstacles) { drawObstacle(c, v, o); }
	for (const MapAgent& a : agents) { drawAgent(c, v, a); }
	return MapImage{ width, height, c.take(), step, v.minX - v.margin / v.scale, v.minZ - v.margin / v.scale, v.scale };
}

} // namespace mitiru::tool
