#pragma once

/// @file RenderScale.hpp
/// @brief 3D の内部解像度と出力解像度の分離 (TAAU)
/// @details 3D は出力の scale 倍の大きさで描き、TAA の履歴で出力の大きさへ戻す。scale は 0.5..1.0 を
///          1/kRenderScaleSteps 刻みに丸める。刻むのは、大きさが変わるたびに内部の資源を作り直すため。

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mitiru::render
{

inline constexpr float kMinRenderScale = 0.5f;
inline constexpr float kMaxRenderScale = 1.0f;
inline constexpr int   kRenderScaleSteps = 20;

/// @brief 指定を 0.5..1.0 に収め、1/kRenderScaleSteps 刻みに丸める
[[nodiscard]] inline float quantizeRenderScale(float scale) noexcept
{
	if (!(scale == scale)) { return kMaxRenderScale; }
	const float c = std::clamp(scale, kMinRenderScale, kMaxRenderScale);
	return std::round(c * kRenderScaleSteps) / static_cast<float>(kRenderScaleSteps);
}

/// @brief 出力の 1 辺と scale から内部の 1 辺 (1 以上)
[[nodiscard]] inline int internalRenderExtent(int output, float scale) noexcept
{
	const float q = quantizeRenderScale(scale);
	return std::max(1, static_cast<int>(std::lround(static_cast<float>(output) * q)));
}

/// @brief ずらしの相の数。出力 1 画素に内部の標本が 8 個ほど落ちるよう、縮めた面積に反比例して増やす
[[nodiscard]] inline std::uint32_t upscaleJitterPhases(float scale) noexcept
{
	const float q = quantizeRenderScale(scale);
	const auto n = static_cast<std::uint32_t>(std::lround(8.0f / (q * q)));
	return std::clamp<std::uint32_t>(n, 8u, 32u);
}

} // namespace mitiru::render
