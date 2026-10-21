#pragma once

// ISceneFx から受け取る場面設定の宣言 (色の段、フォグ、トーンマップ、影、輪郭線、bloom)。
// GPU は扱わず、Renderer3D_Dawn.cpp が FrameCB と各パスへ値を渡す。

#include <algorithm>

#include <mitiru/render/ISceneFx.hpp>

#include "DawnLinePass.hpp"
#include "DawnSceneSettings.hpp"
#include "DawnShadowSettings.hpp"

namespace mitiru::render::dawn_detail
{

class DawnSceneFx : public ISceneFx
{
public:
	// ── ISceneFx。対応する処理だけ実装し、残りは既定の no-op ──
	void setSkybox(const Cubemap& cubemap) override;
	void setAmbientColor(const sgc::Colorf& color) override { m_look.ambient = color; }
	[[nodiscard]] sgc::Colorf ambientColor() const noexcept override { return m_look.ambient; }
	void setToonShadowTint(const sgc::Colorf& tint) override { m_look.shadowTint = tint; }
	void setFog(bool enabled, const sgc::Colorf& color, float nearDist, float farDist) override
	{
		m_look.fogOn = enabled; m_look.fogColor = color; m_look.fogNear = nearDist; m_look.fogFar = farDist;
	}
	void setTonemapExposure(float exposure) override { m_look.exposure = exposure; }
	[[nodiscard]] float tonemapExposure() const noexcept override { return m_look.exposure; }
	void setTonemapGamma(float gamma) override { m_look.gamma = gamma; }
	[[nodiscard]] float tonemapGamma() const noexcept override { return m_look.gamma; }
	void setToonRamp(int bands, float softness, const sgc::Colorf& midTint) override
	{
		m_look.toonBands = bands; m_look.toonSoftness = softness; m_look.toonMidTint = midTint;
	}
	void setToonSpecular(float strength, float power) override { m_look.toonSpecular = strength; m_look.toonSpecularPower = power; }
	void setColorGrade(float saturation, float contrast) override { m_look.saturation = saturation; m_look.contrast = contrast; }
	void setHemisphereAmbient(const sgc::Colorf& sky, const sgc::Colorf& ground) override
	{
		m_look.ambientSkySet = sky; m_look.ambientGroundSet = ground;
	}
	void setRimLight(float strength, float power, const sgc::Colorf& color) override
	{
		m_look.rimStrength = strength; m_look.rimPower = power; m_look.rimColor = color;
	}

	// 太陽の影
	void setShadowEnabled(bool enabled) override { m_shadowSettings.setEnabled(enabled); }
	void setShadowDirection(const sgc::Vec3f& dir) override { m_shadowSettings.setDirection(dir); }
	void setShadowCaster(bool enabled) override { m_shadowCaster = enabled; }
	void setCascadedShadowEnabled(bool enabled) override { m_shadowSettings.setCascadedEnabled(enabled); }
	[[nodiscard]] bool isCascadedShadowEnabled() const noexcept override { return m_shadowSettings.cascaded(); }
	void setCascadedShadowAutoFit(bool enabled, float maxDistance) override { m_shadowSettings.setAutoFit(enabled, maxDistance); }
	void setShadowCascadeCount(int count) override { m_shadowSettings.setCascadeCount(count); }
	void setShadowSoftness(float texels) override { m_shadowSettings.setSoftness(texels); }
	void setShadowBias(float worldUnits) override { m_shadowSettings.setBias(worldUnits); }

	// 場面の輪郭線 (印 0)。描画ごとの線は setToonStyle が持つ。
	void setOutlineEnabled(bool enabled) override { m_lineSettings.enabled = enabled; }
	[[nodiscard]] bool isOutlineEnabled() const noexcept override { return m_lineSettings.enabled; }
	void setOutlineMode(OutlineMode mode) override { m_lineSettings.mode = mode; }
	[[nodiscard]] OutlineMode outlineMode() const noexcept override { return m_lineSettings.mode; }
	void setOutlineParams(float widthPx, float) override { m_lineSettings.widthPx = (widthPx < 1.0f) ? 1.0f : widthPx; }
	void setOutlineCaster(bool enabled) override { m_outlineCaster = enabled; }
	void setOutlineFade(float nearDist, float farDist, float minStrength) override
	{
		m_lineSettings.fadeNear = std::max(nearDist, 0.0f);
		m_lineSettings.fadeFar = std::max(farDist, 0.0f);
		m_lineSettings.fadeMin = std::clamp(minStrength, 0.0f, 1.0f);
	}
	void setOutlineDarken(float darken) override { m_lineSettings.darken = std::clamp(darken, 0.0f, 1.0f); }

	void setBloom(bool enabled, float threshold, float strength) override
	{
		m_bloomOn = enabled;
		m_bloomThreshold = std::max(threshold, 0.0f);
		m_bloomStrength = std::clamp(strength, 0.0f, 4.0f);
	}

protected:
	DawnShadowSettings m_shadowSettings;
	DawnLineSettings m_lineSettings;
	bool m_shadowCaster = true;    ///< 以後の描画が影を落とすか。フレーム頭で true
	bool m_outlineCaster = true;   ///< 以後の描画が輪郭線の検出に入るか。フレーム頭で true
	bool m_bloomOn = false;
	float m_bloomThreshold = 1.0f;
	float m_bloomStrength = 0.3f;
	DawnSceneSettings m_look;
};

inline void DawnSceneFx::setSkybox(const Cubemap& cubemap)
{
	m_look.skyOn = false;
	if (!cubemap.valid()) { return; }
	// verticalGradient の cubemap だけを扱う。真上の面は天頂、真下の面は地表の単色。
	const auto& top = cubemap.face(CubeFace::PosY).pixels();
	const auto& bottom = cubemap.face(CubeFace::NegY).pixels();
	if (top.size() < 4 || bottom.size() < 4) { return; }
	m_look.skyZenith = {top[0] / 255.0f, top[1] / 255.0f, top[2] / 255.0f, 1.0f};
	m_look.skyNadir = {bottom[0] / 255.0f, bottom[1] / 255.0f, bottom[2] / 255.0f, 1.0f};
	m_look.skyOn = true;
}

} // namespace mitiru::render::dawn_detail
