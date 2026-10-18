#pragma once

/// @file FluidSolver.hpp
/// @brief 流れる体の 1 回の小さな刻みの中身 (FluidBody.hpp から読む)。
/// @details 粒 i の引き合い・密度・粘りは i の行だけから集めて足す (行は 4 つずつ、レーンの合計の順は決まっている)。
///          ほかの粒の値へは書かないので、粒をどう分けて回しても同じビットになる。

#include <cmath>

#include <mitiru/softbody/detail/FluidSimd.hpp>

namespace mitiru::softbody::detail
{

/// @brief poly6 と spiky の係数 (Müller ほか 2003)
struct FluidKernels
{
	float h = 0.0f, h2 = 0.0f;
	float poly6 = 0.0f;   ///< 315 / (64 π h^9)
	float spiky = 0.0f;   ///< -45 / (π h^6)
	float w0 = 0.0f;      ///< 距離 0 の poly6
	float wdq = 0.0f;     ///< 人工の圧力の基準 (距離 0.2 h の poly6)
};

[[nodiscard]] inline FluidKernels fluidKernels(float h) noexcept
{
	constexpr float kPi = 3.14159265f;
	FluidKernels k;
	k.h = h;
	k.h2 = h * h;
	const float h3 = h * h * h;
	k.poly6 = 315.0f / (64.0f * kPi * h3 * h3 * h3);
	k.spiky = -45.0f / (kPi * h3 * h3);
	k.w0 = k.poly6 * k.h2 * k.h2 * k.h2;
	const float x = k.h2 - 0.04f * k.h2;
	k.wdq = k.poly6 * x * x * x;
	return k;
}

[[nodiscard]] inline float fluidPoly6(const FluidKernels& k, float r2) noexcept
{
	const float x = k.h2 - r2;
	return x > 0.0f ? k.poly6 * x * x * x : 0.0f;
}

[[nodiscard]] inline F4 fluidPoly6(const FluidKernels& k, F4 r2) noexcept
{
	const F4 x = max(F4::splat(k.h2) - r2, F4::splat(0.0f));
	return F4::splat(k.poly6) * x * x * x;
}

/// @brief 行 i の欄 e から 4 つの番号と、本物の欄か (1) 埋め草か (0)
inline F4 fluidLanes(const FluidNeighbors& nb, int i, int e, int (&j)[4]) noexcept
{
	float m[4];
	for (int l = 0; l < 4; ++l)
	{
		j[l] = nb.j[e + l];
		m[l] = j[l] != i ? 1.0f : 0.0f;
	}
	return F4::set(m[0], m[1], m[2], m[3]);
}

/// @brief 床・壁の平面で粒 1 つを押し出す (平面の順に)
inline void fluidCollidePlanes(const FluidStepInput& in, float radius, Vec3& p) noexcept
{
	for (int c = 0; c < in.planeCount && c < kFluidMaxPlanes; ++c)
	{
		const FluidPlane& pl = in.planes[c];
		const float dist = (p - pl.point).dot(pl.normal) - radius;
		if (dist < 0.0f) { p -= pl.normal * dist; }
	}
}

/// @brief 床・壁の平面と、呼び手の当たりで粒を押し出す
inline void fluidCollide(const FluidStepInput& in, float radius, Vec3* p, int n) noexcept
{
	for (int i = 0; i < n; ++i) { fluidCollidePlanes(in, radius, p[i]); }
	if (in.collide != nullptr) { in.collide(in.collideUser, p, n); }
}

/// @brief 近くの粒どうしを弱く引き合わせる (Akinci ほか 2013 の表面張力を簡単にしたもの)。h / 2 で最も強い。
///        近くの粒の数で割るので、粒の多い所ほど強くはならない。位置は w.pos から読み、粒 [begin, end) の分を w.acc に書く
inline void fluidCohesion(FluidScratch& w, float h, float strength, int begin, int end) noexcept
{
	const FluidNeighbors& nb = w.pairs;
	const FluidSoa& x = w.pos;
	{
		for (int i = begin; i < end; ++i)
		{
			F4x3 sum{F4::splat(0.0f), F4::splat(0.0f), F4::splat(0.0f)};
			const F4 xi = F4::splat(x.x[i]), yi = F4::splat(x.y[i]), zi = F4::splat(x.z[i]);
			for (int e = nb.rowStart[i]; e < nb.rowStart[i + 1]; e += 4)
			{
				int j[4];
				const F4 mask = fluidLanes(nb, i, e, j);
				const F4x3 pj = fluidGather(x.x, x.y, x.z, j);
				const F4 dx = pj.x - xi, dy = pj.y - yi, dz = pj.z - zi;
				const F4 r = sqrt(dx * dx + dy * dy + dz * dz) + F4::splat(1e-9f);
				const F4 q = r / F4::splat(h);
				const F4 f = F4::splat(strength * 4.0f) * q * (F4::splat(1.0f) - q) / r * mask;
				sum = {sum.x + dx * f, sum.y + dy * f, sum.z + dz * f};
			}
			const float count = static_cast<float>(nb.rowCount[i] > 0 ? nb.rowCount[i] : 1);
			w.acc[i] = Vec3{sum.x.sum(), sum.y.sum(), sum.z.sum()} / count;
		}
	}
}

/// @brief 欄ごとの重みと勾配を測り、粒ごとの λ を出す (Macklin と Müller 2013 の式 11。押し合いだけ)。位置は w.pos から読む
inline void fluidLambda(FluidScratch& w, const FluidKernels& kk, float rho0, float relaxation, int n) noexcept
{
	const FluidNeighbors& nb = w.pairs;
	const FluidSoa& p = w.pos;
	const float inv = 1.0f / (rho0 * rho0);
	fluidForRanges(w.parallel, n, [&](int begin, int end) {
		for (int i = begin; i < end; ++i)
		{
			F4 rho = F4::splat(0.0f), g2 = F4::splat(0.0f);
			F4x3 gsum{F4::splat(0.0f), F4::splat(0.0f), F4::splat(0.0f)};
			const F4 xi = F4::splat(p.x[i]), yi = F4::splat(p.y[i]), zi = F4::splat(p.z[i]);
			for (int e = nb.rowStart[i]; e < nb.rowStart[i + 1]; e += 4)
			{
				int j[4];
				const F4 mask = fluidLanes(nb, i, e, j);
				const F4x3 pj = fluidGather(p.x, p.y, p.z, j);
				const F4 dx = xi - pj.x, dy = yi - pj.y, dz = zi - pj.z;
				const F4 r2 = dx * dx + dy * dy + dz * dz;
				const F4 r = sqrt(r2) + F4::splat(1e-9f);
				const F4 wij = fluidPoly6(kk, r2) * mask;
				const F4 hr = F4::splat(kk.h) - r;
				const F4 s = F4::splat(kk.spiky) * hr * hr / r * mask;
				const F4 gx = dx * s, gy = dy * s, gz = dz * s;
				wij.store(w.pairW + e);
				gx.store(w.pairGx + e);
				gy.store(w.pairGy + e);
				gz.store(w.pairGz + e);
				rho = rho + wij;
				gsum = {gsum.x + gx, gsum.y + gy, gsum.z + gz};
				g2 = g2 + gx * gx + gy * gy + gz * gz;
			}
			const float rhoI = kk.w0 + rho.sum();
			const Vec3 gs{gsum.x.sum(), gsum.y.sum(), gsum.z.sum()};
			const float c = std::fmax(rhoI / rho0 - 1.0f, 0.0f);
			w.lambda[i] = -c / ((gs.dot(gs) + g2.sum() + relaxation) * inv);
		}
	});
}

/// @brief λ と人工の圧力から位置の補正を出して足し、床と壁で押し出す。読むのは欄ごとの値と λ だけなので、
///        同じ仕事の中で位置 (w.p と w.pos) を書き換えてよい
inline void fluidApplyDensity(FluidScratch& w, const FluidStepInput& in, const FluidKernels& kk, float rho0, float tensileK, float radius,
                              int n) noexcept
{
	const FluidNeighbors& nb = w.pairs;
	const float invRho0 = 1.0f / rho0;
	const float invWdq = 1.0f / kk.wdq;
	fluidForRanges(w.parallel, n, [&](int begin, int end) {
		for (int i = begin; i < end; ++i)
		{
			F4x3 sum{F4::splat(0.0f), F4::splat(0.0f), F4::splat(0.0f)};
			const F4 li = F4::splat(w.lambda[i]);
			for (int e = nb.rowStart[i]; e < nb.rowStart[i + 1]; e += 4)
			{
				int j[4];
				fluidLanes(nb, i, e, j);
				const F4 s = F4::set(w.pairW[e], w.pairW[e + 1], w.pairW[e + 2], w.pairW[e + 3]) * F4::splat(invWdq);
				const F4 s2 = s * s;
				const F4 lj = F4::set(w.lambda[j[0]], w.lambda[j[1]], w.lambda[j[2]], w.lambda[j[3]]);
				const F4 c = (li + lj - F4::splat(tensileK) * s2 * s2) * F4::splat(invRho0);
				sum = {sum.x + F4::set(w.pairGx[e], w.pairGx[e + 1], w.pairGx[e + 2], w.pairGx[e + 3]) * c,
				       sum.y + F4::set(w.pairGy[e], w.pairGy[e + 1], w.pairGy[e + 2], w.pairGy[e + 3]) * c,
				       sum.z + F4::set(w.pairGz[e], w.pairGz[e + 1], w.pairGz[e + 2], w.pairGz[e + 3]) * c};
			}
			w.p[i] += Vec3{sum.x.sum(), sum.y.sum(), sum.z.sum()};
			fluidCollidePlanes(in, radius, w.p[i]);
			w.pos.x[i] = w.p[i].x;
			w.pos.y[i] = w.p[i].y;
			w.pos.z[i] = w.p[i].z;
		}
	});
	if (in.collide != nullptr)
	{
		in.collide(in.collideUser, w.p, n);
		fluidToSoa(w.p, n, w.pos);
	}
}

/// @brief XSPH の粘り。近くの粒の速さへ重みを付けて寄せ、位置を確定する。位置は w.pos、速さは w.vel から読むので、
///        同じ仕事の中で v と x を書き換えてよい
inline void fluidViscosity(FluidScratch& w, const FluidKernels& kk, float rho0, float viscosity, Vec3* v, Vec3* x, int n) noexcept
{
	const FluidNeighbors& nb = w.pairs;
	const FluidSoa& p = w.pos;
	const FluidSoa& u = w.vel;
	const float invRho0 = 1.0f / rho0;
	fluidForRanges(w.parallel, n, [&](int begin, int end) {
		for (int i = begin; i < end; ++i)
		{
			F4x3 sum{F4::splat(0.0f), F4::splat(0.0f), F4::splat(0.0f)};
			const F4 xi = F4::splat(p.x[i]), yi = F4::splat(p.y[i]), zi = F4::splat(p.z[i]);
			const F4 ui = F4::splat(u.x[i]), vi = F4::splat(u.y[i]), wi = F4::splat(u.z[i]);
			for (int e = nb.rowStart[i]; e < nb.rowStart[i + 1]; e += 4)
			{
				int j[4];
				const F4 mask = fluidLanes(nb, i, e, j);
				const F4x3 pj = fluidGather(p.x, p.y, p.z, j);
				const F4 dx = xi - pj.x, dy = yi - pj.y, dz = zi - pj.z;
				const F4 wij = fluidPoly6(kk, dx * dx + dy * dy + dz * dz) * F4::splat(invRho0) * mask;
				const F4x3 vj = fluidGather(u.x, u.y, u.z, j);
				sum = {sum.x + (vj.x - ui) * wij, sum.y + (vj.y - vi) * wij, sum.z + (vj.z - wi) * wij};
			}
			v[i] += Vec3{sum.x.sum(), sum.y.sum(), sum.z.sum()} * viscosity;
			x[i] = w.p[i];
		}
	});
}

} // namespace mitiru::softbody::detail
