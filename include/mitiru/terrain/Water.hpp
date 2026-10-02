#pragma once

/// @file Water.hpp
/// @brief 水面 (高さ一定の長方形)。見た目の設定と、ゲームが同じフレームで使う問い合わせをまとめる
/// @details 波は描画にだけ使い、水面の高さは常に height とする。
///          泳ぎと沈水の判定には waterDepthAt で求めた地形との差を使い、波の影響を受けない

#include <algorithm>
#include <cstdint>
#include <span>

#include <mitiru/terrain/Heightfield.hpp>

namespace mitiru::terrain
{

struct WaterBody
{
	float minX = -64.0f;
	float minZ = -64.0f;
	float maxX = 64.0f;
	float maxZ = 64.0f;
	float height = 0.0f;               ///< 水面の y
	float shallowColor[3] = {0.20f, 0.62f, 0.66f};
	float deepColor[3] = {0.02f, 0.12f, 0.22f};
	float absorption[3] = {0.45f, 0.11f, 0.07f};  ///< 1 m あたりの減衰 (赤が先に消える)
	float foamColor[3] = {0.95f, 0.97f, 1.0f};
	float foamDepth = 0.35f;           ///< 地形や物との水深の差がこれ以下の所に泡を出す (m)
	float refraction = 0.03f;          ///< 屈折で画面をずらす量 (画面の幅に対する割合)
	float waveAmplitude = 0.35f;       ///< 法線を揺らす強さ (0 で鏡面)
	float waveScale = 0.35f;           ///< 波の細かさ (1 m あたりの周期)
	float waveSpeed = 0.6f;
	float reflectivity = 0.6f;         ///< 真横から見たときの空の映り込み
	std::int32_t toonBands = 0;        ///< 0 = 滑らか、2..4 = 色と泡を段で刻む
};

[[nodiscard]] constexpr bool insideWater(const WaterBody& w, float x, float z) noexcept
{
	return x >= w.minX && x <= w.maxX && z >= w.minZ && z <= w.maxZ;
}

/// @brief 水面の外か、地形が水面より上なら水深は 0
[[nodiscard]] inline float waterDepthAt(const WaterBody& w, const Heightfield& h, float x, float z) noexcept
{
	if (!insideWater(w, x, z)) { return 0.0f; }
	return std::max(0.0f, w.height - h.heightAt(x, z));
}

/// @brief 沈んでいる水面の添字。該当しなければ -1。重なっていれば先の水面を返す
[[nodiscard]] inline int submergedIn(std::span<const WaterBody> bodies, const Vec3& p) noexcept
{
	for (std::size_t i = 0; i < bodies.size(); ++i)
	{
		if (insideWater(bodies[i], p.x, p.z) && p.y < bodies[i].height) { return static_cast<int>(i); }
	}
	return -1;
}

} // namespace mitiru::terrain
