#pragma once

/// @file SceneLookAtmosphere.hpp
/// @brief SceneLook の末尾に置く空と体積フォグの指定 (ABI v48、ADR 0056 / 0057)。依存を持たない POD だけを置き、
///        Screen.hpp と SceneLook.hpp の両方から include する。
/// @details 空とフォグは絵だけを変え、GameMemory には触れない。毎フレーム sceneLook3D で頼み直す。太陽は主光源の向きのまま
///          (時刻から決めたい時は render/Atmosphere.hpp の sunDirectionFromTimeOfDay を DLL で呼んで light3D に渡す)。

#include <cstdint>
#include <type_traits>

namespace mitiru::render
{

/// @brief 物理ベースの空と空気遠近のうち、ゲームが決める部分。大気の組成は host の既定
struct SkyLook
{
	std::uint8_t enabled       = 0;
	std::uint8_t toonBands     = 0;     ///< 0 = 連続。2..8 なら空と空気遠近の明るさを段に切る
	std::uint8_t driveSunLight = 0;     ///< 1 = 主光源の色をカメラ位置で減衰した太陽の色にする
	std::uint8_t driveAmbient  = 0;     ///< 1 = 半球の環境光を空の明るさから決める
	float sunIlluminance         = 8.0f;     ///< 空の明るさ
	float aerialPerspectiveScale = 1.0f;     ///< 空気遠近の距離の倍率。大きいほど近くから霞む
	float metersPerUnit          = 1.0f;     ///< 世界の 1 単位が何 m か
	float groundHeight           = 0.0f;     ///< 地表の世界 y
	float sunDiskRadius          = 0.0093f;  ///< 太陽の円盤の見かけの半径 (rad)。0 で円盤を描かない
};

/// @brief 体積フォグのうち、ゲームが決める部分。距離フォグ (SceneLook::fog) とは別に掛かる
struct VolumetricFogLook
{
	std::uint8_t  enabled = 0;
	std::uint8_t  shadows = 1;          ///< 1 = 主光源の影 (カスケード) を見る
	std::uint16_t _pad = 0;
	float density         = 0.02f;      ///< 基準の高さでの消散係数 (1/世界単位)
	float heightFalloff   = 0.15f;      ///< 高さ 1 単位あたりの減り方
	float baseHeight      = 0.0f;       ///< 密度が density になる世界 y
	float constantDensity = 0.0f;       ///< 高さに関係なく足す消散係数
	float albedo[3]       = {0.9f, 0.9f, 0.95f};
	float anisotropy      = 0.6f;       ///< Henyey-Greenstein の g
	float range           = 80.0f;      ///< 積む奥の端 (世界単位)
	float sunScale        = 1.0f;
	float localLightScale = 1.0f;
	float ambientScale    = 1.0f;
};

static_assert(sizeof(SkyLook) == 24 && std::is_trivially_copyable_v<SkyLook>, "SkyLook wire size 固定 (v48)");
static_assert(sizeof(VolumetricFogLook) == 52 && std::is_trivially_copyable_v<VolumetricFogLook>,
              "VolumetricFogLook wire size 固定 (v48)");

}  // namespace mitiru::render
