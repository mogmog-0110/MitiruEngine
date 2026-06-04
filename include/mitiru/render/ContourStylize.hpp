#pragma once

/// @file ContourStylize.hpp
/// @brief 輪郭強度マップ [0,1] にアーティスト制御（線幅 + プロファイル）を掛ける後処理
/// @details `ContourDetect`(#2) / `TemporalContour`(#8) が出す輪郭強度に、線の太さと
///          falloff の硬さを乗せる。dilation（最大値の広がり）で太らせ、距離 falloff で
///          縁を減衰、gamma でプロファイルの硬さを調整する。NPR/トゥーンの作風出しに使う。
///
/// @note 真の「入り抜き（arc-length taper）」はラスタ後処理だけでは定義できない（閉曲線に
///       端点が無い）。それは object-space silhouette（`SilhouetteExtract`）をストローク化して
///       弧長パラメータを得る別タスク（inbox #12 future）。本ファイルは raster の太さ・質感まで。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <mitiru/render/ContourDetect.hpp>   // clampCoord

namespace mitiru::render
{

/// @brief 輪郭スタイライズのパラメータ
struct ContourStylizeParams
{
	float lineWidth = 1.5f;     ///< 線の半径(px)。大きいほど太い（dilation 半径）。
	float profileGamma = 0.8f;  ///< プロファイル硬さ。<1 でくっきり / >1 で柔らかい。
	/// @brief 低強度カットオフ再マップ（#14）。0 = 従来挙動。
	/// @details 出力に `v = clamp((v - cutoff) / (1 - cutoff), 0, 1)` を適用し、淡い「もや」を落として
	///          線の芯（高強度）を保つ。gamma<1 が弱値を持ち上げて「もや」を増幅する症状の汎用ノブ。
	float cutoff = 0.0f;
};

/// @brief 輪郭強度に線幅プロファイルを掛ける（dilation + 距離 falloff + gamma）。
/// @param contour 入力の輪郭強度 [0,1]（row-major、w*h）
/// @param w,h     サイズ
/// @param p       パラメータ
/// @return スタイライズ後の輪郭強度 [0,1]（同サイズ。空入力/サイズ不整合なら入力をそのまま返す）
[[nodiscard]] inline std::vector<float> stylizeContour(
	const std::vector<float>& contour, int w, int h, const ContourStylizeParams& p = {})
{
	const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
	if (w < 1 || h < 1 || contour.size() != n) { return contour; }

	const float radius = std::max(0.0f, p.lineWidth);
	const int   r = static_cast<int>(std::ceil(radius));
	std::vector<float> out(n, 0.0f);

	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			float best = 0.0f;
			for (int dy = -r; dy <= r; ++dy)
			{
				for (int dx = -r; dx <= r; ++dx)
				{
					const float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
					if (dist > radius) { continue; }
					const int sx = clampCoord(x + dx, w), sy = clampCoord(y + dy, h);
					const float falloff = 1.0f - dist / (radius + 1.0f);
					best = std::max(best, contour[static_cast<std::size_t>(sy * w + sx)] * falloff);
				}
			}
			float v = std::pow(best, p.profileGamma);
			if (p.cutoff > 0.0f && p.cutoff < 1.0f)   // #14: 淡い「もや」を落とす再マップ
			{
				v = std::clamp((v - p.cutoff) / (1.0f - p.cutoff), 0.0f, 1.0f);
			}
			out[static_cast<std::size_t>(y * w + x)] = v;
		}
	}
	return out;
}

} // namespace mitiru::render
