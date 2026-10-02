#pragma once

/// @file QualityCaps.hpp
/// @brief 画質の上限 (設定画面のプリセット)。ゲームが SceneLook で頼んだ効果を、利用者の機械に合わせて削る。
/// @details 上限は削るだけで、ゲームが頼んでいない効果を足さない。絵作り (色・輪郭線・トゥーン) は
///          ゲームの領分なので触らず、重い効果 (SSAO・bloom・被写界深度・影のカスケード) だけを対象にする。

#include <cstdint>
#include <string_view>

#include <mitiru/render/RenderScale.hpp>

namespace mitiru::render
{

enum class QualityPreset : std::uint8_t { Low, Medium, High, Custom };

struct QualityCaps
{
	bool ambientOcclusion = true;
	bool bloom = true;
	bool depthOfField = true;
	int  maxShadowCascades = 3;   ///< 1..3
	UpscaleQuality upscale = UpscaleQuality::Off;   ///< 内部解像度の段 (プリセットには含めず、利用者が別に選ぶ)
	Upscaler3D     upscaler = Upscaler3D::Taau;     ///< FSR を選んでもビルドに無ければ TAAU で戻す

	[[nodiscard]] bool operator==(const QualityCaps&) const noexcept = default;
};

/// @brief プリセットの上限。Custom は High と同じ (個々の値は設定ファイルが持つ)。
[[nodiscard]] constexpr QualityCaps qualityCapsFor(QualityPreset p) noexcept
{
	switch (p)
	{
	case QualityPreset::Low:    return QualityCaps{ false, false, false, 1 };
	case QualityPreset::Medium: return QualityCaps{ false, true, false, 2 };
	case QualityPreset::High:
	case QualityPreset::Custom: return QualityCaps{};
	}
	return QualityCaps{};
}

[[nodiscard]] constexpr std::string_view qualityPresetName(QualityPreset p) noexcept
{
	switch (p)
	{
	case QualityPreset::Low:    return "low";
	case QualityPreset::Medium: return "medium";
	case QualityPreset::High:   return "high";
	case QualityPreset::Custom: return "custom";
	}
	return "high";
}

}  // namespace mitiru::render
