#pragma once

/// @file AdvancedPostProcess_shaders_tables.hpp
/// @brief AdvancedPostProcess 用 HLSL シェーダーソース表（AdvancedPostProcess.hpp から機械的分割）

#ifdef _WIN32

#include <string_view>

namespace mitiru::render
{

// ============================================================================
// HLSL — SSAO ピクセルシェーダー
// ============================================================================

constexpr std::string_view PP_SSAO_PS = R"hlsl(
Texture2D depthTexture : register(t0);
Texture2D normalTexture : register(t1);
SamplerState pointSampler : register(s0);

cbuffer SSAOParams : register(b0)
{
	float4x4 projection;
	float4x4 invProjection;
	float4 samples[16];
	float2 noiseScale;
	float radius;
	float bias;
	float intensity;
	int kernelSize;
	float2 ssaoPad;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float3 reconstructViewPos(float2 uv) {
	float depth = depthTexture.Sample(pointSampler, uv).r;
	float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0); ndc.y = -ndc.y;
	float4 vp = mul(invProjection, ndc); return vp.xyz / vp.w;
}
float rand2d(float2 co) {
	return frac(sin(dot(co, float2(12.9898, 78.233))) * 43758.5453);
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float3 viewPos = reconstructViewPos(input.texCoord);
	float3 normal = normalize(
		normalTexture.Sample(pointSampler, input.texCoord).xyz * 2.0 - 1.0);

	float angle = rand2d(input.texCoord * noiseScale) * 6.283185;
	float3 randomVec = float3(cos(angle), sin(angle), 0.0);
	float3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
	float3x3 TBN = float3x3(tangent, cross(normal, tangent), normal);

	float occlusion = 0.0;
	for (int i = 0; i < kernelSize; ++i) {
		float3 samplePos = viewPos + mul(samples[i].xyz, TBN) * radius;
		float4 offset = mul(projection, float4(samplePos, 1.0));
		offset.xy = (offset.xy / offset.w) * 0.5 + 0.5;
		offset.y = 1.0 - offset.y;
		float sd = reconstructViewPos(offset.xy).z;
		float rc = smoothstep(0.0, 1.0, radius / abs(viewPos.z - sd));
		occlusion += (sd >= samplePos.z + bias ? 1.0 : 0.0) * rc;
	}
	occlusion = 1.0 - (occlusion / float(kernelSize)) * intensity;
	return float4(occlusion, occlusion, occlusion, 1.0);
}
)hlsl";

constexpr std::string_view PP_SSAO_BLUR_PS = R"hlsl(
Texture2D ssaoTexture : register(t0);
SamplerState pointSampler : register(s0);

cbuffer BlurParams : register(b0)
{
	float2 texelSize;
	float2 blurPad;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
	float result = 0.0;
	for (int x = -2; x <= 2; ++x)
		for (int y = -2; y <= 2; ++y)
			result += ssaoTexture.Sample(pointSampler,
				input.texCoord + float2(x, y) * texelSize).r;
	return float4((result / 25.0).xxx, 1.0);
}
)hlsl";

// ============================================================================
// HLSL — HDR Tone Mapping ピクセルシェーダー
// ============================================================================

constexpr std::string_view PP_HDR_TONEMAP_PS = R"hlsl(
Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer HDRParams : register(b0)
{
	float exposure;
	int tonemapOp;      // 0=Reinhard,1=ACES,2=Uncharted2,3=Exposure
	float whitePoint;
	float gamma;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float3 reinhardTonemap(float3 c, float wp) {
	return c * (1.0 + c / (wp * wp)) / (1.0 + c);
}
float3 acesFilmic(float3 x) {
	float a=2.51, b=0.03, c=2.43, d=0.59, e=0.14;
	return saturate((x*(a*x+b))/(x*(c*x+d)+e));
}
float3 uc2(float3 x) {
	float A=0.15,B=0.50,C=0.10,D=0.20,E=0.02,F=0.30;
	return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}
float3 uncharted2Tonemap(float3 c, float wp) {
	return uc2(c) / uc2(float3(wp,wp,wp));
}
float3 exposureTonemap(float3 c) { return 1.0 - exp(-c); }

float4 PSMain(PSInput input) : SV_TARGET
{
	float3 color = sceneTexture.Sample(linearSampler,
		input.texCoord).rgb * exposure;

	if (tonemapOp == 0)      color = reinhardTonemap(color, whitePoint);
	else if (tonemapOp == 1) color = acesFilmic(color);
	else if (tonemapOp == 2) color = uncharted2Tonemap(color, whitePoint);
	else                     color = exposureTonemap(color);

	// gamma correction
	color = pow(max(color, 0.0), 1.0 / gamma);
	return float4(color, 1.0);
}
)hlsl";

// ============================================================================
// HLSL — TAA ピクセルシェーダー
// ============================================================================

constexpr std::string_view PP_TAA_PS = R"hlsl(
Texture2D currentTexture : register(t0);
Texture2D historyTexture : register(t1);
Texture2D motionTexture : register(t2);
SamplerState linearSampler : register(s0);

cbuffer TAAParams : register(b0)
{
	float blendFactor;
	float2 rcpFrame;
	float taaPad;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
	float2 uv = input.texCoord;
	float2 motion = motionTexture.Sample(linearSampler, uv).rg;
	float3 current = currentTexture.Sample(linearSampler, uv).rgb;
	// neighborhood clamping (3x3 AABB)
	float3 nMin = 9999, nMax = -9999;
	for (int x = -1; x <= 1; ++x)
		for (int y = -1; y <= 1; ++y) {
			float3 s = currentTexture.Sample(linearSampler,
				uv + float2(x, y) * rcpFrame).rgb;
			nMin = min(nMin, s); nMax = max(nMax, s);
		}
	float3 history = historyTexture.Sample(linearSampler, uv - motion).rgb;
	history = clamp(history, nMin, nMax);
	return float4(lerp(history, current, blendFactor), 1.0);
}
)hlsl";

// ============================================================================
// HLSL — Depth of Field ピクセルシェーダー
// ============================================================================

constexpr std::string_view PP_DOF_PS = R"hlsl(
Texture2D sceneTexture : register(t0);
Texture2D depthTexture : register(t1);
SamplerState linearSampler : register(s0);

cbuffer DoFParams : register(b0)
{
	float focusDistance;
	float aperture;
	float focalLength;
	float maxBlur;
	float2 rcpFrame;
	float nearPlane;
	float farPlane;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float linearDepth(float d) {
	return nearPlane * farPlane / (farPlane - d * (farPlane - nearPlane));
}
float4 PSMain(PSInput input) : SV_TARGET
{
	float depth = depthTexture.Sample(linearSampler, input.texCoord).r;
	float linD = linearDepth(depth);
	float coc = clamp(abs(linD - focusDistance) * aperture / linD * focalLength,
		0.0, maxBlur);
	float3 color = 0; float tw = 0;
	int taps = clamp(int(coc * 4.0), 1, 8);
	for (int x = -taps; x <= taps; ++x)
		for (int y = -taps; y <= taps; ++y) {
			float dist = length(float2(x, y));
			if (dist > float(taps)) continue;
			float w = 1.0 / (1.0 + dist);
			color += sceneTexture.Sample(linearSampler,
				input.texCoord + float2(x,y) * rcpFrame * coc).rgb * w;
			tw += w;
		}
	return float4(color / max(tw, 0.001), 1.0);
}
)hlsl";

} // namespace mitiru::render

#endif // _WIN32
