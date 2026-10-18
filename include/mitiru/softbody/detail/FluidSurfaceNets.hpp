#pragma once

/// @file FluidSurfaceNets.hpp
/// @brief 粗い格子の距離から Surface Nets (Gibson 1998) で網を作る (FluidSurface.hpp から読む)。
/// @details 面の近くの粗いセルだけを 2 × 2 × 2 の細かいセルに割る。面の近くとは、角の符号が混ざるか、手足の近くで
///          角の距離が粗い格子 1 つ分より小さいセル (fluidFindSlots)。細かい格子点の値は位置だけで決まる (3 線形補間は x、y、z の順に
///          a (1 - t) + b t で、t は 0・½・1 だけ) ので、隣のセルから測っても同じビットになり、符号の判定が食い違わない。
///          頂点の番号はセルの順に振るので、法線と動きは粗いセルごとに分けて計算しても同じビットになる。

#include <algorithm>
#include <bit>
#include <cmath>

namespace mitiru::softbody::detail
{

[[nodiscard]] inline int fluidNode(const FluidSurfaceScratch& w, int x, int y, int z) noexcept
{
	return (z * w.dim[1] + y) * w.dim[0] + x;
}

[[nodiscard]] inline float fluidLerp(float a, float b, float t) noexcept { return a * (1.0f - t) + b * t; }

/// @brief 粗いセルの 8 つの角の、field (stride おきに並ぶ) の値。添字は x + 2 y + 4 z
inline void fluidCorners(const FluidSurfaceScratch& w, const float* field, int stride, int node, float (&out)[8]) noexcept
{
	const int sy = w.dim[0], sz = w.dim[0] * w.dim[1];
	const int idx[8] = {node, node + 1, node + sy, node + sy + 1, node + sz, node + sz + 1, node + sy + sz, node + sy + sz + 1};
	for (int c = 0; c < 8; ++c) { out[c] = field[static_cast<std::size_t>(idx[c]) * static_cast<std::size_t>(stride)]; }
}

/// @brief 角の値の 3 線形補間 (x、y、z の順)
[[nodiscard]] inline float fluidCornerLerp(const float (&c)[8], const Vec3& t) noexcept
{
	const float c0 = fluidLerp(fluidLerp(c[0], c[1], t.x), fluidLerp(c[2], c[3], t.x), t.y);
	const float c1 = fluidLerp(fluidLerp(c[4], c[5], t.x), fluidLerp(c[6], c[7], t.x), t.y);
	return fluidLerp(c0, c1, t.z);
}

/// @brief 粗い格子点の粒の面の距離の中心差分 (端は片側)
[[nodiscard]] inline Vec3 fluidNodeGradient(const FluidSurfaceScratch& w, int x, int y, int z) noexcept
{
	const auto at = [&w](int a, int b, int c) {
		return w.body[fluidNode(w, std::clamp(a, 0, w.dim[0] - 1), std::clamp(b, 0, w.dim[1] - 1), std::clamp(c, 0, w.dim[2] - 1))];
	};
	return {at(x + 1, y, z) - at(x - 1, y, z), at(x, y + 1, z) - at(x, y - 1, z), at(x, y, z + 1) - at(x, y, z - 1)};
}

/// @brief 粗いセル cell の中の値を変えうる手足の印 (ビット i が形 i)。箱がセルに重ならない形と、セルの中のどこでも
///        「形の距離 ≥ 粒の面の距離の最大 + limbFillet」の形は落とす (滑らかな最小がちょうど粒の面の値になり、ビットも変わらない)。
///        形の距離の下限は、セルの中心の距離から中心と角の距離の 1.5 倍を引いたもの
[[nodiscard]] inline std::uint32_t fluidCellShapes(const FluidParams& k, const FluidSurfaceScratch& w, const int (&cell)[3], const float (&body)[8]) noexcept
{
	const Vec3 lo = w.origin + Vec3{static_cast<float>(cell[0]), static_cast<float>(cell[1]), static_cast<float>(cell[2])} * w.voxel;
	const Vec3 hi = lo + Vec3{w.voxel, w.voxel, w.voxel};
	float bodyMax = body[0];
	for (const float v : body) { bodyMax = std::fmax(bodyMax, v); }
	const float reach = 1.5f * 0.8660254f * w.voxel;
	std::uint32_t mask = 0;
	for (int i = 0; i < w.shapeCount; ++i)
	{
		const FluidSurfaceShape& sh = w.shapes[i];
		const bool apart = hi.x < sh.boxLo.x || hi.y < sh.boxLo.y || hi.z < sh.boxLo.z || lo.x > sh.boxHi.x || lo.y > sh.boxHi.y || lo.z > sh.boxHi.z;
		if (apart || fluidShapeDistance(sh.now, (lo + hi) * 0.5f) - reach >= bodyMax + k.limbFillet) { continue; }
		mask |= 1u << i;
	}
	return mask;
}

/// @brief 形の距離を 4 点まとめて測る (範囲の箱の外の点は 1000)。fluidShapesMin の 1 つの形の分と同じ式
[[nodiscard]] inline F4 fluidShapeDistance4(const FluidSurfaceShape& sh, const F4x3& p) noexcept
{
	const FluidShape& s = sh.now;
	F4 d = F4::splat(1e3f);
	if (s.kind == FluidShape::Kind::Capsule)
	{
		const Vec3 ba = s.b - s.a;
		const float bb = std::fmax(ba.dot(ba), 1e-9f);
		const F4 px = p.x - F4::splat(s.a.x), py = p.y - F4::splat(s.a.y), pz = p.z - F4::splat(s.a.z);
		const F4 h = min(max((px * F4::splat(ba.x) + py * F4::splat(ba.y) + pz * F4::splat(ba.z)) / F4::splat(bb), F4::splat(0.0f)), F4::splat(1.0f));
		const F4 qx = px - F4::splat(ba.x) * h, qy = py - F4::splat(ba.y) * h, qz = pz - F4::splat(ba.z) * h;
		d = sqrt(qx * qx + qy * qy + qz * qz) - (F4::splat(s.ra) + F4::splat(s.rb - s.ra) * h);
	}
	else if (s.kind == FluidShape::Kind::Ellipsoid)
	{
		const F4 px = p.x - F4::splat(s.a.x), py = p.y - F4::splat(s.a.y), pz = p.z - F4::splat(s.a.z);
		const auto axis = [&](const Vec3& c) { return px * F4::splat(c.x) + py * F4::splat(c.y) + pz * F4::splat(c.z); };
		const F4 lx = axis(s.axes.c0), ly = axis(s.axes.c1), lz = axis(s.axes.c2);
		const F4 ax = lx / F4::splat(s.half.x), ay = ly / F4::splat(s.half.y), az = lz / F4::splat(s.half.z);
		const F4 bx = lx / F4::splat(s.half.x * s.half.x), by = ly / F4::splat(s.half.y * s.half.y), bz = lz / F4::splat(s.half.z * s.half.z);
		const F4 k0 = sqrt(ax * ax + ay * ay + az * az), k1 = sqrt(bx * bx + by * by + bz * bz);
		d = k0 * (k0 - F4::splat(1.0f)) / max(k1, F4::splat(1e-6f));
	}
	else
	{
		float x[4], y[4], z[4], out[4];
		p.x.store(x);
		p.y.store(y);
		p.z.store(z);
		for (int l = 0; l < 4; ++l) { out[l] = fluidShapeDistance(s, Vec3{x[l], y[l], z[l]}); }
		d = F4::load(out);
	}
	const F4 big = F4::splat(1e3f);
	d = ifLess(p.x, F4::splat(sh.boxLo.x), big, d);
	d = ifLess(p.y, F4::splat(sh.boxLo.y), big, d);
	d = ifLess(p.z, F4::splat(sh.boxLo.z), big, d);
	d = ifLess(F4::splat(sh.boxHi.x), p.x, big, d);
	d = ifLess(F4::splat(sh.boxHi.y), p.y, big, d);
	return ifLess(F4::splat(sh.boxHi.z), p.z, big, d);
}

/// @brief 粗いセルを 2 × 2 × 2 に割った 27 の格子点の距離を測る。角は粗い格子の phi をそのまま使い、
///        そのほかは粒の面の 3 線形補間に、セルの値を変えうる手足 (slotShapes) を滑らかに足す
inline void fluidFineValues(const FluidParams& k, const FluidSurfaceScratch& w, int slot, float (&out)[27]) noexcept
{
	float c[8];
	fluidCorners(w, w.body, 1, w.slotCell[slot], c);
	constexpr float kT[3] = {0.0f, 0.5f, 1.0f};
	float e[4][3];
	for (int r = 0; r < 4; ++r)
	{
		for (int a = 0; a < 3; ++a) { e[r][a] = fluidLerp(c[2 * r], c[2 * r + 1], kT[a]); }
	}
	for (int zc = 0; zc < 3; ++zc)
	{
		for (int b = 0; b < 3; ++b)
		{
			for (int a = 0; a < 3; ++a)
			{
				const float lo = fluidLerp(e[0][a], e[1][a], kT[b]), hi = fluidLerp(e[2][a], e[3][a], kT[b]);
				out[(zc * 3 + b) * 3 + a] = fluidLerp(lo, hi, kT[zc]);
			}
		}
	}
	if (w.slotShapes[slot] == 0) { return; }
	const int* cell = w.slotXyz[slot];
	alignas(16) float px[28], py[28], pz[28], dmin[28];
	for (int n = 0; n < 28; ++n)
	{
		const int a = n % 3, b = (n / 3) % 3, zc = n / 9;
		px[n] = w.origin.x + static_cast<float>(2 * cell[0] + a) * (0.5f * w.voxel);
		py[n] = w.origin.y + static_cast<float>(2 * cell[1] + b) * (0.5f * w.voxel);
		pz[n] = w.origin.z + static_cast<float>(2 * cell[2] + zc) * (0.5f * w.voxel);
		dmin[n] = 1e3f;
	}
	for (std::uint32_t m = w.slotShapes[slot]; m != 0; m &= m - 1)
	{
		const FluidSurfaceShape& sh = w.shapes[std::countr_zero(m)];
		for (int n = 0; n < 28; n += 4)
		{
			const F4x3 p{F4::load(px + n), F4::load(py + n), F4::load(pz + n)};
			min(F4::load(dmin + n), fluidShapeDistance4(sh, p)).store(dmin + n);
		}
	}
	for (int n = 0; n < 27; ++n)
	{
		const int a = n % 3, b = (n / 3) % 3, zc = n / 9;
		if (((a | b | zc) & 1) == 0) { out[n] = w.phi[fluidNode(w, cell[0] + a / 2, cell[1] + b / 2, cell[2] + zc / 2)]; }
		else if (dmin[n] < 1e3f) { out[n] = smoothMin(out[n], dmin[n], k.limbFillet); }
	}
}

/// @brief 手足の形が動いた量。前の手足の同じ所 (筒は軸の割合と半径の割合、楕円体は半径の割合) から測る
[[nodiscard]] inline Vec3 fluidShapeMotion(const FluidSurfaceShape& sh, const Vec3& p) noexcept
{
	if (sh.hasBefore == 0) { return {}; }
	const FluidShape& a = sh.now;
	const FluidShape& b = sh.before;
	if (a.kind == FluidShape::Kind::Capsule)
	{
		const Vec3 axis = a.b - a.a;
		const float h = clamp01((p - a.a).dot(axis) / std::fmax(axis.dot(axis), 1e-9f));
		const float r = a.ra + (a.rb - a.ra) * h, rb = b.ra + (b.rb - b.ra) * h;
		const Vec3 radial = p - (a.a + axis * h);
		return p - (b.a + (b.b - b.a) * h + radial * (r > 1e-6f ? rb / r : 1.0f));
	}
	const Vec3 l = toLocal(a.axes, p - a.a);
	const Vec3 q{l.x / a.half.x * b.half.x, l.y / a.half.y * b.half.y, l.z / a.half.z * b.half.z};
	return p - (b.a + toWorld(b.axes, q));
}

/// @brief 形の距離が増える向き (単位)。筒は軸から離れる向き、楕円体は半径で割った位置の向き
[[nodiscard]] inline Vec3 fluidShapeDirection(const FluidShape& s, const Vec3& p) noexcept
{
	if (s.kind == FluidShape::Kind::Capsule)
	{
		const Vec3 axis = s.b - s.a;
		const float h = clamp01((p - s.a).dot(axis) / std::fmax(axis.dot(axis), 1e-9f));
		return normalizeOr(p - (s.a + axis * h));
	}
	const Vec3 l = toLocal(s.axes, p - s.a);
	return normalizeOr(toWorld(s.axes, Vec3{l.x / (s.half.x * s.half.x), l.y / (s.half.y * s.half.y), l.z / (s.half.z * s.half.z)}));
}

/// @brief 粗いセル 1 つ分の、法線と動きを測るための角の値
struct FluidCellCorners
{
	Vec3 grad[8];
	float body[8];
	float weight[8];
	float move[3][8];
};

inline void fluidLoadCorners(const FluidSurfaceScratch& w, int slot, FluidCellCorners& out) noexcept
{
	const int* cell = w.slotXyz[slot];
	const int node = w.slotCell[slot];
	const bool inner = cell[0] > 0 && cell[1] > 0 && cell[2] > 0 && cell[0] + 2 < w.dim[0] && cell[1] + 2 < w.dim[1] && cell[2] + 2 < w.dim[2];
	const int sy = w.dim[0], sz = w.dim[0] * w.dim[1];
	for (int c = 0; c < 8; ++c)
	{
		const int x = cell[0] + (c & 1), y = cell[1] + ((c >> 1) & 1), z = cell[2] + ((c >> 2) & 1);
		if (!inner)
		{
			out.grad[c] = fluidNodeGradient(w, x, y, z);
			continue;
		}
		const int i = fluidNode(w, x, y, z);
		out.grad[c] = {w.body[i + 1] - w.body[i - 1], w.body[i + sy] - w.body[i - sy], w.body[i + sz] - w.body[i - sz]};
	}
	fluidCorners(w, w.body, 1, node, out.body);
	fluidCorners(w, &w.field[0][0], 8, node, out.weight);
	for (int a = 0; a < 3; ++a) { fluidCorners(w, &w.field[0][4 + a], 8, node, out.move[a]); }
}

/// @brief 点 p (セルの中の位置 t) の外向きの法線と、直前の fluidStep での動き。手足の近くでは手足の側と滑らかに混ぜる
inline void fluidSurfacePoint(const FluidParams& k, const FluidSurfaceScratch& w, int slot, const FluidCellCorners& c, const Vec3& p,
                              const Vec3& t, Vec3& normal, Vec3& motion) noexcept
{
	Vec3 gradient{};
	for (int i = 0; i < 8; ++i)
	{
		const float wt = ((i & 1) != 0 ? t.x : 1.0f - t.x) * ((i & 2) != 0 ? t.y : 1.0f - t.y) * ((i & 4) != 0 ? t.z : 1.0f - t.z);
		gradient += c.grad[i] * wt;
	}
	const float m = fluidCornerLerp(c.weight, t);
	motion = m > 1e-6f ? Vec3{fluidCornerLerp(c.move[0], t), fluidCornerLerp(c.move[1], t), fluidCornerLerp(c.move[2], t)} / m : Vec3{};
	int list[kFluidMaxSurfaceShapes];
	int listCount = 0;
	for (std::uint32_t m = w.slotShapes[slot]; m != 0; m &= m - 1) { list[listCount++] = std::countr_zero(m); }
	int which = -1;
	const float shapes = listCount > 0 ? fluidShapesMin(w, p, &which, list, listCount) : 1e3f;
	if (which >= 0)
	{
		const float h = clamp01(0.5f + 0.5f * (shapes - fluidCornerLerp(c.body, t)) / k.limbFillet);
		const Vec3 gs = fluidShapeDirection(w.shapes[which].now, p) * (2.0f * w.voxel);
		gradient = gradient * h + gs * (1.0f - h);
		motion = motion * h + fluidShapeMotion(w.shapes[which], p) * (1.0f - h);
	}
	normal = normalizeOr(gradient);
}

inline constexpr int kFluidCubeEdges[12][2] = {
	{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
};

[[nodiscard]] inline float fluidSlotPhi(const FluidSurfaceScratch& w, int slot, int a, int b, int c) noexcept
{
	return w.slotPhi[slot][(c * 3 + b) * 3 + a];
}

/// @brief 27 の細かい格子点のうち内 (負) のものの印と、面をまたぐ細かいセルの印 (8 ビット)
inline void fluidSlotSigns(FluidSurfaceScratch& w, int slot) noexcept
{
	std::uint32_t neg = 0;
	for (int n = 0; n < 27; ++n) { neg |= (w.slotPhi[slot][n] < 0.0f ? 1u : 0u) << n; }
	std::uint32_t mixed = 0;
	for (int sub = 0; sub < 8; ++sub)
	{
		std::uint32_t corners = 0;
		for (int c = 0; c < 8; ++c)
		{
			const int a = (sub & 1) + (c & 1), b = ((sub >> 1) & 1) + ((c >> 1) & 1), z = ((sub >> 2) & 1) + ((c >> 2) & 1);
			corners |= 1u << ((z * 3 + b) * 3 + a);
		}
		const std::uint32_t in = neg & corners;
		mixed |= (in != 0 && in != corners ? 1u : 0u) << sub;
	}
	w.slotNeg[slot] = neg;
	w.slotMixed[slot] = static_cast<std::uint8_t>(mixed);
}

/// @brief 細かいセル sub (面をまたぐもの) の頂点の、粗いセルの中の位置 (0〜1)。辺が面を横切る点の平均
[[nodiscard]] inline Vec3 fluidCellVertexLocal(const FluidSurfaceScratch& w, int slot, int sub) noexcept
{
	const int sa = sub & 1, sb = (sub >> 1) & 1, sc = (sub >> 2) & 1;
	float v[8];
	for (int c = 0; c < 8; ++c) { v[c] = fluidSlotPhi(w, slot, sa + (c & 1), sb + ((c >> 1) & 1), sc + ((c >> 2) & 1)); }
	Vec3 sum{};
	int crossings = 0;
	for (const auto& e : kFluidCubeEdges)
	{
		const float a = v[e[0]], b = v[e[1]];
		if ((a < 0.0f) == (b < 0.0f)) { continue; }
		const float t = a / (a - b);
		const Vec3 pa{static_cast<float>(e[0] & 1), static_cast<float>((e[0] >> 1) & 1), static_cast<float>((e[0] >> 2) & 1)};
		const Vec3 pb{static_cast<float>(e[1] & 1), static_cast<float>((e[1] >> 1) & 1), static_cast<float>((e[1] >> 2) & 1)};
		sum += pa + (pb - pa) * t;
		++crossings;
	}
	return (Vec3{static_cast<float>(sa), static_cast<float>(sb), static_cast<float>(sc)} + sum / static_cast<float>(crossings)) * 0.5f;
}

/// @brief 粗いセル slot の頂点を置き、法線と動きを測る。番号は slotFirst[slot] から面をまたぐ細かいセルの順
inline void fluidSlotVertices(const FluidParams& k, FluidSurfaceScratch& w, int slot, FluidCellCorners& corners, FluidMesh& out) noexcept
{
	int id = w.slotFirst[slot];
	const int* cell = w.slotXyz[slot];
	const Vec3 base{static_cast<float>(cell[0]), static_cast<float>(cell[1]), static_cast<float>(cell[2])};
	bool loaded = false;
	for (int sub = 0; sub < 8; ++sub)
	{
		w.slotVertex[slot][sub] = -1;
		if (((w.slotMixed[slot] >> sub) & 1u) == 0 || id >= kFluidMeshMaxVertices) { continue; }
		if (!loaded)
		{
			fluidLoadCorners(w, slot, corners);
			loaded = true;
		}
		const Vec3 local = fluidCellVertexLocal(w, slot, sub);
		const Vec3 p = w.origin + (base + local) * w.voxel;
		Vec3 motion{};
		out.positions[id] = p;
		fluidSurfacePoint(k, w, slot, corners, p, local, out.normals[id], motion);
		out.previousPositions[id] = p - motion;
		w.slotVertex[slot][sub] = id++;
	}
}

/// @brief 細かいセル (fx, fy, fz) の頂点の番号 (無ければ -1)
[[nodiscard]] inline int fluidVertexOf(const FluidSurfaceScratch& w, int fx, int fy, int fz) noexcept
{
	const int slot = w.cellSlot[fluidNode(w, fx >> 1, fy >> 1, fz >> 1)] - 1;
	return slot < 0 ? -1 : w.slotVertex[slot][(fx & 1) | ((fy & 1) << 1) | ((fz & 1) << 2)];
}

/// @brief 粗いセル slot の細かいセルの最小の角から出る辺のうち、面をまたぐものに四角形 (三角形 2 枚) を張る。
///        indices が null なら数だけを返す。書くときは indices の先頭から書き、上限 limit を超えない
inline int fluidSlotQuads(const FluidSurfaceScratch& w, int slot, std::uint32_t* indices, int limit) noexcept
{
	const int* cell = w.slotXyz[slot];
	const std::uint32_t neg = w.slotNeg[slot];
	int written = 0;
	for (int sub = 0; sub < 8; ++sub)
	{
		if (w.slotVertex[slot][sub] < 0) { continue; }
		const int s[3] = {sub & 1, (sub >> 1) & 1, (sub >> 2) & 1};
		const int f[3] = {2 * cell[0] + s[0], 2 * cell[1] + s[1], 2 * cell[2] + s[2]};
		const bool in0 = ((neg >> ((s[2] * 3 + s[1]) * 3 + s[0])) & 1u) != 0;
		for (int axis = 0; axis < 3; ++axis)
		{
			const int u = (axis + 1) % 3, vv = (axis + 2) % 3;
			if (f[u] == 0 || f[vv] == 0) { continue; }
			int n1[3] = {s[0], s[1], s[2]};
			++n1[axis];
			if (in0 == (((neg >> ((n1[2] * 3 + n1[1]) * 3 + n1[0])) & 1u) != 0)) { continue; }
			int c[4][3] = {{f[0], f[1], f[2]}, {f[0], f[1], f[2]}, {f[0], f[1], f[2]}, {f[0], f[1], f[2]}};
			--c[0][u];
			--c[0][vv];
			--c[1][vv];
			--c[3][u];
			int id[4];
			bool ok = true;
			for (int q = 0; q < 4 && ok; ++q) { ok = (id[q] = fluidVertexOf(w, c[q][0], c[q][1], c[q][2])) >= 0; }
			if (!ok || written + 6 > limit) { continue; }
			if (indices != nullptr)
			{
				constexpr int kOrder[6] = {0, 1, 2, 0, 2, 3};
				for (int tri = 0; tri < 6; ++tri) { indices[written + tri] = static_cast<std::uint32_t>(id[kOrder[in0 ? tri : 5 - tri]]); }
			}
			written += 6;
		}
	}
	return written;
}

/// @brief 粗い格子点の印 (z の層 [zBegin, zEnd))。1 は内 (距離が負)、2 は面から粗い格子 1 つ分の内、4 は手足の範囲の内。
///        行ごとに、内か面の近くの点があるかも書く
inline void fluidMarkNodes(FluidSurfaceScratch& w, int zBegin, int zEnd) noexcept
{
	const int nx = w.dim[0];
	for (int r = zBegin * w.dim[1]; r < zEnd * w.dim[1]; ++r)
	{
		std::uint8_t any = 0;
		for (int x = 0; x < nx; ++x)
		{
			const int i = r * nx + x;
			const float phi = w.phi[i];
			const auto mark = static_cast<std::uint8_t>((phi < 0.0f ? 1 : 0) | (std::fabs(phi) < w.voxel ? 2 : 0) | (w.shapeNear[i] < 1e3f ? 4 : 0));
			w.nodeMark[i] = mark;
			any |= mark;
		}
		w.rowActive[0][r] = (any & 3) != 0 ? 1 : 0;
	}
}

/// @brief 粗いセル (x, y, z) が面の近くか。面の近くとは、角の符号が混ざるか、手足の範囲の内で角の距離が粗い格子 1 つ分より
///        小さいセル (セルの中のどの点も、いちばん近い角から粗い格子の √3 / 2 倍の内にあるので、中で 0 を横切るなら角の距離は
///        それより小さい)。手足の範囲に入る角があれば shape に 1 を書く
[[nodiscard]] inline bool fluidNearCell(const FluidSurfaceScratch& w, int i, std::uint32_t& shape) noexcept
{
	const int sy = w.dim[0], sz = w.dim[0] * w.dim[1];
	const std::uint8_t* m = w.nodeMark;
	const int any = m[i] | m[i + 1] | m[i + sy] | m[i + sy + 1] | m[i + sz] | m[i + sz + 1] | m[i + sy + sz] | m[i + sy + sz + 1];
	const int all = m[i] & m[i + 1] & m[i + sy] & m[i + sy + 1] & m[i + sz] & m[i + sz + 1] & m[i + sy + sz] & m[i + sy + sz + 1];
	const bool mixed = (any & 1) != 0 && (all & 1) == 0;
	shape = (any & 4) != 0 ? 1u : 0u;
	return mixed || (any & 6) == 6;
}

/// @brief z の層のセルを順に見て、面の近くのセルを数える。write なら first から番号を振って書く。
///        内でも面の近くでもない格子点しか無い行は飛ばす
inline int fluidScanSlab(FluidSurfaceScratch& w, int z, int first, bool write) noexcept
{
	const std::uint8_t* rowNear = w.rowActive[0];
	int count = 0;
	for (int y = 0; y + 1 < w.dim[1]; ++y)
	{
		const int r = z * w.dim[1] + y;
		if ((rowNear[r] | rowNear[r + 1] | rowNear[r + w.dim[1]] | rowNear[r + w.dim[1] + 1]) == 0) { continue; }
		for (int x = 0; x + 1 < w.dim[0]; ++x)
		{
			const int i = fluidNode(w, x, y, z);
			std::uint32_t shape = 0;
			if (!fluidNearCell(w, i, shape)) { continue; }
			const int slot = first + count++;
			if (!write || slot >= kFluidMaxSlots) { continue; }
			w.slotShapes[slot] = shape;
			w.slotCell[slot] = i;
			w.slotXyz[slot][0] = x;
			w.slotXyz[slot][1] = y;
			w.slotXyz[slot][2] = z;
			w.cellSlot[i] = slot + 1;
		}
	}
	return count;
}

/// @brief 面の近くの粗いセルに番号を振る。番号はセルの順 (z の層ごとに数えて累積する) なので、層を分けて回しても同じ
inline void fluidFindSlots(FluidSurfaceScratch& w) noexcept
{
	for (int s = 0; s < w.slotCount; ++s) { w.cellSlot[w.slotCell[s]] = 0; }
	fluidForRanges(w.parallel, w.dim[2], [&](int zBegin, int zEnd) { fluidMarkNodes(w, zBegin, zEnd); }, 8);
	const int slabs = w.dim[2] - 1;
	fluidForRanges(w.parallel, slabs, [&](int zBegin, int zEnd) {
		for (int z = zBegin; z < zEnd; ++z) { w.slabCount[z] = fluidScanSlab(w, z, 0, false); }
	}, 8);
	int total = 0;
	for (int z = 0; z < slabs; ++z)
	{
		w.slabFirst[z] = total;
		total += w.slabCount[z];
	}
	w.slotCount = std::min(total, kFluidMaxSlots);
	fluidForRanges(w.parallel, slabs, [&](int zBegin, int zEnd) {
		for (int z = zBegin; z < zEnd; ++z) { fluidScanSlab(w, z, w.slabFirst[z], true); }
	}, 8);
}

/// @brief 面の近くの粗いセルを細かく測り、頂点を置き、法線と動きを測り、四角形を張る。
///        頂点と三角形の番号は「セルごとの数」の累積で決めるので、セルを分けて回しても同じ網になる
inline void fluidSurfaceNets(const FluidParams& k, FluidSurfaceScratch& w, FluidMesh& out) noexcept
{
	fluidFindSlots(w);
	fluidForRanges(w.parallel, w.slotCount, [&](int begin, int end) {
		for (int s = begin; s < end; ++s)
		{
			if (w.slotShapes[s] != 0)
			{
				float body[8];
				fluidCorners(w, w.body, 1, w.slotCell[s], body);
				w.slotShapes[s] = fluidCellShapes(k, w, w.slotXyz[s], body);
			}
			fluidFineValues(k, w, s, w.slotPhi[s]);
			fluidSlotSigns(w, s);
		}
	});
	int vertices = 0;
	for (int s = 0; s < w.slotCount; ++s)
	{
		w.slotFirst[s] = vertices;
		vertices += std::popcount(static_cast<unsigned>(w.slotMixed[s]));
	}
	out.vertexCount = std::min(vertices, kFluidMeshMaxVertices);
	out.truncated = vertices > kFluidMeshMaxVertices ? 1 : 0;
	fluidForRanges(w.parallel, w.slotCount, [&](int begin, int end) {
		FluidCellCorners corners;
		for (int s = begin; s < end; ++s) { fluidSlotVertices(k, w, s, corners, out); }
	});
	fluidForRanges(w.parallel, w.slotCount, [&](int begin, int end) {
		for (int s = begin; s < end; ++s) { w.slotQuadCount[s] = fluidSlotQuads(w, s, nullptr, 1 << 20); }
	});
	int indices = 0;
	for (int s = 0; s < w.slotCount; ++s)
	{
		w.slotFirstIndex[s] = indices;
		indices += w.slotQuadCount[s];
	}
	out.indexCount = std::min(indices, kFluidMeshMaxIndices);
	out.truncated |= indices > kFluidMeshMaxIndices ? 1 : 0;
	fluidForRanges(w.parallel, w.slotCount, [&](int begin, int end) {
		for (int s = begin; s < end; ++s)
		{
			const int first = w.slotFirstIndex[s];
			if (first < kFluidMeshMaxIndices) { fluidSlotQuads(w, s, out.indices + first, kFluidMeshMaxIndices - first); }
		}
	});
	out.boundsMin = out.vertexCount > 0 ? out.positions[0] : Vec3{};
	out.boundsMax = out.boundsMin;
	for (int i = 1; i < out.vertexCount; ++i)
	{
		const Vec3& p = out.positions[i];
		out.boundsMin = {std::fmin(out.boundsMin.x, p.x), std::fmin(out.boundsMin.y, p.y), std::fmin(out.boundsMin.z, p.z)};
		out.boundsMax = {std::fmax(out.boundsMax.x, p.x), std::fmax(out.boundsMax.y, p.y), std::fmax(out.boundsMax.z, p.z)};
	}
}

} // namespace mitiru::softbody::detail
