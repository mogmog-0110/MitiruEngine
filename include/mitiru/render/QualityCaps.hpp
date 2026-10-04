#pragma once

/// @file QualityCaps.hpp
/// @brief 画質の上限 (設定画面のプリセット)。ゲームが SceneLook で頼んだ効果を、利用者の機械に合わせて削る。
/// @details 上限は削るだけで、ゲームが頼んでいない効果を足さない。絵作り (色・輪郭線・トゥーン) は
///          ゲームの領分なので触らず、重い効果 (SSAO・bloom・被写界深度・影のカスケード・画面の反射) だけを対象にする。

#include <cstdint>
#include <string_view>

#include <mitiru/render/RenderScale.hpp>

namespace mitiru::render
{

enum class QualityPreset : std::uint8_t { Low, Medium, High, Custom };

/// @brief 動く光の GI (DDGI、ADR 0074) の段。Off は焼いた光のまま。段ごとのレイの数と格子の細かさは gi/DynamicGi.hpp
enum class DynamicGiQuality : std::uint8_t { Off, Low, Medium, High };

struct QualityCaps
{
	bool ambientOcclusion = true;
	bool bloom = true;
	bool depthOfField = true;
	int  maxShadowCascades = 3;   ///< 1..3
	UpscaleQuality upscale = UpscaleQuality::Off;   ///< 内部解像度の段 (プリセットには含めず、利用者が別に選ぶ)
	Upscaler3D     upscaler = Upscaler3D::Taau;     ///< FSR を選んでもビルドに無ければ TAAU で戻す
	bool screenSpaceReflections = true;   ///< 画面の反射 (SSR、ADR 0070)。切ると反射のプローブと IBL だけになる
	DynamicGiQuality dynamicGi = DynamicGiQuality::Medium;   ///< ゲームが頼んだ DDGI の上限。Off なら焼いた光のまま

	[[nodiscard]] bool operator==(const QualityCaps&) const noexcept = default;
};

/// @brief プリセットの上限。Custom は High と同じ (個々の値は設定ファイルが持つ)。
[[nodiscard]] constexpr QualityCaps qualityCapsFor(QualityPreset p) noexcept
{
	switch (p)
	{
	case QualityPreset::Low:    return QualityCaps{ .ambientOcclusion = false, .bloom = false, .depthOfField = false, .maxShadowCascades = 1,
	                                                .screenSpaceReflections = false, .dynamicGi = DynamicGiQuality::Off };
	case QualityPreset::Medium: return QualityCaps{ .ambientOcclusion = false, .bloom = true, .depthOfField = false, .maxShadowCascades = 2,
	                                                .screenSpaceReflections = false, .dynamicGi = DynamicGiQuality::Low };
	case QualityPreset::High:
	case QualityPreset::Custom: return QualityCaps{};
	}
	return QualityCaps{};
}

[[nodiscard]] constexpr std::string_view dynamicGiQualityName(DynamicGiQuality q) noexcept
{
	switch (q)
	{
	case DynamicGiQuality::Off:    return "off";
	case DynamicGiQuality::Low:    return "low";
	case DynamicGiQuality::Medium: return "medium";
	case DynamicGiQuality::High:   return "high";
	}
	return "medium";
}

/// @brief 名前から段。知らない名前なら ok = false で Medium
[[nodiscard]] constexpr DynamicGiQuality dynamicGiQualityFromName(std::string_view name, bool& ok) noexcept
{
	for (const auto q : {DynamicGiQuality::Off, DynamicGiQuality::Low, DynamicGiQuality::Medium, DynamicGiQuality::High})
	{
		if (dynamicGiQualityName(q) == name)
		{
			ok = true;
			return q;
		}
	}
	ok = false;
	return DynamicGiQuality::Medium;
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
