#pragma once

/// @file RenderScale.hpp
/// @brief 3D の内部解像度と出力解像度の分離 (TAAU / FSR 3.1)
/// @details 3D は出力の scale 倍の大きさで描き、時間方向の upscaler で出力の大きさへ戻す。scale は 0.5..1.0 を
///          1/kRenderScaleSteps 刻みか FSR の画質の段 (1/1.5、1/1.7) のうち近い方へ丸める。刻むのは、大きさが
///          変わるたびに内部の資源を作り直すため。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

namespace mitiru::render
{

inline constexpr float kMinRenderScale = 0.5f;
inline constexpr float kMaxRenderScale = 1.0f;
inline constexpr int   kRenderScaleSteps = 20;

/// @brief 内部解像度を出力へ戻す方法。Fsr3 はビルドに FSR が無いか作れない時 TAAU になる (ADR 0058)
enum class Upscaler3D : std::uint8_t { Taau, Fsr3 };

/// @brief FSR と同じ画質の段。Off は倍率 1 で upscaler を通さない (AA は setAntiAliasing のまま)。
///        NativeAA は倍率 1 のまま upscaler を AA として通す
enum class UpscaleQuality : std::uint8_t { Off, NativeAA, Quality, Balanced, Performance };

/// @brief 出力の 1 辺が内部の 1 辺の何倍か (AMD の FSR 3.1 の段と同じ値)
[[nodiscard]] constexpr float upscaleRatio(UpscaleQuality q) noexcept
{
	switch (q)
	{
	case UpscaleQuality::Quality:     return 1.5f;
	case UpscaleQuality::Balanced:    return 1.7f;
	case UpscaleQuality::Performance: return 2.0f;
	case UpscaleQuality::Off:
	case UpscaleQuality::NativeAA:    return 1.0f;
	}
	return 1.0f;
}

[[nodiscard]] constexpr float renderScaleFor(UpscaleQuality q) noexcept { return 1.0f / upscaleRatio(q); }

[[nodiscard]] constexpr std::string_view upscaleQualityName(UpscaleQuality q) noexcept
{
	switch (q)
	{
	case UpscaleQuality::Off:         return "off";
	case UpscaleQuality::NativeAA:    return "native";
	case UpscaleQuality::Quality:     return "quality";
	case UpscaleQuality::Balanced:    return "balanced";
	case UpscaleQuality::Performance: return "performance";
	}
	return "off";
}

[[nodiscard]] constexpr std::optional<UpscaleQuality> parseUpscaleQuality(std::string_view s) noexcept
{
	for (auto q : {UpscaleQuality::Off, UpscaleQuality::NativeAA, UpscaleQuality::Quality, UpscaleQuality::Balanced,
	               UpscaleQuality::Performance})
	{
		if (upscaleQualityName(q) == s) { return q; }
	}
	return std::nullopt;
}

[[nodiscard]] constexpr std::string_view upscalerName(Upscaler3D u) noexcept
{
	return u == Upscaler3D::Fsr3 ? "fsr3" : "taau";
}

[[nodiscard]] constexpr std::optional<Upscaler3D> parseUpscaler(std::string_view s) noexcept
{
	if (s == "fsr3") { return Upscaler3D::Fsr3; }
	if (s == "taau") { return Upscaler3D::Taau; }
	return std::nullopt;
}

/// @brief 指定を 0.5..1.0 に収め、1/kRenderScaleSteps 刻みか FSR の段の近い方に丸める
[[nodiscard]] inline float quantizeRenderScale(float scale) noexcept
{
	if (!(scale == scale)) { return kMaxRenderScale; }
	const float c = std::clamp(scale, kMinRenderScale, kMaxRenderScale);
	float best = std::round(c * kRenderScaleSteps) / static_cast<float>(kRenderScaleSteps);
	for (auto q : {UpscaleQuality::Quality, UpscaleQuality::Balanced})
	{
		const float named = renderScaleFor(q);
		if (std::abs(c - named) < std::abs(c - best)) { best = named; }
	}
	return best;
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
