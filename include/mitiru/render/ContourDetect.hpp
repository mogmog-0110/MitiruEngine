#pragma once

/// @file ContourDetect.hpp
/// @brief GBuffer の深度・法線不連続から screen-space 輪郭線を検出する
/// @details NPR シェーダの `edge = 1 - abs(dot(N,V))` は画面端を暗くするリムライトであって
///          幾何的な輪郭線ではない。本ファイルは GBuffer の **深度段差 / 法線の折れ** を見て
///          キャラの外形 (シルエット) と内側の折れ線を拾う、本物の screen-space 輪郭抽出を提供する。
///          ソフトウェア deferred (GBuffer.hpp) と同じ CPU パスなので単体テスト可能。
///
/// 抽出した線にフレーム間で一貫した ID を付けたい用途では、GBufferPixel::velocity
/// (前フレームからの画面内移動量) で前フレーム位置へ reproject して追跡できる。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <mitiru/render/GBuffer.hpp>

namespace mitiru::render
{

/// @brief 輪郭検出のしきい値
struct ContourParams
{
	/// @brief 深度差がこの値を超えると最大強度のエッジ (シルエット / 前後関係の段差)。
	float depthThreshold = 0.02f;
	/// @brief 法線の不連続 (1 - dot) がこの値を超えると最大強度のエッジ (面の折れ)。
	float normalThreshold = 0.35f;
};

/// @brief 座標を [0, max-1] にクランプする (境界で偽のエッジを作らないため)。
[[nodiscard]] inline int clampCoord(int v, int maxExclusive) noexcept
{
	if (v < 0) { return 0; }
	if (v >= maxExclusive) { return maxExclusive - 1; }
	return v;
}

/// @brief 1 ピクセルの輪郭強度を返す [0, 1]
/// @details 4 近傍 (左右上下) との深度差・法線差の最大を、それぞれしきい値で正規化して
///          [0,1] にクランプし、深度系と法線系の大きい方を採用する。
///          平坦面 → 0、シルエット / 折れ → 1 に近づく。
/// @param g 対象 GBuffer (initialize 済み)
/// @param x,y ピクセル座標
/// @param p しきい値
/// @return 輪郭強度 [0,1]
[[nodiscard]] inline float contourAt(const GBuffer& g, int x, int y,
                                     const ContourParams& p = {}) noexcept
{
	const GBufferPixel& c = g.readPixel(x, y);
	const int w = g.width(), h = g.height();

	float maxDepthDiff  = 0.0f;
	float maxNormalDiff = 0.0f;

	constexpr int kDx[4] = {-1, 1, 0, 0};
	constexpr int kDy[4] = {0, 0, -1, 1};
	for (int i = 0; i < 4; ++i)
	{
		const GBufferPixel& n =
			g.readPixel(clampCoord(x + kDx[i], w), clampCoord(y + kDy[i], h));
		maxDepthDiff = std::max(maxDepthDiff, std::abs(c.depth - n.depth));
		const float ndot = c.normal.dot(n.normal);
		maxNormalDiff = std::max(maxNormalDiff, 1.0f - ndot);
	}

	const float depthE = (p.depthThreshold > 0.0f)
		? std::min(1.0f, maxDepthDiff / p.depthThreshold) : 0.0f;
	const float normalE = (p.normalThreshold > 0.0f)
		? std::min(1.0f, maxNormalDiff / p.normalThreshold) : 0.0f;
	return std::max(depthE, normalE);
}

/// @brief GBuffer 全面の輪郭強度マップを返す
/// @param g 対象 GBuffer
/// @param p しきい値
/// @return width*height の輪郭強度 [0,1] (row-major、未初期化 GBuffer なら空)
[[nodiscard]] inline std::vector<float> detectContours(const GBuffer& g,
                                                       const ContourParams& p = {})
{
	std::vector<float> out;
	const int w = g.width(), h = g.height();
	if (w < 1 || h < 1) { return out; }
	out.resize(static_cast<std::size_t>(w) * h);
	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			out[static_cast<std::size_t>(y * w + x)] = contourAt(g, x, y, p);
		}
	}
	return out;
}

} // namespace mitiru::render
