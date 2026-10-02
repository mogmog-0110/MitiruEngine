#pragma once

/// @file NavMeshBake.hpp
/// @brief レベルの三角形からナビメッシュ blob (.navmesh) を焼く (Recast)。mitiru_nav_bake を link した target 用
///
/// 焼くのは tool (mitiru_navbake) とテストの仕事で、ゲーム DLL は焼いた blob を NavMesh.hpp で読むだけ。
/// 世界をタイルに切って 1 枚ずつ焼くので、大きなレベルでもタイル 1 枚の頂点上限 (65535) に当たりにくい。
/// 同じ三角形・同じ設定からは同じバイト列が出る (単一スレッドで、乱数も時刻も使わない)。

#include <limits>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <Recast.h>

#include "sgc/math/Vec3.hpp"
#include "mitiru/nav/NavMeshBlob.hpp"

namespace mitiru::nav
{

/// @brief 寸法はすべてワールド単位 (m)。cell は細かいほど正確で焼くのが遅い
struct NavBakeSettings
{
	float cellSize = 0.2f;
	float cellHeight = 0.1f;
	float agentHeight = 1.8f;
	float agentRadius = 0.4f;
	float agentMaxClimb = 0.4f;
	float agentMaxSlopeDeg = 45.0f;
	float regionMinSize = 8.0f;        ///< これより小さい孤立領域は捨てる (cell 数の平方根)
	float regionMergeSize = 20.0f;
	float edgeMaxLen = 12.0f;
	float edgeMaxError = 1.3f;
	float detailSampleDist = 6.0f;     ///< cellSize の倍数。0 で高さの細部を取らない
	float detailSampleMaxError = 1.0f; ///< cellHeight の倍数
	int tileSizeCells = 64;
};

struct NavBakeResult
{
	std::vector<std::uint8_t> blob;   ///< 空なら失敗 (error に理由)
	std::string error;
	int tileCount = 0;
	int polyCount = 0;
};

namespace detail
{

struct RcDeleter
{
	void operator()(rcHeightfield* p) const noexcept { rcFreeHeightField(p); }
	void operator()(rcCompactHeightfield* p) const noexcept { rcFreeCompactHeightfield(p); }
	void operator()(rcContourSet* p) const noexcept { rcFreeContourSet(p); }
	void operator()(rcPolyMesh* p) const noexcept { rcFreePolyMesh(p); }
	void operator()(rcPolyMeshDetail* p) const noexcept { rcFreePolyMeshDetail(p); }
};
template <class T> using RcPtr = std::unique_ptr<T, RcDeleter>;

/// @brief Recast の頂点・添字の形 (float[3] の列 / int[3] の列) に揃えた入力
struct BakeInput
{
	std::vector<float> verts;
	std::vector<int> tris;
	float bmin[3] = {};
	float bmax[3] = {};
};

/// @brief タイル 1 枚分の作業。中間生成物はタイルごとに捨てる
struct TileJob
{
	rcConfig cfg{};
	int tileX = 0;
	int tileY = 0;
};

struct TileMesh
{
	RcPtr<rcPolyMesh> poly;
	RcPtr<rcPolyMeshDetail> detail;
};

[[nodiscard]] inline BakeInput makeBakeInput(std::span<const sgc::Vec3f> vertices, std::span<const std::uint32_t> indices)
{
	BakeInput in;
	in.verts.reserve(vertices.size() * 3);
	for (const auto& v : vertices)
	{
		in.verts.push_back(v.x);
		in.verts.push_back(v.y);
		in.verts.push_back(v.z);
	}
	in.tris.reserve(indices.size() / 3 * 3);
	for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
	{
		for (std::size_t k = 0; k < 3; ++k) in.tris.push_back(static_cast<int>(indices[i + k]));
	}
	rcCalcBounds(in.verts.data(), static_cast<int>(vertices.size()), in.bmin, in.bmax);
	return in;
}

/// @brief 全タイル共通の rcConfig (寸法を voxel 単位へ直す)。タイル固有の範囲は tileConfig で入れる
[[nodiscard]] inline rcConfig baseConfig(const NavBakeSettings& s)
{
	rcConfig cfg{};
	cfg.cs = s.cellSize;
	cfg.ch = s.cellHeight;
	cfg.walkableSlopeAngle = s.agentMaxSlopeDeg;
	cfg.walkableHeight = static_cast<int>(std::ceil(s.agentHeight / s.cellHeight));
	cfg.walkableClimb = static_cast<int>(std::floor(s.agentMaxClimb / s.cellHeight));
	cfg.walkableRadius = static_cast<int>(std::ceil(s.agentRadius / s.cellSize));
	cfg.maxEdgeLen = static_cast<int>(s.edgeMaxLen / s.cellSize);
	cfg.maxSimplificationError = s.edgeMaxError;
	cfg.minRegionArea = static_cast<int>(s.regionMinSize * s.regionMinSize);
	cfg.mergeRegionArea = static_cast<int>(s.regionMergeSize * s.regionMergeSize);
	cfg.maxVertsPerPoly = DT_VERTS_PER_POLYGON;
	cfg.tileSize = s.tileSizeCells;
	cfg.borderSize = cfg.walkableRadius + 3;   // 隣のタイルと縁が揃うよう、半径ぶん外まで焼いて切り落とす
	cfg.width = cfg.tileSize + cfg.borderSize * 2;
	cfg.height = cfg.tileSize + cfg.borderSize * 2;
	cfg.detailSampleDist = s.detailSampleDist < 0.9f ? 0.0f : s.cellSize * s.detailSampleDist;
	cfg.detailSampleMaxError = s.cellHeight * s.detailSampleMaxError;
	return cfg;
}

[[nodiscard]] inline TileJob tileConfig(const rcConfig& base, const BakeInput& in, int tx, int ty)
{
	TileJob job{base, tx, ty};
	const float tcs = static_cast<float>(base.tileSize) * base.cs;
	const float border = static_cast<float>(base.borderSize) * base.cs;
	rcVcopy(job.cfg.bmin, in.bmin);
	rcVcopy(job.cfg.bmax, in.bmax);
	job.cfg.bmin[0] = in.bmin[0] + static_cast<float>(tx) * tcs - border;
	job.cfg.bmin[2] = in.bmin[2] + static_cast<float>(ty) * tcs - border;
	job.cfg.bmax[0] = in.bmin[0] + static_cast<float>(tx + 1) * tcs + border;
	job.cfg.bmax[2] = in.bmin[2] + static_cast<float>(ty + 1) * tcs + border;
	return job;
}

/// @brief タイルの XZ 範囲に掛かる三角形だけを歩ける/歩けないで塗って積む
[[nodiscard]] inline bool rasterizeTile(rcContext& ctx, const BakeInput& in, const rcConfig& cfg, rcHeightfield& solid)
{
	const int triCount = static_cast<int>(in.tris.size() / 3);
	const int vertCount = static_cast<int>(in.verts.size() / 3);
	std::vector<int> tris;
	tris.reserve(in.tris.size());
	for (int t = 0; t < triCount; ++t)
	{
		float lo[2] = {1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
		for (int k = 0; k < 3; ++k)
		{
			const float* v = &in.verts[static_cast<std::size_t>(in.tris[static_cast<std::size_t>(t * 3 + k)]) * 3];
			lo[0] = std::min(lo[0], v[0]); hi[0] = std::max(hi[0], v[0]);
			lo[1] = std::min(lo[1], v[2]); hi[1] = std::max(hi[1], v[2]);
		}
		if (hi[0] < cfg.bmin[0] || lo[0] > cfg.bmax[0] || hi[1] < cfg.bmin[2] || lo[1] > cfg.bmax[2]) continue;
		tris.insert(tris.end(), in.tris.begin() + t * 3, in.tris.begin() + t * 3 + 3);
	}
	const int n = static_cast<int>(tris.size() / 3);
	std::vector<unsigned char> areas(static_cast<std::size_t>(n), 0);
	rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, in.verts.data(), vertCount, tris.data(), n, areas.data());
	return rcRasterizeTriangles(&ctx, in.verts.data(), vertCount, tris.data(), areas.data(), n, solid, cfg.walkableClimb);
}

[[nodiscard]] inline RcPtr<rcCompactHeightfield> buildCompact(rcContext& ctx, const BakeInput& in, const rcConfig& cfg)
{
	RcPtr<rcHeightfield> solid(rcAllocHeightfield());
	if (!solid || !rcCreateHeightfield(&ctx, *solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch)) return nullptr;
	if (!rasterizeTile(ctx, in, cfg, *solid)) return nullptr;
	rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *solid);
	rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid);
	rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *solid);

	RcPtr<rcCompactHeightfield> chf(rcAllocCompactHeightfield());
	if (!chf || !rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid, *chf)) return nullptr;
	if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf)) return nullptr;
	if (!rcBuildDistanceField(&ctx, *chf)) return nullptr;
	if (!rcBuildRegions(&ctx, *chf, cfg.borderSize, cfg.minRegionArea, cfg.mergeRegionArea)) return nullptr;
	return chf;
}

/// @return poly が null なら失敗、poly->npolys == 0 なら歩ける所の無い空タイル
[[nodiscard]] inline TileMesh buildTileMesh(rcContext& ctx, const BakeInput& in, const rcConfig& cfg)
{
	TileMesh out;
	const auto chf = buildCompact(ctx, in, cfg);
	if (!chf) return out;
	RcPtr<rcContourSet> cset(rcAllocContourSet());
	if (!cset || !rcBuildContours(&ctx, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset)) return out;
	RcPtr<rcPolyMesh> poly(rcAllocPolyMesh());
	if (!poly) return out;
	if (cset->nconts == 0) { out.poly = std::move(poly); return out; }
	if (!rcBuildPolyMesh(&ctx, *cset, cfg.maxVertsPerPoly, *poly)) return out;
	RcPtr<rcPolyMeshDetail> dmesh(rcAllocPolyMeshDetail());
	if (!dmesh || !rcBuildPolyMeshDetail(&ctx, *poly, *chf, cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh)) return out;
	out.poly = std::move(poly);
	out.detail = std::move(dmesh);
	return out;
}

/// @brief Recast の領域を「歩ける (flag 1)」1 種類にまとめる。area 0 / flag 1 が NavMesh.hpp の既定フィルタの前提
inline void markWalkable(rcPolyMesh& pm)
{
	for (int i = 0; i < pm.npolys; ++i)
	{
		const bool walkable = pm.areas[i] == RC_WALKABLE_AREA;
		pm.areas[i] = 0;
		pm.flags[i] = walkable ? 1 : 0;
	}
}

[[nodiscard]] inline dtNavMeshCreateParams createParams(const TileMesh& tm, const TileJob& job, const NavBakeSettings& s)
{
	const rcPolyMesh& pm = *tm.poly;
	const rcPolyMeshDetail& dm = *tm.detail;
	dtNavMeshCreateParams p{};
	p.verts = pm.verts; p.vertCount = pm.nverts;
	p.polys = pm.polys; p.polyAreas = pm.areas; p.polyFlags = pm.flags; p.polyCount = pm.npolys; p.nvp = pm.nvp;
	p.detailMeshes = dm.meshes; p.detailVerts = dm.verts; p.detailVertsCount = dm.nverts;
	p.detailTris = dm.tris; p.detailTriCount = dm.ntris;
	p.walkableHeight = s.agentHeight; p.walkableRadius = s.agentRadius; p.walkableClimb = s.agentMaxClimb;
	p.tileX = job.tileX; p.tileY = job.tileY; p.tileLayer = 0;
	rcVcopy(p.bmin, pm.bmin);
	rcVcopy(p.bmax, pm.bmax);
	p.cs = job.cfg.cs; p.ch = job.cfg.ch;
	p.buildBvTree = true;
	return p;
}

inline void appendBytes(std::vector<std::uint8_t>& out, const void* src, std::size_t size)
{
	const auto* b = static_cast<const std::uint8_t*>(src);
	out.insert(out.end(), b, b + size);
}

inline void appendTile(std::vector<std::uint8_t>& out, const unsigned char* data, int size)
{
	const NavBlobTile tile{static_cast<std::uint32_t>(size)};
	appendBytes(out, &tile, sizeof(tile));
	appendBytes(out, data, static_cast<std::size_t>(size));
	out.resize(out.size() + (navBlobPadded(tile.dataSize) - tile.dataSize), 0);
}

[[nodiscard]] inline int ceilLog2(unsigned v) noexcept
{
	int r = 0;
	while ((1u << r) < v) ++r;
	return r;
}

/// @brief タイル 1 枚を焼いて blob の末尾に足す。空タイルは何も足さず true
[[nodiscard]] inline bool bakeTile(rcContext& ctx, const BakeInput& in, const TileJob& job, const NavBakeSettings& s,
	NavBakeResult& result)
{
	TileMesh tm = buildTileMesh(ctx, in, job.cfg);
	if (!tm.poly) { result.error = "Recast がタイルを焼けなかった"; return false; }
	if (tm.poly->npolys == 0) return true;
	if (tm.poly->nverts >= 0xffff) { result.error = "タイル 1 枚の頂点が 65535 を超えた (tileSizeCells を小さく)"; return false; }
	markWalkable(*tm.poly);
	dtNavMeshCreateParams params = createParams(tm, job, s);
	unsigned char* data = nullptr;
	int size = 0;
	if (!dtCreateNavMeshData(&params, &data, &size))
	{
		result.error = "Detour のタイルデータを作れなかった";
		return false;
	}
	appendTile(result.blob, data, size);
	dtFree(data);
	++result.tileCount;
	result.polyCount += tm.poly->npolys;
	return true;
}

[[nodiscard]] inline const char* validate(const NavBakeSettings& s, std::span<const sgc::Vec3f> vertices,
	std::span<const std::uint32_t> indices)
{
	if (indices.size() / 3 == 0) return "三角形が無い";
	if (vertices.empty() || vertices.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
		return "頂点の数が不正";
	for (const std::uint32_t i : indices)
	{
		if (i >= vertices.size()) return "頂点の範囲外を指す添字がある";
	}
	for (const sgc::Vec3f& v : vertices)
	{
		if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return "有限でない頂点座標がある";
	}
	const float lengths[] = {s.cellSize, s.cellHeight, s.agentHeight, s.agentRadius, s.agentMaxClimb,
		s.regionMinSize, s.regionMergeSize, s.edgeMaxLen, s.edgeMaxError, s.detailSampleDist, s.detailSampleMaxError};
	for (const float f : lengths)
	{
		if (!std::isfinite(f) || f < 0.0f) return "設定に負の数か有限でない値がある";
	}
	if (!(s.cellSize > 0.0f) || !(s.cellHeight > 0.0f)) return "cellSize / cellHeight は正の数";
	if (s.tileSizeCells < 8) return "tileSizeCells は 8 以上";
	if (!(s.agentHeight > 0.0f) || s.agentRadius < 0.0f || s.agentMaxClimb < 0.0f) return "agent の寸法が不正";
	if (!(s.agentMaxSlopeDeg >= 0.0f && s.agentMaxSlopeDeg < 90.0f)) return "agentMaxSlopeDeg は 0 以上 90 未満";
	return nullptr;
}

} // namespace detail

/// @brief 三角形 (添字 3 つで 1 枚) からナビメッシュ blob を焼く
[[nodiscard]] inline NavBakeResult bakeNavMesh(std::span<const sgc::Vec3f> vertices,
	std::span<const std::uint32_t> indices, const NavBakeSettings& settings = {})
{
	NavBakeResult result;
	if (const char* why = detail::validate(settings, vertices, indices)) { result.error = why; return result; }
	const detail::BakeInput in = detail::makeBakeInput(vertices, indices);
	const rcConfig base = detail::baseConfig(settings);
	int gw = 0, gh = 0;
	rcCalcGridSize(in.bmin, in.bmax, base.cs, &gw, &gh);
	const int tw = (gw + base.tileSize - 1) / base.tileSize;
	const int th = (gh + base.tileSize - 1) / base.tileSize;
	// 範囲や cellSize が極端だと int の掛け算が溢れるので、64 bit で数えてから絞る
	const long long tiles = static_cast<long long>(tw) * static_cast<long long>(th);
	if (tw <= 0 || th <= 0) { result.error = "三角形の広がりが cellSize に満たない"; return result; }
	if (tiles > (1 << 14)) { result.error = "タイル数が多すぎる (tileSizeCells を大きく)"; return result; }
	const int tileBits = std::min(detail::ceilLog2(static_cast<unsigned>(tiles)), 14);

	NavBlobHeader header;
	header.maxTiles = 1u << tileBits;
	header.maxPolysPerTile = 1u << (22 - tileBits);
	rcVcopy(header.origin, in.bmin);
	header.tileWidth = header.tileHeight = static_cast<float>(base.tileSize) * base.cs;
	header.agentRadius = settings.agentRadius;
	header.agentHeight = settings.agentHeight;
	header.agentMaxClimb = settings.agentMaxClimb;
	result.blob.resize(sizeof(NavBlobHeader));

	rcContext ctx(false);
	for (int y = 0; y < th; ++y)
	{
		for (int x = 0; x < tw; ++x)
		{
			if (!detail::bakeTile(ctx, in, detail::tileConfig(base, in, x, y), settings, result)) { result.blob.clear(); return result; }
		}
	}
	if (result.tileCount == 0) { result.blob.clear(); result.error = "歩ける面が無い (agent の寸法と傾斜の上限を確かめる)"; return result; }
	header.tileCount = static_cast<std::uint32_t>(result.tileCount);
	std::memcpy(result.blob.data(), &header, sizeof(header));
	return result;
}

} // namespace mitiru::nav
