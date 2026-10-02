#pragma once

/// @file NavMeshBlob.hpp
/// @brief 焼いたナビメッシュ (.navmesh) のファイル形式。焼く側 (NavMeshBake.hpp) と読む側 (NavMesh.hpp) が共有する
///
/// [NavBlobHeader][tile 0: NavBlobTile + Detour のタイルデータ][tile 1]... の順に並ぶ。タイルデータは
/// dtCreateNavMeshData の出力そのままで、Detour の構造体を直に写したものなので、焼いたのと同じ
/// エンディアン・同じ Detour 版 (DT_NAVMESH_VERSION) でしか読めない。版が合わなければ読む側が断る。

#include <cstdint>

namespace mitiru::nav
{

inline constexpr std::uint32_t kNavBlobMagic = 0x56414E4Du;   ///< "MNAV" (little endian)
inline constexpr std::uint32_t kNavBlobVersion = 1;

/// @brief dtNavMeshParams と、焼いたときの agent 寸法 (読む側が「どの大きさ用か」を確かめるため)
struct NavBlobHeader
{
	std::uint32_t magic = kNavBlobMagic;
	std::uint32_t version = kNavBlobVersion;
	std::uint32_t tileCount = 0;
	std::uint32_t maxTiles = 0;
	std::uint32_t maxPolysPerTile = 0;
	float origin[3] = {0.0f, 0.0f, 0.0f};
	float tileWidth = 0.0f;
	float tileHeight = 0.0f;
	float agentRadius = 0.0f;
	float agentHeight = 0.0f;
	float agentMaxClimb = 0.0f;
};

/// @brief タイル 1 枚の前置き。直後に dataSize バイトの Detour タイルデータが続く (4 バイト境界まで詰め物)
struct NavBlobTile
{
	std::uint32_t dataSize = 0;
};

[[nodiscard]] constexpr std::uint32_t navBlobPadded(std::uint32_t size) noexcept
{
	return (size + 3u) & ~3u;
}

} // namespace mitiru::nav
