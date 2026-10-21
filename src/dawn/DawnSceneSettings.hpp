#pragma once

// ゲームが ISceneFx と setLight で渡す場面設定。sRGB の色は GPU 定数へ写すときに線形へ変換する

#include <mitiru/render/ColorSpace.hpp>
#include <mitiru/render/Light.hpp>

#include "DawnRenderTypes.hpp"

namespace mitiru::render::dawn_detail
{

struct DawnSceneSettings
{
	sgc::Colorf clear{0.2f, 0.2f, 0.3f, 1.0f};
	sgc::Colorf ambient{0.15f, 0.15f, 0.15f, 1.0f};
	sgc::Colorf ambientSkySet{0.0f, 0.0f, 0.0f, 1.0f};
	sgc::Colorf ambientGroundSet{0.0f, 0.0f, 0.0f, 1.0f};
	sgc::Colorf shadowTint{0.60f, 0.64f, 0.76f, 1.0f};
	sgc::Colorf toonMidTint{0.78f, 0.80f, 0.88f, 1.0f};
	sgc::Colorf fogColor{0.5f, 0.5f, 0.5f, 1.0f};
	sgc::Colorf rimColor{1.0f, 1.0f, 1.0f, 1.0f};
	int toonBands = 1;
	float toonSoftness = 0.12f;
	float toonSpecular = 0.0f;
	float toonSpecularPower = 32.0f;
	float rimStrength = 0.0f;
	float rimPower = 3.0f;
	float fogNear = 0.0f;
	float fogFar = 100.0f;
	bool fogOn = false;
	float exposure = 1.0f;
	float gamma = 2.2f;
	float saturation = 1.0f;
	float contrast = 1.0f;
	Light light = Light::directional({0.0f, -1.0f, 0.0f});
	bool skyOn = false;
	sgc::Colorf skyZenith{0.0f, 0.0f, 0.0f, 1.0f};
	sgc::Colorf skyNadir{0.0f, 0.0f, 0.0f, 1.0f};
	// 物理ベースの空が決めた主光源の色と環境光。どちらも線形で、設定されているときはゲームの指定より優先する
	bool lightLinearSet = false;
	sgc::Colorf lightLinear{1.0f, 1.0f, 1.0f, 1.0f};
	bool ambientLinearSet = false;
	sgc::Colorf ambientUpLinear{0.0f, 0.0f, 0.0f, 1.0f};
	sgc::Colorf ambientDownLinear{0.0f, 0.0f, 0.0f, 1.0f};

	[[nodiscard]] bool hemisphereSet() const noexcept
	{
		return (ambientSkySet.r + ambientSkySet.g + ambientSkySet.b + ambientGroundSet.r + ambientGroundSet.g +
		        ambientGroundSet.b) > 0.0f;
	}

	void fill(FrameCB& cb) const noexcept
	{
		cb.lightDir[0] = light.direction.x; cb.lightDir[1] = light.direction.y; cb.lightDir[2] = light.direction.z;
		const sgc::Colorf lc = lightLinearSet ? lightLinear : linearColor(light.color);
		cb.lightColor[0] = lc.r * light.intensity; cb.lightColor[1] = lc.g * light.intensity; cb.lightColor[2] = lc.b * light.intensity;
		setRgb(cb.ambientSky, ambientLinearSet ? ambientUpLinear : linearColor(hemisphereSet() ? ambientSkySet : ambient));
		setRgb(cb.ambientGround, ambientLinearSet ? ambientDownLinear : linearColor(hemisphereSet() ? ambientGroundSet : ambient));
		setRgb(cb.ambientFlat, ambientLinearSet ? ambientUpLinear : linearColor(ambient));
		setRgb(cb.shadowTint, linearColor(shadowTint));
		setRgb(cb.toonMidTint, linearColor(toonMidTint));
		cb.toonParams[0] = static_cast<float>(toonBands);
		cb.toonParams[1] = toonSoftness;
		cb.toonParams[2] = toonSpecular;
		cb.toonParams[3] = toonSpecularPower;
		const sgc::Colorf rim = linearColor(rimColor);
		cb.rimParams[0] = rim.r * rimStrength; cb.rimParams[1] = rim.g * rimStrength; cb.rimParams[2] = rim.b * rimStrength;
		cb.rimParams[3] = rimPower;
		setRgb(cb.fogColor, linearColor(fogColor));
		cb.fogParams[0] = fogNear; cb.fogParams[1] = fogFar; cb.fogParams[2] = fogOn ? 1.0f : 0.0f;
		// 空の縦グラデーションは cubemap の画素と同じく、sRGB のまま混ぜてから線形にする。線形で混ぜると中間が明るくなるためである
		setRgb(cb.skyZenith, skyZenith, skyOn ? 1.0f : 0.0f);
		setRgb(cb.skyNadir, skyNadir);
	}
};

} // namespace mitiru::render::dawn_detail
