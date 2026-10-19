#pragma once

/// @file FluidField.hpp
/// @brief 面の距離を粗い格子に作る (FluidSurface.hpp から読む)。

#include <algorithm>
#include <cmath>

namespace mitiru::softbody::detail
{

/// @brief 粒の位置を近くの粒の重み付き平均へ renderSmoothing だけ寄せる。粒の動き (直前の fluidStep の分) も写す
inline void fluidSmoothPositions(const FluidBodyState& s, const FluidParams& k, FluidSurfaceScratch& w) noexcept
{
	const int n = s.count;
	const float r2 = k.renderSmoothRadius * k.renderSmoothRadius;
	fluidFindNeighbors(w.cache, s.x, n, k.renderSmoothRadius, w.pairs, w.parallel);
	const float lam = k.renderSmoothing;
	fluidForRanges(w.parallel, n, [&](int begin, int end) {
		for (int i = begin; i < end; ++i)
		{
			Vec3 sum = s.x[i];
			float wsum = 1.0f;
			for (int e = w.pairs.rowStart[i]; e < w.pairs.rowStart[i] + w.pairs.rowCount[i]; ++e)
			{
				const int j = w.pairs.j[e];
				const Vec3 d = s.x[i] - s.x[j];
				const float q = 1.0f - d.dot(d) / r2;
				const float wij = q * q * q;
				sum += s.x[j] * wij;
				wsum += wij;
			}
			w.smoothed[i] = s.x[i] * (1.0f - lam) + sum * (lam / wsum);
			w.motion[i] = s.x[i] - s.xStepStart[i];
		}
	});
}

/// @brief 面に足す形を集める (体の手足と、ゲームの形)。距離を測る範囲は形の外へ limbFillet + 粗い格子の半分
inline void fluidGatherShapes(const FluidBodyState& s, const FluidShape* extra, const FluidShape* extraBefore,
                              int extraCount, FluidSurfaceScratch& w) noexcept
{
	w.shapeCount = 0;
	const bool matched = s.limbCountBefore == s.limbCount;
	for (int i = 0; i < s.limbCount && w.shapeCount < kFluidMaxSurfaceShapes; ++i)
	{
		FluidSurfaceShape& sh = w.shapes[w.shapeCount++];
		sh.now = s.limbs[i];
		sh.before = s.limbsBefore[i];
		sh.hasBefore = matched ? 1 : 0;
	}
	for (int i = 0; i < extraCount && w.shapeCount < kFluidMaxSurfaceShapes; ++i)
	{
		FluidSurfaceShape& sh = w.shapes[w.shapeCount++];
		sh.now = extra[i];
		sh.hasBefore = extraBefore != nullptr ? 1 : 0;
		if (extraBefore != nullptr)
		{
			sh.before = extraBefore[i];
		}
	}
}

/// @brief p で範囲に入っている形の距離の最小 (無ければ大きな値)。list があればその形だけを見る (p を含みうる形を集めたもの)
[[nodiscard]] inline float fluidShapesMin(const FluidSurfaceScratch& w, const Vec3& p, int* which = nullptr, const int* list = nullptr,
                                          int listCount = -1) noexcept
{
	float best = 1e3f;
	const int count = list != nullptr ? listCount : w.shapeCount;
	for (int n = 0; n < count; ++n)
	{
		const int i = list != nullptr ? list[n] : n;
		const FluidSurfaceShape& sh = w.shapes[i];
		if (p.x < sh.boxLo.x || p.y < sh.boxLo.y || p.z < sh.boxLo.z || p.x > sh.boxHi.x || p.y > sh.boxHi.y || p.z > sh.boxHi.z) { continue; }
		const float d = fluidShapeDistance(sh.now, p);
		if (d < best)
		{
			best = d;
			if (which != nullptr) { *which = i; }
		}
	}
	return best;
}

/// @brief 粗い格子の置き場所と間隔を決める。原点は間隔の倍数にそろえ、体が動いても格子が揺れないようにする
inline void fluidPlaceField(int n, const FluidParams& k, FluidSurfaceScratch& w) noexcept
{
	Vec3 lo = w.smoothed[0], hi = w.smoothed[0];
	const auto grow = [&lo, &hi](const Vec3& a, const Vec3& b) {
		lo = {std::fmin(lo.x, a.x), std::fmin(lo.y, a.y), std::fmin(lo.z, a.z)};
		hi = {std::fmax(hi.x, b.x), std::fmax(hi.y, b.y), std::fmax(hi.z, b.z)};
	};
	for (int i = 0; i < n; ++i) { grow(w.smoothed[i], w.smoothed[i]); }
	for (int i = 0; i < w.shapeCount; ++i)
	{
		Vec3 a{}, b{};
		fluidShapeBounds(w.shapes[i].now, a, b);
		grow(a, b);
	}
	float vc = 2.0f * k.meshVoxelSize;
	const float margin = k.surfaceRadius + 3.0f * k.surfaceBlur + 2.0f * vc;
	const Vec3 ext = hi - lo + Vec3{2.0f * margin, 2.0f * margin, 2.0f * margin};
	vc = std::fmax(vc, std::cbrt(ext.x * ext.y * ext.z / static_cast<float>(kFluidFieldMaxNodes)) * 1.05f);
	vc = std::fmax(vc, std::fmax(ext.x, std::fmax(ext.y, ext.z)) / static_cast<float>(kFluidFieldMaxDim - 3));
	w.voxel = vc;
	const Vec3 start = lo - Vec3{margin, margin, margin};
	w.origin = {std::floor(start.x / vc) * vc, std::floor(start.y / vc) * vc, std::floor(start.z / vc) * vc};
	w.dim[0] = std::min(kFluidFieldMaxDim, static_cast<int>(std::ceil((hi.x + margin - w.origin.x) / vc)) + 1);
	w.dim[1] = std::min(kFluidFieldMaxDim, static_cast<int>(std::ceil((hi.y + margin - w.origin.y) / vc)) + 1);
	w.dim[2] = std::min(kFluidFieldMaxDim, static_cast<int>(std::ceil((hi.z + margin - w.origin.z) / vc)) + 1);
	while (w.dim[0] * w.dim[1] * w.dim[2] > kFluidFieldMaxNodes) { --w.dim[w.dim[0] >= w.dim[1] ? (w.dim[0] >= w.dim[2] ? 0 : 2) : (w.dim[1] >= w.dim[2] ? 1 : 2)]; }
	const float reach = k.limbFillet + 0.5f * vc;
	for (int i = 0; i < w.shapeCount; ++i)
	{
		fluidShapeBounds(w.shapes[i].now, w.shapes[i].boxLo, w.shapes[i].boxHi);
		w.shapes[i].boxLo -= Vec3{reach, reach, reach};
		w.shapes[i].boxHi += Vec3{reach, reach, reach};
	}
}

/// @brief 1 軸ぶんのガウスの重み。first は最初の格子点の番号 (書いた数を返す)
inline int fluidAxisWeights(float g, float radius, int dim, float invTwoSigma2Vox, float* out, int& first) noexcept
{
	first = std::max(0, static_cast<int>(std::ceil(g - radius)));
	const int last = std::min({dim - 1, static_cast<int>(std::floor(g + radius)), first + 31});
	int count = 0;
	for (int i = first; i <= last; ++i)
	{
		const float d = static_cast<float>(i) - g;
		out[count++] = fastExp2(-d * d * invTwoSigma2Vox * 1.44269504f);
	}
	return count;
}

/// @brief 粒の z の層 [zBegin, zEnd) の分だけを、ガウス (半径 2.5 σ で切る) で格子へ広げる。
///        重み・重み × 位置・重み × 動きを格子点ごとに並べて持つ。どの格子点も粒の番号順に足すので、層の分け方でビットは変わらない
inline void fluidSplatSlab(int n, const FluidParams& k, FluidSurfaceScratch& w, int zBegin, int zEnd) noexcept
{
	const int plane = w.dim[0] * w.dim[1];
	std::fill(&w.field[0][0] + static_cast<std::size_t>(zBegin) * plane * 8, &w.field[0][0] + static_cast<std::size_t>(zEnd) * plane * 8, 0.0f);
	const float sigmaVox = k.splatSigma / w.voxel;
	const float radius = 2.5f * sigmaVox;
	const float inv = 1.0f / (2.0f * sigmaVox * sigmaVox);
	float wx[32], wy[32], wz[32];
	for (int p = 0; p < n; ++p)
	{
		const Vec3 g = (w.smoothed[p] - w.origin) / w.voxel;
		int x0 = 0, y0 = 0, z0 = 0;
		const int nzAll = fluidAxisWeights(g.z, radius, w.dim[2], inv, wz, z0);
		const int cBegin = std::max(0, zBegin - z0), cEnd = std::min(nzAll, zEnd - z0);
		if (cBegin >= cEnd) { continue; }
		const int nx = fluidAxisWeights(g.x, radius, w.dim[0], inv, wx, x0);
		const int ny = fluidAxisWeights(g.y, radius, w.dim[1], inv, wy, y0);
		const F4 lo = F4::set(1.0f, w.smoothed[p].x, w.smoothed[p].y, w.smoothed[p].z);
		const F4 hi = F4::set(w.motion[p].x, w.motion[p].y, w.motion[p].z, 0.0f);
		for (int c = cBegin; c < cEnd; ++c)
		{
			for (int b = 0; b < ny; ++b)
			{
				const float wyz = wy[b] * wz[c];
				float(*row)[8] = w.field + ((z0 + c) * w.dim[1] + (y0 + b)) * w.dim[0] + x0;
				for (int a = 0; a < nx; ++a)
				{
					const F4 wt = F4::splat(wx[a] * wyz);
					(F4::load(row[a]) + wt * lo).store(row[a]);
					(F4::load(row[a] + 4) + wt * hi).store(row[a] + 4);
				}
			}
		}
	}
}

inline void fluidSplat(int n, const FluidParams& k, FluidSurfaceScratch& w) noexcept
{
	fluidForRanges(w.parallel, w.dim[2], [&](int begin, int end) { fluidSplatSlab(n, k, w, begin, end); }, 8);
}

/// @brief 1 軸のガウスのぼかし (端は端の値を伸ばす)。active は行 (y, z) に外 (0.3) 以外の値があるか。
///        ぼかした後に値が変わりうる行を next に書く
inline void fluidBlurAxis(const float* src, float* dst, const int (&dim)[3], int axis, const float* kernel, int radius,
                          const std::uint8_t* active, std::uint8_t* next, int zBegin, int zEnd) noexcept
{
	const int nx = dim[0], ny = dim[1], nz = dim[2];
	for (int z = zBegin; z < zEnd; ++z)
	{
		for (int y = 0; y < ny; ++y)
		{
			bool any = active[z * ny + y] != 0;
			for (int o = -radius; o <= radius && !any && axis != 0; ++o)
			{
				const int yy = axis == 1 ? std::clamp(y + o, 0, ny - 1) : y;
				const int zz = axis == 2 ? std::clamp(z + o, 0, nz - 1) : z;
				any = active[zz * ny + yy] != 0;
			}
			next[z * ny + y] = any ? 1 : 0;
			float* out = dst + (z * ny + y) * nx;
			if (!any) { std::fill(out, out + nx, 0.3f); continue; }
			if (axis == 0)
			{
				const float* in = src + (z * ny + y) * nx;
				for (int x = 0; x < nx; ++x)
				{
					float acc = 0.0f;
					if (x >= radius && x + radius < nx)
					{
						for (int o = -radius; o <= radius; ++o) { acc += kernel[o + radius] * in[x + o]; }
					}
					else
					{
						for (int o = -radius; o <= radius; ++o) { acc += kernel[o + radius] * in[std::clamp(x + o, 0, nx - 1)]; }
					}
					out[x] = acc;
				}
				continue;
			}
			std::fill(out, out + nx, 0.0f);
			for (int o = -radius; o <= radius; ++o)
			{
				const int yy = axis == 1 ? std::clamp(y + o, 0, ny - 1) : y;
				const int zz = axis == 2 ? std::clamp(z + o, 0, nz - 1) : z;
				const float* in = src + (zz * ny + yy) * nx;
				const float kw = kernel[o + radius];
				for (int x = 0; x < nx; ++x) { out[x] += kw * in[x]; }
			}
		}
	}
}

/// @brief 粒の面の距離の z の層 [zBegin, zEnd)。粒の疎な所 (重みが surfaceMinWeight 以下) は外 (0.3 m) とみなす
inline void fluidBodyRows(const FluidParams& k, FluidSurfaceScratch& w, int zBegin, int zEnd) noexcept
{
	const int nx = w.dim[0], ny = w.dim[1];
	for (int z = zBegin; z < zEnd; ++z)
	{
		for (int y = 0; y < ny; ++y)
		{
			bool any = false;
			for (int x = 0; x < nx; ++x)
			{
				const int idx = (z * ny + y) * nx + x;
				const float* f = w.field[idx];
				if (f[0] <= k.surfaceMinWeight) { w.body[idx] = 0.3f; continue; }
				const Vec3 node = w.origin + Vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)} * w.voxel;
				w.body[idx] = length(node - Vec3{f[1], f[2], f[3]} / f[0]) - k.surfaceRadius;
				any = true;
			}
			w.rowActive[0][z * ny + y] = any ? 1 : 0;
		}
	}
}

/// @brief 粒の面の距離 body を作る。粒の疎な所 (重みが surfaceMinWeight 以下) は外 (0.3 m) とみなし、surfaceBlur でぼかす
inline void fluidBodyField(const FluidParams& k, FluidSurfaceScratch& w) noexcept
{
	const int nx = w.dim[0], ny = w.dim[1], nz = w.dim[2];
	fluidForRanges(w.parallel, nz, [&](int zBegin, int zEnd) { fluidBodyRows(k, w, zBegin, zEnd); }, 8);
	const float sigma = k.surfaceBlur / w.voxel;
	if (sigma <= 0.05f) { return; }
	const int radius = std::clamp(static_cast<int>(std::ceil(3.0f * sigma)), 1, 8);
	float kernel[17];
	float total = 0.0f;
	for (int o = -radius; o <= radius; ++o) { total += kernel[o + radius] = std::exp(-0.5f * static_cast<float>(o * o) / (sigma * sigma)); }
	for (int o = 0; o <= 2 * radius; ++o) { kernel[o] /= total; }
	const auto pass = [&](const float* src, float* dst, int axis, int from, int to) {
		fluidForRanges(w.parallel, nz, [&](int zBegin, int zEnd) {
			fluidBlurAxis(src, dst, w.dim, axis, kernel, radius, w.rowActive[from], w.rowActive[to], zBegin, zEnd);
		}, 8);
	};
	pass(w.body, w.tmp, 0, 0, 1);
	pass(w.tmp, w.body, 1, 1, 0);
	pass(w.body, w.tmp, 2, 0, 1);
	std::copy(w.tmp, w.tmp + nx * ny * nz, w.body);
}

/// @brief 粗い格子点の距離 phi の z の層 [zBegin, zEnd)。粒の面に手足を limbFillet で滑らかに足す (手足どうしはそのまま重ねる)
inline void fluidDistanceRows(const FluidParams& k, FluidSurfaceScratch& w, int zBegin, int zEnd) noexcept
{
	const int plane = w.dim[0] * w.dim[1];
	std::copy(w.body + zBegin * plane, w.body + zEnd * plane, w.phi + zBegin * plane);
	std::fill(w.shapeNear + zBegin * plane, w.shapeNear + zEnd * plane, 1e3f);
	for (int i = 0; i < w.shapeCount; ++i)
	{
		const FluidSurfaceShape& sh = w.shapes[i];
		const Vec3 lo = (sh.boxLo - w.origin) / w.voxel, hi = (sh.boxHi - w.origin) / w.voxel;
		const int x0 = std::max(0, static_cast<int>(std::ceil(lo.x))), x1 = std::min(w.dim[0] - 1, static_cast<int>(std::floor(hi.x)));
		const int y0 = std::max(0, static_cast<int>(std::ceil(lo.y))), y1 = std::min(w.dim[1] - 1, static_cast<int>(std::floor(hi.y)));
		const int z0 = std::max(zBegin, static_cast<int>(std::ceil(lo.z))), z1 = std::min(zEnd - 1, static_cast<int>(std::floor(hi.z)));
		for (int z = z0; z <= z1; ++z)
		{
			for (int y = y0; y <= y1; ++y)
			{
				for (int x = x0; x <= x1; ++x)
				{
					const int idx = (z * w.dim[1] + y) * w.dim[0] + x;
					const Vec3 node = w.origin + Vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)} * w.voxel;
					w.shapeNear[idx] = std::fmin(w.shapeNear[idx], fluidShapesMin(w, node));
				}
			}
		}
	}
	for (int i = zBegin * plane; i < zEnd * plane; ++i)
	{
		if (w.shapeNear[i] < 1e3f) { w.phi[i] = smoothMin(w.body[i], w.shapeNear[i], k.limbFillet); }
	}
}

inline void fluidDistance(const FluidParams& k, FluidSurfaceScratch& w) noexcept
{
	fluidBodyField(k, w);
	fluidForRanges(w.parallel, w.dim[2], [&](int zBegin, int zEnd) { fluidDistanceRows(k, w, zBegin, zEnd); }, 8);
}

} // namespace mitiru::softbody::detail
