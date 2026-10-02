#pragma once

/// @file NavCacheBake.hpp
/// @brief レベルの三角形から、障害物に応じて作り直せるナビメッシュの元データ (.navcache) を焼く (Recast +
///        DetourTileCache)。mitiru_nav_bake をリンクしたターゲットで使う
///
/// .navmesh (NavMeshBake.hpp) と同じ分け方でタイルにし、歩ける高さの格子を層として持つ。
/// 床を agent の半径だけ縮めて層にする。実行時は DynamicNavMesh.hpp が障害物を層に反映してからポリゴンにする。
/// 同じ三角形と設定からは同じバイト列ができる。

#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <DetourTileCacheBuilder.h>
#include <Recast.h>

#include "mitiru/nav/NavCacheBlob.hpp"
#include "mitiru/nav/NavMeshBake.hpp"
#include "mitiru/nav/detail/NavCacheCodec.hpp"

namespace mitiru::nav
{

namespace detail
{

struct RcLayerDeleter
{
	void operator()(rcHeightfieldLayerSet* p) const noexcept { rcFreeHeightfieldLayerSet(p); }
};

/// @brief タイル 1 枚の床を agent の半径だけ縮めた格子にし、高さごとの層に分ける
[[nodiscard]] inline std::unique_ptr<rcHeightfieldLayerSet, RcLayerDeleter> buildTileLayers(rcContext& ctx,
	const BakeInput& in, const rcConfig& cfg)
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
	std::unique_ptr<rcHeightfieldLayerSet, RcLayerDeleter> lset(rcAllocHeightfieldLayerSet());
	if (!lset || !rcBuildHeightfieldLayers(&ctx, *chf, cfg.borderSize, cfg.walkableHeight, *lset)) return nullptr;
	return lset;
}

[[nodiscard]] inline dtTileCacheLayerHeader layerHeader(const rcHeightfieldLayer& layer, int tx, int ty, int index)
{
	dtTileCacheLayerHeader h{};
	h.magic = DT_TILECACHE_MAGIC;
	h.version = DT_TILECACHE_VERSION;
	h.tx = tx;
	h.ty = ty;
	h.tlayer = index;
	rcVcopy(h.bmin, layer.bmin);
	rcVcopy(h.bmax, layer.bmax);
	h.width = static_cast<unsigned char>(layer.width);
	h.height = static_cast<unsigned char>(layer.height);
	h.minx = static_cast<unsigned char>(layer.minx);
	h.maxx = static_cast<unsigned char>(layer.maxx);
	h.miny = static_cast<unsigned char>(layer.miny);
	h.maxy = static_cast<unsigned char>(layer.maxy);
	h.hmin = static_cast<unsigned short>(layer.hmin);
	h.hmax = static_cast<unsigned short>(layer.hmax);
	return h;
}

/// @return 層の数。失敗なら -1
[[nodiscard]] inline int bakeTileLayers(rcContext& ctx, const BakeInput& in, const TileJob& job, std::vector<std::uint8_t>& blob)
{
	const auto lset = buildTileLayers(ctx, in, job.cfg);
	if (!lset) return -1;
	NavCacheCopyCodec codec;
	for (int i = 0; i < lset->nlayers; ++i)
	{
		const rcHeightfieldLayer& layer = lset->layers[i];
		dtTileCacheLayerHeader header = layerHeader(layer, job.tileX, job.tileY, i);
		unsigned char* data = nullptr;
		int size = 0;
		if (dtStatusFailed(dtBuildTileCacheLayer(&codec, &header, layer.heights, layer.areas, layer.cons, &data, &size))) return -1;
		appendTile(blob, data, size);
		dtFree(data);
	}
	return lset->nlayers;
}

} // namespace detail

struct NavCacheBakeResult
{
	std::vector<std::uint8_t> blob;   ///< 空なら失敗 (error に理由)
	std::string error;
	int layerCount = 0;
};

/// @brief 三角形 (添字 3 つで 1 枚) から .navcache を焼く。タイルが小さいほど、障害物を動かしたときに作り直す範囲が狭い
[[nodiscard]] inline NavCacheBakeResult bakeNavCache(std::span<const sgc::Vec3f> vertices,
	std::span<const std::uint32_t> indices, const NavBakeSettings& settings = {})
{
	NavCacheBakeResult result;
	if (const char* why = detail::validate(settings, vertices, indices)) { result.error = why; return result; }
	const detail::BakeInput in = detail::makeBakeInput(vertices, indices);
	const rcConfig base = detail::baseConfig(settings);
	if (base.width > 255) { result.error = "層の幅は 255 cell まで (tileSizeCells を小さく)"; return result; }
	int gw = 0, gh = 0;
	rcCalcGridSize(in.bmin, in.bmax, base.cs, &gw, &gh);
	const int tw = (gw + base.tileSize - 1) / base.tileSize;
	const int th = (gh + base.tileSize - 1) / base.tileSize;
	if (tw <= 0 || th <= 0) { result.error = "三角形の広がりが cellSize に満たない"; return result; }
	if (static_cast<long long>(tw) * th > (1 << 12)) { result.error = "タイル数が多すぎる (tileSizeCells を大きく)"; return result; }

	result.blob.resize(sizeof(NavCacheHeader));
	rcContext ctx(false);
	for (int y = 0; y < th; ++y)
	{
		for (int x = 0; x < tw; ++x)
		{
			const int n = detail::bakeTileLayers(ctx, in, detail::tileConfig(base, in, x, y), result.blob);
			if (n < 0) { result.blob.clear(); result.error = "Recast がタイルの層を焼けなかった"; return result; }
			result.layerCount += n;
		}
	}
	if (result.layerCount == 0) { result.blob.clear(); result.error = "歩ける面が無い (agent の寸法と傾斜の上限を確かめる)"; return result; }
	if (result.layerCount > (1 << 14)) { result.blob.clear(); result.error = "層が多すぎる (tileSizeCells を大きく)"; return result; }

	const int tileBits = std::min(detail::ceilLog2(static_cast<unsigned>(result.layerCount)), 14);
	NavCacheHeader header;
	header.layerCount = static_cast<std::uint32_t>(result.layerCount);
	header.maxTiles = 1u << tileBits;
	header.maxPolysPerTile = 1u << (22 - tileBits);
	rcVcopy(header.origin, in.bmin);
	header.tileWidth = static_cast<float>(base.tileSize) * base.cs;
	header.cellSize = base.cs;
	header.cellHeight = base.ch;
	header.agentHeight = settings.agentHeight;
	header.agentRadius = settings.agentRadius;
	header.agentMaxClimb = settings.agentMaxClimb;
	header.edgeMaxError = settings.edgeMaxError;
	std::memcpy(result.blob.data(), &header, sizeof(header));
	return result;
}

} // namespace mitiru::nav
