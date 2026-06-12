#pragma once

/// @file SSAOEffect_shaders_tables.hpp
/// @brief SSAOEffect 用 HLSL シェーダーソース表（SSAOEffect.hpp から機械的分割）

#ifdef _WIN32

#include <string_view>

namespace mitiru::render
{

// ============================================================================
// HLSL定数 — SSAOピクセルシェーダー
// ============================================================================

/// @brief SSAO計算ピクセルシェーダー
/// @details 深度バッファから半球サンプリングでAO値を算出する。
///          16個のランダムカーネルとノイズテクスチャによるランダム回転を使用。
constexpr std::string_view PP_SSAO_PS = R"hlsl(
Texture2D depthTexture : register(t0);
Texture2D noiseTexture : register(t1);
SamplerState pointClampSampler : register(s0);
SamplerState noiseWrapSampler : register(s1);

cbuffer SSAOParams : register(b0)
{
	float4x4 projection;
	float4x4 invProjection;
	float4 samples[16];
	float2 noiseScale;
	float radius;
	float bias;
	float intensity;
	float farPlane;
	float nearPlane;
	float pad0;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

// ── 深度値からビュー空間Z座標を復元する ──────────────────
float linearizeDepth(float d)
{
	return (nearPlane * farPlane)
		/ (farPlane - d * (farPlane - nearPlane));
}

// ── UV+深度からビュー空間位置を復元する ─────────────────
float3 reconstructViewPos(float2 uv, float depth)
{
	float z = linearizeDepth(depth);
	float2 ndc = uv * 2.0 - 1.0;
	ndc.y = -ndc.y;
	float4 clipPos = float4(ndc, depth, 1.0);
	float4 viewPos = mul(invProjection, clipPos);
	viewPos /= viewPos.w;
	return viewPos.xyz;
}

// ── 深度バッファから近似法線を求める ────────────────────
float3 estimateNormal(float2 uv, float2 texelSize)
{
	float depthC = depthTexture.Sample(pointClampSampler, uv).r;
	float depthR = depthTexture.Sample(pointClampSampler,
		uv + float2(texelSize.x, 0)).r;
	float depthU = depthTexture.Sample(pointClampSampler,
		uv + float2(0, -texelSize.y)).r;

	float3 posC = reconstructViewPos(uv, depthC);
	float3 posR = reconstructViewPos(
		uv + float2(texelSize.x, 0), depthR);
	float3 posU = reconstructViewPos(
		uv + float2(0, -texelSize.y), depthU);

	return normalize(cross(posR - posC, posU - posC));
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float2 uv = input.texCoord;
	float2 texelSize = float2(
		1.0 / (noiseScale.x * 4.0),
		1.0 / (noiseScale.y * 4.0));

	float depthVal = depthTexture.Sample(pointClampSampler, uv).r;

	// 遠方ピクセルはAO計算をスキップする
	if (depthVal >= 1.0)
	{
		return float4(1, 1, 1, 1);
	}

	float3 fragPos = reconstructViewPos(uv, depthVal);
	float3 normal = estimateNormal(uv, texelSize);

	// ノイズテクスチャからランダム回転ベクトルを取得する
	float3 randomVec = normalize(
		noiseTexture.Sample(noiseWrapSampler,
			uv * noiseScale).xyz * 2.0 - 1.0);

	// TBN行列を構築する（Gram-Schmidt正規直交化）
	float3 tangent = normalize(randomVec - normal
		* dot(randomVec, normal));
	float3 bitangent = cross(normal, tangent);
	float3x3 TBN = float3x3(tangent, bitangent, normal);

	// 半球サンプリングでオクルージョンを計算する
	float occlusion = 0.0;

	[unroll]
	for (int i = 0; i < 16; i++)
	{
		// サンプル位置をビュー空間に変換する
		float3 sampleDir = mul(samples[i].xyz, TBN);
		float3 samplePos = fragPos + sampleDir * radius;

		// サンプル位置をクリップ空間に射影する
		float4 offset = mul(projection, float4(samplePos, 1.0));
		offset.xy /= offset.w;
		offset.xy = offset.xy * 0.5 + 0.5;
		offset.y = 1.0 - offset.y;

		// クリップ座標が画面外ならスキップする
		if (offset.x < 0 || offset.x > 1
			|| offset.y < 0 || offset.y > 1)
		{
			continue;
		}

		// サンプル位置の実際の深度を取得する
		float sampleDepth = depthTexture.Sample(
			pointClampSampler, offset.xy).r;
		float sampleZ = linearizeDepth(sampleDepth);

		// 距離による減衰（レンジチェック）
		float rangeCheck = smoothstep(0.0, 1.0,
			radius / abs(fragPos.z - sampleZ));

		// サンプル位置が現在の深度より手前ならオクルージョンに寄与する
		occlusion += (sampleZ <= samplePos.z - bias
			? 1.0 : 0.0) * rangeCheck;
	}

	occlusion = 1.0 - (occlusion / 16.0) * intensity;
	return float4(occlusion, occlusion, occlusion, 1.0);
}
)hlsl";

// ============================================================================
// HLSL定数 — SSAOブラーピクセルシェーダー
// ============================================================================

/// @brief SSAO結果のブラーピクセルシェーダー
/// @details 4x4のボックスブラーでSSAOノイズを平滑化する。
constexpr std::string_view PP_SSAO_BLUR_PS = R"hlsl(
Texture2D ssaoTexture : register(t0);
SamplerState pointClampSampler : register(s0);

cbuffer SSAOBlurParams : register(b0)
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

	[unroll]
	for (int x = -2; x < 2; x++)
	{
		[unroll]
		for (int y = -2; y < 2; y++)
		{
			float2 offset = float2(float(x), float(y))
				* texelSize;
			result += ssaoTexture.Sample(pointClampSampler,
				input.texCoord + offset).r;
		}
	}

	result /= 16.0;
	return float4(result, result, result, 1.0);
}
)hlsl";

/// @brief SSAO合成ピクセルシェーダー（シーン色 * AO）
constexpr std::string_view PP_SSAO_COMPOSITE_PS = R"hlsl(
Texture2D sceneTexture : register(t0);
Texture2D aoTexture : register(t1);
SamplerState linearSampler : register(s0);

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
	float4 scene = sceneTexture.Sample(linearSampler,
		input.texCoord);
	float ao = aoTexture.Sample(linearSampler,
		input.texCoord).r;
	return float4(scene.rgb * ao, scene.a);
}
)hlsl";

} // namespace mitiru::render

#endif // _WIN32
