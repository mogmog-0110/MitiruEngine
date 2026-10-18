#pragma once

/// @file FluidParamsJson.hpp
/// @brief FluidParams を JSON から読む。キーはメンバー名と同じで、書いていないキーは既定値のまま。
/// @details 知らないキーは読み飛ばす。ゲームの調整ファイルには体の外の値 (落とす高さ・震えの強さ等) も並ぶため。
///          ベクトルは [x, y, z] の配列、dashStyle は "tuck" か "wheel"。型が違う値は誤りにする。

#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <mitiru/data/JsonDiagnostics.hpp>
#include <mitiru/softbody/FluidParams.hpp>

namespace mitiru::softbody
{

namespace detail
{

struct FluidFloatKey { const char* name; float FluidParams::*member; };
struct FluidVecKey { const char* name; sgc::Vec3f FluidParams::*member; };

#define MITIRU_FLUID_KEY(name) FluidFloatKey{#name, &FluidParams::name}
inline constexpr FluidFloatKey kFluidFloatKeys[] = {
	MITIRU_FLUID_KEY(particleSpacing), MITIRU_FLUID_KEY(kernelRadius), MITIRU_FLUID_KEY(substepHz), MITIRU_FLUID_KEY(relaxation),
	MITIRU_FLUID_KEY(tensileK), MITIRU_FLUID_KEY(viscosity), MITIRU_FLUID_KEY(cohesion), MITIRU_FLUID_KEY(restDensityShare),
	MITIRU_FLUID_KEY(collisionRadius), MITIRU_FLUID_KEY(bodyCenterY), MITIRU_FLUID_KEY(bodyExponent), MITIRU_FLUID_KEY(containerHz),
	MITIRU_FLUID_KEY(containerZeta), MITIRU_FLUID_KEY(followRate), MITIRU_FLUID_KEY(centerPullHz), MITIRU_FLUID_KEY(groundGravityScale),
	MITIRU_FLUID_KEY(poseSquashShare), MITIRU_FLUID_KEY(trailPerSpeed), MITIRU_FLUID_KEY(trailMax), MITIRU_FLUID_KEY(trailBase),
	MITIRU_FLUID_KEY(flattenPerSpeed), MITIRU_FLUID_KEY(flattenMax), MITIRU_FLUID_KEY(airContainerHz),
	MITIRU_FLUID_KEY(landContainerDelay), MITIRU_FLUID_KEY(landContainerHz), MITIRU_FLUID_KEY(landRecoverTime),
	MITIRU_FLUID_KEY(sloshAmp), MITIRU_FLUID_KEY(sloshPeriod), MITIRU_FLUID_KEY(sloshSpeed), MITIRU_FLUID_KEY(stretchGrowTime),
	MITIRU_FLUID_KEY(stretchRelaxTime), MITIRU_FLUID_KEY(stretchTurnTime), MITIRU_FLUID_KEY(brakeAccel),
	MITIRU_FLUID_KEY(brakeAccelTime), MITIRU_FLUID_KEY(brakeSpeed), MITIRU_FLUID_KEY(brakeHold), MITIRU_FLUID_KEY(brakeSlide),
	MITIRU_FLUID_KEY(brakeSlideTime), MITIRU_FLUID_KEY(wallFlatten), MITIRU_FLUID_KEY(wallSpread), MITIRU_FLUID_KEY(wallTailPerSpeed),
	MITIRU_FLUID_KEY(wallTailMax), MITIRU_FLUID_KEY(wallTaper), MITIRU_FLUID_KEY(wallTaperPower), MITIRU_FLUID_KEY(wallTailLength),
	MITIRU_FLUID_KEY(wallWaveAmp), MITIRU_FLUID_KEY(wallWaveHz), MITIRU_FLUID_KEY(wallWaves), MITIRU_FLUID_KEY(wallCenterPull),
	MITIRU_FLUID_KEY(podSpeed), MITIRU_FLUID_KEY(podSpacing), MITIRU_FLUID_KEY(podMaxShare), MITIRU_FLUID_KEY(podHz),
	MITIRU_FLUID_KEY(podZeta), MITIRU_FLUID_KEY(podRootRadius), MITIRU_FLUID_KEY(podRadius), MITIRU_FLUID_KEY(podFillet),
	MITIRU_FLUID_KEY(podWave), MITIRU_FLUID_KEY(podWaveHz), MITIRU_FLUID_KEY(zipStretch), MITIRU_FLUID_KEY(limbSpeedMin),
	MITIRU_FLUID_KEY(limbPuddleTime), MITIRU_FLUID_KEY(limbBrakeHold), MITIRU_FLUID_KEY(limbGrowHz), MITIRU_FLUID_KEY(limbGrowZeta),
	MITIRU_FLUID_KEY(limbRetractHz), MITIRU_FLUID_KEY(limbRetractZeta), MITIRU_FLUID_KEY(limbFillet), MITIRU_FLUID_KEY(legLift),
	MITIRU_FLUID_KEY(legSpacing), MITIRU_FLUID_KEY(legRootX), MITIRU_FLUID_KEY(legRootUp), MITIRU_FLUID_KEY(legRadius),
	MITIRU_FLUID_KEY(strideShare), MITIRU_FLUID_KEY(footHalfX), MITIRU_FLUID_KEY(footHalfY), MITIRU_FLUID_KEY(footHalfZ),
	MITIRU_FLUID_KEY(footForward), MITIRU_FLUID_KEY(toeShare), MITIRU_FLUID_KEY(armLength), MITIRU_FLUID_KEY(armRootRadius),
	MITIRU_FLUID_KEY(armTipRadius), MITIRU_FLUID_KEY(armInset), MITIRU_FLUID_KEY(armRootUp), MITIRU_FLUID_KEY(armRaiseExtra),
	MITIRU_FLUID_KEY(armFlail), MITIRU_FLUID_KEY(tuckSpeed), MITIRU_FLUID_KEY(tuckBlendSpeed), MITIRU_FLUID_KEY(tuckShare),
	MITIRU_FLUID_KEY(wheelSpeed), MITIRU_FLUID_KEY(wheelRadius), MITIRU_FLUID_KEY(wheelLift), MITIRU_FLUID_KEY(smearFps),
	MITIRU_FLUID_KEY(splatSigma), MITIRU_FLUID_KEY(surfaceRadius), MITIRU_FLUID_KEY(surfaceMinWeight), MITIRU_FLUID_KEY(surfaceBlur),
	MITIRU_FLUID_KEY(renderSmoothing), MITIRU_FLUID_KEY(renderSmoothRadius), MITIRU_FLUID_KEY(meshVoxelSize), MITIRU_FLUID_KEY(faceHz),
	MITIRU_FLUID_KEY(faceZeta), MITIRU_FLUID_KEY(faceAboveCentroid), MITIRU_FLUID_KEY(stickFaceTiltDeg),
};
#undef MITIRU_FLUID_KEY

inline constexpr FluidVecKey kFluidVecKeys[] = {
	{"gravity", &FluidParams::gravity}, {"bodyHalf", &FluidParams::bodyHalf},
};

[[nodiscard]] inline bool fluidReadVec(const nlohmann::json& v, sgc::Vec3f& out) noexcept
{
	if (!v.is_array() || v.size() != 3 || !v[0].is_number() || !v[1].is_number() || !v[2].is_number()) { return false; }
	out = {v[0].get<float>(), v[1].get<float>(), v[2].get<float>()};
	return true;
}

[[nodiscard]] inline bool fluidReadExtras(const nlohmann::json& root, FluidParams& k, std::string& error)
{
	if (const auto it = root.find("densityIterations"); it != root.end())
	{
		if (!it->is_number_integer()) { error = "fluid.json: 'densityIterations' は整数で書く"; return false; }
		k.densityIterations = it->get<int>();
	}
	if (const auto it = root.find("dashStyle"); it != root.end())
	{
		const bool known = it->is_string() && (*it == "tuck" || *it == "wheel");
		if (!known) { error = "fluid.json: 'dashStyle' は \"tuck\" か \"wheel\" で書く"; return false; }
		k.dashWheel = *it == "wheel" ? 1 : 0;
	}
	return true;
}

} // namespace detail

/// @brief text を読む。読めなければ nullopt を返し、error に場所と理由を書く
[[nodiscard]] inline std::optional<FluidParams> parseFluidParams(std::string_view text, std::string& error)
{
	const nlohmann::json root = data::parseJsonText(text, "fluid.json", error);
	if (root.is_discarded()) { return std::nullopt; }
	if (!root.is_object()) { error = "fluid.json: 中身はオブジェクト ({ ... }) で書く"; return std::nullopt; }
	FluidParams k;
	for (const auto& key : detail::kFluidFloatKeys)
	{
		const auto it = root.find(key.name);
		if (it == root.end()) { continue; }
		if (!it->is_number()) { error = std::string("fluid.json: '") + key.name + "' は数で書く"; return std::nullopt; }
		k.*key.member = it->get<float>();
	}
	for (const auto& key : detail::kFluidVecKeys)
	{
		const auto it = root.find(key.name);
		if (it != root.end() && !detail::fluidReadVec(*it, k.*key.member))
		{
			error = std::string("fluid.json: '") + key.name + "' は [x, y, z] の 3 つの数で書く";
			return std::nullopt;
		}
	}
	if (!detail::fluidReadExtras(root, k, error)) { return std::nullopt; }
	return k;
}

} // namespace mitiru::softbody
