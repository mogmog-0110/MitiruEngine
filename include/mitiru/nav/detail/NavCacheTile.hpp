#pragma once

/// @file NavCacheTile.hpp
/// @brief .navcache の 1 層に障害物を塗り、Detour のタイルデータを作る。
/// dtTileCache::buildNavMeshTile と同じ手順だが、できたタイルは dtNavMesh に追加せず bytes で返す。
/// 追加順と番号は DynamicNavMesh が決める。dtTileCache では追加順で番号が変わり、巻き戻しても同じ形に戻せない。

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include <DetourNavMeshBuilder.h>
#include <DetourTileCacheBuilder.h>

#include "mitiru/nav/NavCacheBlob.hpp"
#include "mitiru/nav/NavObstacle.hpp"
#include "mitiru/nav/detail/NavCacheCodec.hpp"

namespace mitiru::nav::detail
{

struct NavBounds
{
	float lo[3] = {0.0f, 0.0f, 0.0f};
	float hi[3] = {0.0f, 0.0f, 0.0f};

	[[nodiscard]] bool overlaps(const NavBounds& o) const noexcept
	{
		for (int i = 0; i < 3; ++i)
		{
			if (hi[i] < o.lo[i] || lo[i] > o.hi[i]) return false;
		}
		return true;
	}
};

/// @brief 回転した箱を外接円として扱い、拡張後の範囲を求める。
[[nodiscard]] inline NavBounds obstacleBounds(const NavObstacle& o, float inflate) noexcept
{
	const sgc::Vec3f& h = o.halfExtents;
	float rx = h.x, rz = h.z;
	if (o.shape == NavObstacleShape::Box && o.yaw != 0.0f) rx = rz = std::sqrt(h.x * h.x + h.z * h.z);
	NavBounds b;
	b.lo[0] = o.center.x - rx - inflate; b.hi[0] = o.center.x + rx + inflate;
	b.lo[1] = o.center.y - h.y;          b.hi[1] = o.center.y + h.y;
	b.lo[2] = o.center.z - rz - inflate; b.hi[2] = o.center.z + rz + inflate;
	return b;
}

/// @brief dtTileCache::addBoxObstacle と同じ方法で、回転した箱を塗るための補助値を求める。
inline void boxRotAux(float yaw, float out[2]) noexcept
{
	const float coshalf = std::cos(0.5f * yaw);
	const float sinhalf = std::sin(-0.5f * yaw);
	out[0] = coshalf * sinhalf;
	out[1] = coshalf * coshalf - 0.5f;
}

inline void markObstacle(dtTileCacheLayer& layer, const NavCacheHeader& h, const NavObstacle& o)
{
	const float r = h.agentRadius;
	const float* orig = layer.header->bmin;
	if (o.shape == NavObstacleShape::Cylinder)
	{
		const float base[3] = {o.center.x, o.center.y - o.halfExtents.y, o.center.z};
		dtMarkCylinderArea(layer, orig, h.cellSize, h.cellHeight, base, o.halfExtents.x + r, 2.0f * o.halfExtents.y, 0);
		return;
	}
	if (o.yaw == 0.0f)
	{
		const NavBounds b = obstacleBounds(o, r);
		dtMarkBoxArea(layer, orig, h.cellSize, h.cellHeight, b.lo, b.hi, 0);
		return;
	}
	const float c[3] = {o.center.x, o.center.y, o.center.z};
	const float e[3] = {o.halfExtents.x + r, o.halfExtents.y, o.halfExtents.z + r};
	float aux[2];
	boxRotAux(o.yaw, aux);
	dtMarkBoxArea(layer, orig, h.cellSize, h.cellHeight, c, e, aux, 0);
}

/// @brief 層と中間生成物を tile cache のアロケータで持ち、途中で失敗しても解放する。
struct NavCacheScratch
{
	dtTileCacheAlloc alloc;
	dtTileCacheLayer* layer = nullptr;
	dtTileCacheContourSet* contours = nullptr;
	dtTileCachePolyMesh* mesh = nullptr;

	NavCacheScratch() = default;
	NavCacheScratch(const NavCacheScratch&) = delete;
	NavCacheScratch& operator=(const NavCacheScratch&) = delete;
	~NavCacheScratch()
	{
		dtFreeTileCachePolyMesh(&alloc, mesh);
		dtFreeTileCacheContourSet(&alloc, contours);
		dtFreeTileCacheLayer(&alloc, layer);
	}
};

/// @brief 焼いた床の area 63 を、NavQuery の既定フィルタに合わせて flag 1、area 0 にする。
inline void markWalkableFlags(dtTileCachePolyMesh& mesh) noexcept
{
	for (int i = 0; i < mesh.npolys; ++i)
	{
		mesh.flags[i] = mesh.areas[i] == DT_TILECACHE_WALKABLE_AREA ? 1 : 0;
		mesh.areas[i] = 0;
	}
}

[[nodiscard]] inline dtNavMeshCreateParams tileParams(const dtTileCachePolyMesh& m, const dtTileCacheLayerHeader& lh,
	const NavCacheHeader& h)
{
	dtNavMeshCreateParams p{};
	p.verts = m.verts; p.vertCount = m.nverts;
	p.polys = m.polys; p.polyAreas = m.areas; p.polyFlags = m.flags; p.polyCount = m.npolys;
	p.nvp = DT_VERTS_PER_POLYGON;
	p.walkableHeight = h.agentHeight; p.walkableRadius = h.agentRadius; p.walkableClimb = h.agentMaxClimb;
	p.tileX = lh.tx; p.tileY = lh.ty; p.tileLayer = lh.tlayer;
	p.cs = h.cellSize; p.ch = h.cellHeight;
	p.buildBvTree = false;
	std::memcpy(p.bmin, lh.bmin, sizeof(p.bmin));
	std::memcpy(p.bmax, lh.bmax, sizeof(p.bmax));
	return p;
}

/// @brief layer にかかる有効な障害物を塗り、Detour のタイルデータを out に書く。歩ける場所がなければ out は空になる。
/// @return 作れなければ false。
[[nodiscard]] inline bool buildCacheTile(std::span<std::uint8_t> layerBytes, const NavCacheHeader& h,
	std::span<const NavObstacle> obstacles, std::vector<std::uint8_t>& out)
{
	out.clear();
	NavCacheScratch s;
	NavCacheCopyCodec codec;
	if (dtStatusFailed(dtDecompressTileCacheLayer(&s.alloc, &codec, layerBytes.data(), static_cast<int>(layerBytes.size()), &s.layer))) return false;
	NavBounds tile;
	std::memcpy(tile.lo, s.layer->header->bmin, sizeof(tile.lo));
	std::memcpy(tile.hi, s.layer->header->bmax, sizeof(tile.hi));
	for (const NavObstacle& o : obstacles)
	{
		if (o.active() && obstacleBounds(o, h.agentRadius).overlaps(tile)) markObstacle(*s.layer, h, o);
	}
	const int climb = static_cast<int>(h.agentMaxClimb / h.cellHeight);
	if (dtStatusFailed(dtBuildTileCacheRegions(&s.alloc, *s.layer, climb))) return false;
	s.contours = dtAllocTileCacheContourSet(&s.alloc);
	if (s.contours == nullptr || dtStatusFailed(dtBuildTileCacheContours(&s.alloc, *s.layer, climb, h.edgeMaxError, *s.contours))) return false;
	s.mesh = dtAllocTileCachePolyMesh(&s.alloc);
	if (s.mesh == nullptr || dtStatusFailed(dtBuildTileCachePolyMesh(&s.alloc, *s.contours, *s.mesh))) return false;
	if (s.mesh->npolys == 0) return true;
	markWalkableFlags(*s.mesh);
	dtNavMeshCreateParams params = tileParams(*s.mesh, *s.layer->header, h);
	unsigned char* data = nullptr;
	int size = 0;
	if (!dtCreateNavMeshData(&params, &data, &size)) return false;
	out.assign(data, data + size);
	dtFree(data);
	return true;
}

} // namespace mitiru::nav::detail
