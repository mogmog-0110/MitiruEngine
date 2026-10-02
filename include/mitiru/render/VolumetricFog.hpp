#pragma once

/// @file VolumetricFog.hpp
/// @brief froxel の体積フォグ (Wronski 2014 / Hillaire 2015) の設定と、奥行きの切り方
/// @details 画面を 8 画素四方、奥行きを kFogSlices 枚に切った箱ごとに、密度と光の散乱を積む。
///          奥行きは t^2 で切り、近くを細かくする。GPU (DX12FogShaders.hpp) も同じ式を使う。

#include <algorithm>
#include <cmath>

#include <sgc/types/Color.hpp>

namespace mitiru::render
{

/// @brief 体積フォグの指定
struct VolumetricFogSettings
{
	bool  enabled = false;
	float density = 0.02f;          ///< 基準の高さでの消散係数 (1/世界単位)
	float heightFalloff = 0.15f;    ///< 高さ 1 単位あたりの減り方 (0 で高さに関係なく一様)
	float baseHeight = 0.0f;        ///< 密度が density になる世界 y
	float constantDensity = 0.0f;   ///< 高さに関係なく足す消散係数
	sgc::Colorf albedo{0.9f, 0.9f, 0.95f, 1.0f};  ///< 散乱の割合と色 (1 = 吸収なし)
	float anisotropy = 0.6f;        ///< Henyey-Greenstein の g。正で光の向こう側が明るい
	float range = 80.0f;            ///< froxel の奥の端 (世界単位)。これより奥は積まない
	float sunScale = 1.0f;          ///< 主光源の散乱の倍率
	float localLightScale = 1.0f;   ///< 点光源・スポットの散乱の倍率
	float ambientScale = 1.0f;      ///< 半球の環境光の散乱の倍率
	float temporalBlend = 0.1f;     ///< 今フレームを混ぜる割合 (1 = 履歴を使わない)
	bool  shadows = true;           ///< 主光源の影 (カスケード) を見る
};

namespace fog
{

inline constexpr int kFogTilePixels = 8;
inline constexpr int kFogSlices = 64;

/// @brief 奥行き方向の位置 t (0..1、froxel の番号 / 枚数) から視線に沿った距離
[[nodiscard]] inline float sliceToDistance(float t, float range) noexcept
{
	return range * t * t;
}

/// @brief 距離から奥行き方向の位置 t (0..1)
[[nodiscard]] inline float distanceToSlice(float distance, float range) noexcept
{
	if (range <= 0.0f) { return 0.0f; }
	return std::sqrt(std::clamp(distance / range, 0.0f, 1.0f));
}

/// @brief 高さ y での消散係数
[[nodiscard]] inline float densityAt(const VolumetricFogSettings& s, float y) noexcept
{
	return s.density * std::exp(-s.heightFalloff * (y - s.baseHeight)) + s.constantDensity;
}

/// @brief 画面の大きさから froxel の横と縦の数
[[nodiscard]] inline int froxelCount(int pixels) noexcept
{
	return std::max(1, (pixels + kFogTilePixels - 1) / kFogTilePixels);
}

} // namespace fog

} // namespace mitiru::render
