#pragma once

/// @file FluidParams.hpp
/// @brief 流れる体 (FluidBody) の調整値。長さは m、時間は秒、角度は度。JSON のキーはメンバー名と同じ (FluidParamsJson.hpp)。
/// @details 既定値は metamorphosis の docs/fluid.json と、その参照実装 (tools/previs/fluid.py) の器の寸法。

#include <cstdint>

#include <sgc/math/Vec3.hpp>

namespace mitiru::softbody
{

struct FluidParams
{
	// 粒 (Position Based Fluids)
	float particleSpacing = 0.115f;
	float kernelRadius = 0.29f;
	float substepHz = 240.0f;
	int densityIterations = 3;
	float relaxation = 3000.0f;
	float tensileK = 0.002f;
	float viscosity = 0.06f;
	float cohesion = 20.0f;
	float restDensityShare = 0.8f;
	float collisionRadius = 0.04f;
	sgc::Vec3f gravity{0.0f, -30.0f, 0.0f};

	// 器。足元が原点、y が上、z が前の超楕円体
	sgc::Vec3f bodyHalf{0.5f, 0.47f, 0.45f};
	float bodyCenterY = 0.55f;
	float bodyExponent = 2.6f;
	float containerHz = 4.5f;
	float containerZeta = 0.25f;
	float followRate = 8.0f;
	float centerPullHz = 0.7f;
	float groundGravityScale = 0.3f;
	float poseSquashShare = 0.4f;
	float trailPerSpeed = 0.03f;
	float trailMax = 0.45f;
	float trailBase = 0.35f;
	float flattenPerSpeed = 0.012f;
	float flattenMax = 0.2f;
	float airContainerHz = 2.0f;
	float landContainerDelay = 0.1f;
	float landContainerHz = 0.8f;
	float landRecoverTime = 0.35f;
	float sloshAmp = 0.06f;
	float sloshPeriod = 2.4f;
	float sloshSpeed = 1.5f;
	float stretchGrowTime = 0.12f;
	float stretchRelaxTime = 0.35f;
	float stretchTurnTime = 0.25f;

	// 急停止 (ならした減速で決め、器を前へずらす)
	float brakeAccel = 30.0f;
	float brakeAccelTime = 0.06f;
	float brakeSpeed = 8.0f;
	float brakeHold = 0.2f;
	float brakeSlide = 0.3f;
	float brakeSlideTime = 0.6f;

	// 壁 (しずくの形で長い尾を引く)
	float wallFlatten = 0.55f;
	float wallSpread = 0.0f;
	float wallTailPerSpeed = 0.1f;
	float wallTailMax = 1.4f;
	float wallTaper = 0.7f;
	float wallTaperPower = 1.2f;
	float wallTailLength = 2.2f;
	float wallWaveAmp = 0.12f;
	float wallWaveHz = 2.5f;
	float wallWaves = 1.2f;
	float wallCenterPull = 0.3f;

	// 仮足
	float podSpeed = 26.0f;
	float podSpacing = 0.022f;
	float podMaxShare = 0.45f;
	float podHz = 9.0f;
	float podZeta = 0.6f;
	float podRootRadius = 0.26f;
	float podRadius = 0.1f;
	float podFillet = 0.18f;
	float podWave = 0.06f;
	float podWaveHz = 7.0f;
	float zipStretch = 0.9f;

	// 歩くときだけ生える手足 (面にだけ足す形)
	float limbSpeedMin = 0.3f;
	float limbPuddleTime = 0.25f;
	float limbBrakeHold = 0.3f;
	float limbGrowHz = 6.0f;
	float limbGrowZeta = 0.32f;
	float limbRetractHz = 5.0f;
	float limbRetractZeta = 1.0f;
	float limbFillet = 0.08f;
	float legLift = 0.24f;
	float legSpacing = 0.46f;
	float legRootX = 0.2f;
	float legRootUp = 0.15f;
	float legRadius = 0.085f;
	float strideShare = 1.0f;
	float footHalfX = 0.11f;
	float footHalfY = 0.075f;
	float footHalfZ = 0.13f;
	float footForward = 0.03f;
	float toeShare = 1.0f;
	float armLength = 0.24f;
	float armRootRadius = 0.09f;
	float armTipRadius = 0.07f;
	float armInset = 0.04f;
	float armRootUp = 0.5f;
	float armRaiseExtra = 45.0f;
	float armFlail = 12.0f;
	int dashWheel = 0;              ///< 最高速の足。0 はたたむ (JSON の dashStyle "tuck")、1 は輪に回す ("wheel")
	float tuckSpeed = 10.0f;
	float tuckBlendSpeed = 4.0f;
	float tuckShare = 0.4f;
	float wheelSpeed = 10.0f;
	float wheelRadius = 0.2f;
	float wheelLift = 0.4f;
	float smearFps = 30.0f;

	// 面 (FluidSurface)
	float splatSigma = 0.095f;
	float surfaceRadius = 0.12f;
	float surfaceMinWeight = 0.15f;
	float surfaceBlur = 0.045f;
	float renderSmoothing = 0.85f;
	float renderSmoothRadius = 0.32f;
	float meshVoxelSize = 0.04f;

	// 顔
	float faceHz = 5.0f;
	float faceZeta = 0.8f;
	float faceAboveCentroid = 0.13f;
	float stickFaceTiltDeg = 70.0f;
};

} // namespace mitiru::softbody
