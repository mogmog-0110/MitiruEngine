#pragma once

/// @file OutdoorDrawPod.hpp
/// @brief 屋外の 1 枚 (world.json) を描くときに毎フレーム変わる描画だけの指定 (ABI v49、ADR 0060 / 0062)
/// @details 当たり判定には効かない。風・水位・草を押し倒す球は GameMemory の値からゲームが毎フレーム渡す。
///          水位を変えるなら、ゲームは waterDepthAt に同じ waterOffset を足して判定をそろえる。

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace mitiru::render
{

inline constexpr int kMaxOutdoorBenders = 8;

inline constexpr std::uint32_t kOutdoorNoGrass   = 1u << 0;
inline constexpr std::uint32_t kOutdoorNoScatter = 1u << 1;
inline constexpr std::uint32_t kOutdoorNoWater   = 1u << 2;

/// @brief 160 byte の POD。既定の値なら world.json に書いたとおりに描く
struct OutdoorDrawPod
{
	float         timeSec = 0.0f;              ///< 草のなびきと波の時刻
	float         windDir[2] = {0.0f, 0.0f};   ///< 風の向き (x, z)。両方 0 なら world.json の向き
	float         windStrength = -1.0f;        ///< 草の先端のずれ (m)。負なら world.json の値
	float         waterOffset = 0.0f;          ///< 水面の高さに足す m (潮の満ち引き)
	float         waveScale = 1.0f;            ///< 波の強さに掛ける倍率
	std::uint32_t flags = 0;                   ///< kOutdoorNo* で草・撒いた物・水面を個別に描かない
	std::uint32_t benderCount = 0;
	float         benders[kMaxOutdoorBenders][4] = {};   ///< 草を押し倒す球 (中心 xyz、半径)。キャラの足元など
};

static_assert(sizeof(OutdoorDrawPod) == 160, "OutdoorDrawPod は 160 byte の POD (ABI v49)");
static_assert(offsetof(OutdoorDrawPod, flags) == 24 && offsetof(OutdoorDrawPod, benders) == 32, "OutdoorDrawPod の並び (ABI v49)");
static_assert(std::is_standard_layout_v<OutdoorDrawPod> && std::is_trivially_copyable_v<OutdoorDrawPod>);

} // namespace mitiru::render
