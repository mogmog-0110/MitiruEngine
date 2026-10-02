#pragma once

/// @file NavCacheBlob.hpp
/// @brief 障害物に応じてナビメッシュを作り直すための .navcache の形式。書き出す NavCacheBake.hpp と
///        読み込む DynamicNavMesh.hpp が共有する
///
/// [NavCacheHeader][layer 0: NavBlobTile + DetourTileCache の圧縮層][layer 1]... の順に並ぶ。各層は
/// dtBuildTileCacheLayer の出力で、書き出し時と同じ Detour の DT_TILECACHE_VERSION でのみ読める。
/// .navmesh は生成済みのポリゴンを持つが、.navcache は高さの格子を持ち、障害物を反映して実行時にポリゴンを生成する。

#include <cstdint>

#include "mitiru/nav/NavMeshBlob.hpp"

namespace mitiru::nav
{

inline constexpr std::uint32_t kNavCacheMagic = 0x43564E4Du;   ///< "MNVC" (little endian)
inline constexpr std::uint32_t kNavCacheVersion = 1;

struct NavCacheHeader
{
	std::uint32_t magic = kNavCacheMagic;
	std::uint32_t version = kNavCacheVersion;
	std::uint32_t layerCount = 0;
	std::uint32_t maxTiles = 0;          ///< dtNavMeshParams::maxTiles (層の数以上の 2 の冪)
	std::uint32_t maxPolysPerTile = 0;
	float origin[3] = {0.0f, 0.0f, 0.0f};
	float tileWidth = 0.0f;              ///< タイル 1 枚の XZ の幅 (m)
	float cellSize = 0.0f;
	float cellHeight = 0.0f;
	float agentHeight = 0.0f;
	float agentRadius = 0.0f;            ///< 障害物はこれだけ太らせて塗る (焼いた床と同じく体の幅を含める)
	float agentMaxClimb = 0.0f;
	float edgeMaxError = 0.0f;
};

} // namespace mitiru::nav
