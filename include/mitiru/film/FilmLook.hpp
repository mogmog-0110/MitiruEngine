#pragma once

/// @file FilmLook.hpp
/// @brief JSON の `look` に書ける名前と SceneLook の項目の対応表。名前は SceneLook の項目名と同じで、入れ子は sky.xxx / volumetricFog.xxx と書く。

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include <mitiru/render/SceneLook.hpp>

namespace mitiru::film
{

enum class LookKind : std::uint8_t { Float, Bool, U8, Vec3 };

struct LookField
{
	const char*       name;
	std::size_t       offset;
	LookKind          kind;
};

#define MITIRU_FILM_LOOK(name, kind) {#name, offsetof(render::SceneLook, name), LookKind::kind}

inline constexpr LookField kLookFields[] = {
	MITIRU_FILM_LOOK(exposure, Float), MITIRU_FILM_LOOK(gamma, Float),
	MITIRU_FILM_LOOK(ambient, Vec3), MITIRU_FILM_LOOK(ambientSky, Vec3), MITIRU_FILM_LOOK(ambientGround, Vec3),
	MITIRU_FILM_LOOK(outline, Bool), MITIRU_FILM_LOOK(outlineWidthPx, Float), MITIRU_FILM_LOOK(outlineThreshold, Float),
	MITIRU_FILM_LOOK(outlineDarken, Float),
	MITIRU_FILM_LOOK(shadow, Bool), MITIRU_FILM_LOOK(shadowDirection, Vec3), MITIRU_FILM_LOOK(shadowCascaded, Bool),
	MITIRU_FILM_LOOK(shadowCascadeAutoFit, Bool), MITIRU_FILM_LOOK(shadowDistance, Float), MITIRU_FILM_LOOK(shadowSoftness, Float),
	MITIRU_FILM_LOOK(shadowBias, Float), MITIRU_FILM_LOOK(shadowMapLog2, U8),
	MITIRU_FILM_LOOK(fog, Bool), MITIRU_FILM_LOOK(fogColor, Vec3), MITIRU_FILM_LOOK(fogNear, Float), MITIRU_FILM_LOOK(fogFar, Float),
	MITIRU_FILM_LOOK(bloom, Bool), MITIRU_FILM_LOOK(bloomThreshold, Float), MITIRU_FILM_LOOK(bloomStrength, Float),
	MITIRU_FILM_LOOK(ao, Bool), MITIRU_FILM_LOOK(aoRadius, Float), MITIRU_FILM_LOOK(aoStrength, Float),
	MITIRU_FILM_LOOK(saturation, Float), MITIRU_FILM_LOOK(contrast, Float), MITIRU_FILM_LOOK(colorLut, U8),
	MITIRU_FILM_LOOK(dofStart, Float), MITIRU_FILM_LOOK(dofEnd, Float), MITIRU_FILM_LOOK(dofStrength, Float),
	MITIRU_FILM_LOOK(motionBlur, Float), MITIRU_FILM_LOOK(aaMode, U8), MITIRU_FILM_LOOK(shadingModel, U8),
	MITIRU_FILM_LOOK(renderScale20, U8), MITIRU_FILM_LOOK(cloudCover, U8), MITIRU_FILM_LOOK(wetness, U8),
	MITIRU_FILM_LOOK(toonBands, U8), MITIRU_FILM_LOOK(toonSoftness, Float), MITIRU_FILM_LOOK(toonMidTint, Vec3),
	MITIRU_FILM_LOOK(toonSpecular, Float), MITIRU_FILM_LOOK(toonSpecularPower, Float),
	MITIRU_FILM_LOOK(rimStrength, Float), MITIRU_FILM_LOOK(rimPower, Float), MITIRU_FILM_LOOK(rimColor, Vec3),
	MITIRU_FILM_LOOK(autoExposureSpeed, Float), MITIRU_FILM_LOOK(autoExposureRangeEv, Float), MITIRU_FILM_LOOK(autoExposureBiasEv, Float),
	MITIRU_FILM_LOOK(contactShadow, Float), MITIRU_FILM_LOOK(contactShadowDistance, Float),
	{"sky.enabled", offsetof(render::SceneLook, sky) + offsetof(render::SkyLook, enabled), LookKind::U8},
	{"sky.sunIlluminance", offsetof(render::SceneLook, sky) + offsetof(render::SkyLook, sunIlluminance), LookKind::Float},
	{"sky.aerialPerspectiveScale", offsetof(render::SceneLook, sky) + offsetof(render::SkyLook, aerialPerspectiveScale), LookKind::Float},
	{"sky.driveSunLight", offsetof(render::SceneLook, sky) + offsetof(render::SkyLook, driveSunLight), LookKind::U8},
	{"sky.driveAmbient", offsetof(render::SceneLook, sky) + offsetof(render::SkyLook, driveAmbient), LookKind::U8},
	{"volumetricFog.enabled", offsetof(render::SceneLook, volumetricFog) + offsetof(render::VolumetricFogLook, enabled), LookKind::U8},
	{"volumetricFog.density", offsetof(render::SceneLook, volumetricFog) + offsetof(render::VolumetricFogLook, density), LookKind::Float},
	{"volumetricFog.heightFalloff", offsetof(render::SceneLook, volumetricFog) + offsetof(render::VolumetricFogLook, heightFalloff), LookKind::Float},
	{"volumetricFog.baseHeight", offsetof(render::SceneLook, volumetricFog) + offsetof(render::VolumetricFogLook, baseHeight), LookKind::Float},
	{"volumetricFog.anisotropy", offsetof(render::SceneLook, volumetricFog) + offsetof(render::VolumetricFogLook, anisotropy), LookKind::Float},
	{"volumetricFog.albedo", offsetof(render::SceneLook, volumetricFog) + offsetof(render::VolumetricFogLook, albedo), LookKind::Vec3},
	{"indirect.flags", offsetof(render::SceneLook, indirect) + offsetof(render::IndirectLightLook, flags), LookKind::U8},
	{"indirect.toonBands", offsetof(render::SceneLook, indirect) + offsetof(render::IndirectLightLook, toonBands), LookKind::U8},
	{"indirect.giIntensity", offsetof(render::SceneLook, indirect) + offsetof(render::IndirectLightLook, giIntensity), LookKind::Float},
	{"volumetricFog.shadows", offsetof(render::SceneLook, volumetricFog) + offsetof(render::VolumetricFogLook, shadows), LookKind::U8},
	{"volumetricFog.constantDensity", offsetof(render::SceneLook, volumetricFog) + offsetof(render::VolumetricFogLook, constantDensity), LookKind::Float},
	{"volumetricFog.range", offsetof(render::SceneLook, volumetricFog) + offsetof(render::VolumetricFogLook, range), LookKind::Float},
	{"volumetricFog.sunScale", offsetof(render::SceneLook, volumetricFog) + offsetof(render::VolumetricFogLook, sunScale), LookKind::Float},
	{"volumetricFog.localLightScale", offsetof(render::SceneLook, volumetricFog) + offsetof(render::VolumetricFogLook, localLightScale), LookKind::Float},
	{"volumetricFog.ambientScale", offsetof(render::SceneLook, volumetricFog) + offsetof(render::VolumetricFogLook, ambientScale), LookKind::Float},
};

#undef MITIRU_FILM_LOOK

[[nodiscard]] inline const LookField* findLookField(std::string_view name) noexcept
{
	for (const LookField& f : kLookFields)
	{
		if (name == f.name) { return &f; }
	}
	return nullptr;
}

/// @brief Float / Bool / U8 の項目に 1 つの数を書く。軌跡から毎フレーム呼ばれる。Vec3 の項目には何も書かず false を返す。
inline bool setLookScalar(render::SceneLook& look, const LookField& f, float v) noexcept
{
	auto* base = reinterpret_cast<unsigned char*>(&look) + f.offset;
	switch (f.kind)
	{
	case LookKind::Float: std::memcpy(base, &v, sizeof(float)); return true;
	case LookKind::Bool: *reinterpret_cast<bool*>(base) = v > 0.5f; return true;
	case LookKind::U8: *base = static_cast<unsigned char>(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v + 0.5f)); return true;
	case LookKind::Vec3: return false;
	}
	return false;
}

inline void setLookVec3(render::SceneLook& look, const LookField& f, float x, float y, float z) noexcept
{
	float v[3] = {x, y, z};
	std::memcpy(reinterpret_cast<unsigned char*>(&look) + f.offset, v, sizeof(v));
}

}  // namespace mitiru::film
