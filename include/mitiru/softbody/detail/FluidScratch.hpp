#pragma once

/// @file FluidScratch.hpp
/// @brief 流れる体の 1 tick の作業領域、近くの粒の表、処理を分けて回す口 (FluidBody.hpp から読む)。
/// @details 近くの粒の表は粒 i ごとの行で、行には i 以外の番号を昇順に並べ、4 の倍数になるまで i 自身で埋める
///          (4 つずつまとめて計算するため。i 自身の所は重み 0 で足す)。並びは位置の組だけで決まり、
///          候補の表を作った時刻に依らないので、巻き戻した後も同じビットになる。
///          粒 i の合計は i の行だけから決まるので、粒をどう分けて回しても同じビットになる。
///          半径に余白 (0.2 h) を足した候補の表を覚えておき、粒が体全体の動きから余白の半分より動くまで使い回す。
///          その間は「今の距離で半径の内にある組」が候補の中に必ずあるので、候補を今の距離でふるうだけで済む。

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

#include <mitiru/softbody/detail/FluidSimd.hpp>

namespace mitiru::softbody
{

/// @brief 1 tick の器の置き方
struct FluidFrame
{
	Basis body{};          ///< 体の座標系 (傾き・横の傾き・見回しを含む)
	Basis ground{};        ///< 足元の座標系
	Vec3 bodyRoot{};
	Vec3 groundRoot{};
	float sx = 1.0f, sy = 1.0f;
	Vec3 trailDir{};       ///< 体の座標系で、伸ばす向き (単位)
	float stretch = 0.0f;
	float slosh = 0.0f;
	float taper = 0.0f;    ///< 壁で尾を細くする割合 (0 は壁でない)
	float wave = 0.0f;     ///< 壁で尾をうねらせる幅
	float halfX = 0.5f;    ///< 横の半径 (横の倍率を掛けたもの)
	std::uint8_t trailOn = 0;
	std::uint8_t pad[3]{};
	FluidShape shapes[kFluidMaxShapes]{};   ///< ワールドの座標にした、器に足す形
	int shapeCount = 0;
	Vec3 podRoot[kFluidMaxPods]{};
	Vec3 podTip[kFluidMaxPods]{};
	std::uint8_t podOn[kFluidMaxPods]{};    ///< 器に筒を足すか
};

/// @brief 近くの粒の表。粒 i の行は j[rowStart[i]] 〜 j[rowStart[i + 1] - 1]、そのうち本物は rowCount[i] 個
struct FluidNeighbors
{
	static constexpr int kCapacity = 96 * 1024;
	int count = 0;
	int dropped = 0;       ///< 入りきらずに捨てた組 (0 でないと密度が粗くなる)
	int rowStart[kFluidMaxParticles + 1]{};
	int rowCount[kFluidMaxParticles]{};
	std::uint16_t j[kCapacity]{};
};

/// @brief セルの一辺が候補の半径の格子。粒の範囲を囲む密な格子で、1 軸 16 セルを超える所は端のセルにまとめる
///        (まとめても近い組は同じか隣のセルに入るので取りこぼさない)。セルの中は粒の番号順
struct FluidGrid
{
	static constexpr int kMaxAxis = 16;
	static constexpr int kCells = kMaxAxis * kMaxAxis * kMaxAxis;
	int dim[3]{};
	int start[kCells + 1]{};
	std::uint16_t order[kFluidMaxParticles]{};
	float sx[kFluidMaxParticles]{}, sy[kFluidMaxParticles]{}, sz[kFluidMaxParticles]{};   ///< セルの順に並べた位置
	int cell[kFluidMaxParticles][3]{};
	std::uint64_t bits[kFluidMaxParticles][kFluidMaxParticles / 64]{};   ///< 粒ごとの近くの粒の印
};

/// @brief 候補の表と、それを作ったときの位置
struct FluidNeighborCache
{
	FluidGrid grid{};
	FluidNeighbors candidates{};
	Vec3 ref[kFluidMaxParticles]{};
	float px[kFluidMaxParticles]{}, py[kFluidMaxParticles]{}, pz[kFluidMaxParticles]{};   ///< ふるう位置
	std::uint16_t kept[FluidNeighbors::kCapacity]{};   ///< ふるって残った番号 (候補と同じ場所)
	int keptCount[kFluidMaxParticles]{};
	int refCount = -1;     ///< -1 は未作成
	float radius = 0.0f;
	int rebuilds = 0;
};

/// @brief 粒の位置・速さを軸ごとの配列に並べたもの (4 つずつ集めて計算するため)
struct FluidSoa
{
	float x[kFluidMaxParticles]{};
	float y[kFluidMaxParticles]{};
	float z[kFluidMaxParticles]{};
};

struct FluidScratch
{
	FluidFrame frame{};
	FluidNeighborCache cache{};
	FluidNeighbors pairs{};
	float pairW[FluidNeighbors::kCapacity]{};      ///< 表の欄ごとの poly6 の重み
	float pairGx[FluidNeighbors::kCapacity]{};     ///< 表の欄ごとの spiky の勾配 (i から見た向き)
	float pairGy[FluidNeighbors::kCapacity]{};
	float pairGz[FluidNeighbors::kCapacity]{};
	FluidSoa pos{};                                ///< 計算に使う位置 (引き合いでは x、密度では予測した位置)
	FluidSoa vel{};
	Vec3 p[kFluidMaxParticles]{};                  ///< 予測した位置
	Vec3 acc[kFluidMaxParticles]{};
	Vec3 push[kFluidMaxParticles]{};              ///< 器が押し戻す加速度
	Vec3 podGoal[kFluidMaxParticles]{};
	float lambda[kFluidMaxParticles]{};
	const FluidParallel* parallel = nullptr;
};

namespace detail
{

/// @brief [0, count) を par の仕事の数に等分して fn(begin, end) を回す。par が無いか count が minCount より少なければその場で 1 回
template <class Fn>
inline void fluidForRanges(const FluidParallel* par, int count, Fn&& fn, int minCount = 64)
{
	if (par == nullptr || par->run == nullptr || par->jobCount <= 1 || count < minCount)
	{
		fn(0, count);
		return;
	}
	struct Ctx
	{
		Fn* fn;
		int count;
		int jobs;
	} ctx{&fn, count, par->jobCount};
	par->run(par->user, par->jobCount, &ctx, [](void* p, int index) {
		const Ctx& c = *static_cast<const Ctx*>(p);
		(*c.fn)(c.count * index / c.jobs, c.count * (index + 1) / c.jobs);
	});
}

/// @brief 粒を格子のセルへ数え分け、セルの順に位置を並べる
inline void fluidBuildGrid(FluidGrid& g, const Vec3* pos, int n, float cellSize) noexcept
{
	const float inv = 1.0f / cellSize;
	Vec3 lo = pos[0];
	for (int i = 1; i < n; ++i) { lo = {std::fmin(lo.x, pos[i].x), std::fmin(lo.y, pos[i].y), std::fmin(lo.z, pos[i].z)}; }
	int hi[3] = {0, 0, 0};
	for (int i = 0; i < n; ++i)
	{
		const float rel[3] = {(pos[i].x - lo.x) * inv, (pos[i].y - lo.y) * inv, (pos[i].z - lo.z) * inv};
		for (int a = 0; a < 3; ++a)
		{
			g.cell[i][a] = std::min(static_cast<int>(rel[a]), FluidGrid::kMaxAxis - 1);
			hi[a] = std::max(hi[a], g.cell[i][a]);
		}
	}
	for (int a = 0; a < 3; ++a) { g.dim[a] = hi[a] + 1; }
	const int cells = g.dim[0] * g.dim[1] * g.dim[2];
	std::fill(g.start, g.start + cells + 1, 0);
	const auto index = [&g](int i) { return (g.cell[i][2] * g.dim[1] + g.cell[i][1]) * g.dim[0] + g.cell[i][0]; };
	for (int i = 0; i < n; ++i) { ++g.start[index(i) + 1]; }
	for (int c = 0; c < cells; ++c) { g.start[c + 1] += g.start[c]; }
	int fill[FluidGrid::kCells];
	std::copy(g.start, g.start + cells, fill);
	for (int i = 0; i < n; ++i)
	{
		const int k = fill[index(i)]++;
		g.order[k] = static_cast<std::uint16_t>(i);
		g.sx[k] = pos[i].x;
		g.sy[k] = pos[i].y;
		g.sz[k] = pos[i].z;
	}
}

/// @brief 粒 i の行の印 (i 以外で半径 r の内の粒)。27 セルを x の並びの 9 本として見る。印は i の行にだけ付ける
inline int fluidMarkRow(FluidGrid& g, const Vec3* pos, int i, int words, float r2) noexcept
{
	std::fill(g.bits[i], g.bits[i] + words, std::uint64_t{0});
	const int cx = g.cell[i][0], cy = g.cell[i][1], cz = g.cell[i][2];
	const int x0 = std::max(cx - 1, 0), x1 = std::min(cx + 1, g.dim[0] - 1);
	const Vec3 p = pos[i];
	for (int z = std::max(cz - 1, 0); z <= std::min(cz + 1, g.dim[2] - 1); ++z)
	{
		for (int y = std::max(cy - 1, 0); y <= std::min(cy + 1, g.dim[1] - 1); ++y)
		{
			const int row = (z * g.dim[1] + y) * g.dim[0];
			for (int k = g.start[row + x0]; k < g.start[row + x1 + 1]; ++k)
			{
				const int j = g.order[k];
				const float dx = p.x - g.sx[k], dy = p.y - g.sy[k], dz = p.z - g.sz[k];
				if (j != i && dx * dx + dy * dy + dz * dz < r2) { g.bits[i][j >> 6] |= std::uint64_t{1} << (j & 63); }
			}
		}
	}
	int count = 0;
	for (int wd = 0; wd < words; ++wd) { count += std::popcount(g.bits[i][wd]); }
	return count;
}

/// @brief 半径 r の内にある組で表を作る (詰めて並べる)。行の印を粒ごとに分けて付け、数の累積で行の場所を決めてから書く
inline void fluidBuildNeighbors(FluidGrid& g, const Vec3* pos, int n, float r, FluidNeighbors& out, const FluidParallel* par = nullptr) noexcept
{
	fluidBuildGrid(g, pos, n, r);
	const int words = (n + 63) / 64;
	fluidForRanges(par, n, [&](int begin, int end) {
		for (int i = begin; i < end; ++i) { out.rowCount[i] = fluidMarkRow(g, pos, i, words, r * r); }
	});
	out.count = 0;
	out.dropped = 0;
	for (int i = 0; i < n; ++i)
	{
		out.rowStart[i] = out.count;
		const int keep = std::min(out.rowCount[i], FluidNeighbors::kCapacity - out.count);
		out.dropped += out.rowCount[i] - keep;
		out.rowCount[i] = keep;
		out.count += keep;
	}
	out.rowStart[n] = out.count;
	fluidForRanges(par, n, [&](int begin, int end) {
		for (int i = begin; i < end; ++i)
		{
			int k = out.rowStart[i];
			for (int wd = 0; wd < words && k < out.rowStart[i] + out.rowCount[i]; ++wd)
			{
				for (std::uint64_t b = g.bits[i][wd]; b != 0 && k < out.rowStart[i] + out.rowCount[i]; b &= b - 1)
				{
					out.j[k++] = static_cast<std::uint16_t>(wd * 64 + std::countr_zero(b));
				}
			}
		}
	});
}

/// @brief 候補を作ったときから、体全体の動きを除いて余白の半分より動いた粒があるか
[[nodiscard]] inline bool fluidCacheStale(const FluidNeighborCache& c, const Vec3* pos, int n, float skin) noexcept
{
	if (c.refCount != n) { return true; }
	Vec3 shift{};
	for (int i = 0; i < n; ++i) { shift += pos[i] - c.ref[i]; }
	shift = shift / static_cast<float>(n);
	const float limit = 0.45f * skin;
	const float limit2 = limit * limit;
	for (int i = 0; i < n; ++i)
	{
		const Vec3 d = pos[i] - c.ref[i] - shift;
		if (d.dot(d) > limit2) { return true; }
	}
	return false;
}

/// @brief 候補の行 i を今の位置でふるい、残った番号を候補と同じ場所 (kept) に詰めて書く。残った数を返す
inline int fluidFilterRow(const FluidNeighborCache& c, const float* px, const float* py, const float* pz, int i, float h2,
                          std::uint16_t* kept) noexcept
{
	const F4 xi = F4::splat(px[i]), yi = F4::splat(py[i]), zi = F4::splat(pz[i]);
	const int begin = c.candidates.rowStart[i];
	const int end = begin + c.candidates.rowCount[i];
	int count = 0;
	for (int k = begin; k < end; k += 4)
	{
		int j[4];
		for (int l = 0; l < 4; ++l) { j[l] = k + l < end ? c.candidates.j[k + l] : i; }
		const F4x3 pj = fluidGather(px, py, pz, j);
		const F4 dx = xi - pj.x, dy = yi - pj.y, dz = zi - pj.z;
		float r2[4];
		(dx * dx + dy * dy + dz * dz).store(r2);
		for (int l = 0; l < 4; ++l)
		{
			if (j[l] != i && r2[l] < h2) { kept[begin + count++] = static_cast<std::uint16_t>(j[l]); }
		}
	}
	return count;
}

/// @brief pos で半径 h の内にある組の表を out に作る (行は 4 の倍数に埋める)。候補が古ければ作り直す。
///        行ごとのふるいは par で分けて回し、詰める所だけを 1 本で回す
inline void fluidFindNeighbors(FluidNeighborCache& c, const Vec3* pos, int n, float h, FluidNeighbors& out,
                               const FluidParallel* par = nullptr) noexcept
{
	const float skin = 0.2f * h;
	if (c.radius != h + skin || fluidCacheStale(c, pos, n, skin))
	{
		c.radius = h + skin;
		fluidBuildNeighbors(c.grid, pos, n, c.radius, c.candidates, par);
		std::copy(pos, pos + n, c.ref);
		c.refCount = n;
		++c.rebuilds;
	}
	const float h2 = h * h;
	for (int i = 0; i < n; ++i)
	{
		c.px[i] = pos[i].x;
		c.py[i] = pos[i].y;
		c.pz[i] = pos[i].z;
	}
	fluidForRanges(par, n, [&](int begin, int end) {
		for (int i = begin; i < end; ++i) { c.keptCount[i] = fluidFilterRow(c, c.px, c.py, c.pz, i, h2, c.kept); }
	});
	out.count = 0;
	out.dropped = c.candidates.dropped;
	for (int i = 0; i < n; ++i)
	{
		out.rowStart[i] = out.count;
		const int count = std::min(c.keptCount[i], FluidNeighbors::kCapacity - 4 - out.count);
		std::copy(c.kept + c.candidates.rowStart[i], c.kept + c.candidates.rowStart[i] + count, out.j + out.count);
		out.count += count;
		out.rowCount[i] = count;
		while (((out.count - out.rowStart[i]) & 3) != 0) { out.j[out.count++] = static_cast<std::uint16_t>(i); }
	}
	out.rowStart[n] = out.count;
}

inline void fluidToSoa(const Vec3* src, int n, FluidSoa& out) noexcept
{
	for (int i = 0; i < n; ++i)
	{
		out.x[i] = src[i].x;
		out.y[i] = src[i].y;
		out.z[i] = src[i].z;
	}
}

} // namespace detail

} // namespace mitiru::softbody
