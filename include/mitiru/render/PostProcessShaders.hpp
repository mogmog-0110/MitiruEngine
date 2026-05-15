#pragma once

/// @file PostProcessShaders.hpp
/// @brief ポストプロセス用HLSLシェーダーソース定数
/// @details PostProcess.hppから分離したHLSL文字列定数。
///          フルスクリーンVS、ブルーム、ガウシアンブラー、カラーグレーディング、
///          ビネット、色収差、フィルムグレイン、フェード、フロストグラスを含む。

#include <string_view>

namespace mitiru::render
{

// ============================================================================
// HLSL定数 — 全パスで共有するフルスクリーン三角形VS
// ============================================================================

/// @brief フルスクリーン三角形を描画する頂点シェーダー
/// @details 頂点バッファ不要。SV_VertexIDから座標を生成する。
constexpr std::string_view PP_FULLSCREEN_VS = R"hlsl(
struct VSOutput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
	VSOutput output;
	// 3頂点でスクリーン全体をカバーする三角形
	output.texCoord = float2((vertexId << 1) & 2, vertexId & 2);
	output.position = float4(
		output.texCoord.x * 2.0f - 1.0f,
		-(output.texCoord.y * 2.0f - 1.0f),
		0.0f, 1.0f);
	return output;
}
)hlsl";

// ============================================================================
// HLSL定数 — ブルームパス
// ============================================================================

/// @brief 輝度抽出ピクセルシェーダー
constexpr std::string_view PP_BLOOM_EXTRACT_PS = R"hlsl(
Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer BloomParams : register(b0)
{
	float threshold;
	float intensity;
	float2 padding;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
	float4 color = sceneTexture.Sample(linearSampler, input.texCoord);
	float luminance = dot(color.rgb, float3(0.299, 0.587, 0.114));
	float contrib = max(0.0, luminance - threshold);
	float factor = contrib / (contrib + 1.0);
	return float4(color.rgb * factor * intensity, 1.0);
}
)hlsl";

/// @brief ブルーム最終合成ピクセルシェーダー
constexpr std::string_view PP_BLOOM_COMBINE_PS = R"hlsl(
Texture2D sceneTexture : register(t0);
Texture2D bloomTexture : register(t1);
SamplerState linearSampler : register(s0);

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
	float4 scene = sceneTexture.Sample(linearSampler, input.texCoord);
	float4 bloom = bloomTexture.Sample(linearSampler, input.texCoord);
	return float4(scene.rgb + bloom.rgb, scene.a);
}
)hlsl";

// ============================================================================
// HLSL定数 — ガウシアンブラーパス
// ============================================================================

/// @brief 分離型ガウシアンブラーピクセルシェーダー
constexpr std::string_view PP_GAUSSIAN_BLUR_PS = R"hlsl(
Texture2D inputTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer BlurParams : register(b0)
{
	float2 texelDir;  // (1/w, 0) or (0, 1/h)
	int radius;
	float sigma;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float gaussWeight(int offset, float s)
{
	float x = float(offset);
	return exp(-(x * x) / (2.0 * s * s));
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float4 result = float4(0, 0, 0, 0);
	float totalWeight = 0.0;

	for (int i = -radius; i <= radius; ++i)
	{
		float w = gaussWeight(i, sigma);
		float2 offset = texelDir * float(i);
		result += inputTexture.Sample(linearSampler,
			input.texCoord + offset) * w;
		totalWeight += w;
	}

	return result / totalWeight;
}
)hlsl";

// ============================================================================
// HLSL定数 — カラーグレーディングパス
// ============================================================================

constexpr std::string_view PP_COLOR_GRADING_PS = R"hlsl(
Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer ColorGradingParams : register(b0)
{
	float brightness;
	float contrast;
	float saturation;
	float gamma;
	float4 tint;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
	float4 color = sceneTexture.Sample(linearSampler, input.texCoord);
	color.rgb = pow(max(color.rgb, 0.0), gamma);
	color.rgb = (color.rgb - 0.5) * contrast + 0.5;
	color.rgb *= brightness;
	float lum = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
	color.rgb = lerp(float3(lum, lum, lum), color.rgb, saturation);
	color.rgb *= tint.rgb;
	return float4(saturate(color.rgb), color.a);
}
)hlsl";

// ============================================================================
// HLSL定数 — ビネットパス
// ============================================================================

constexpr std::string_view PP_VIGNETTE_PS = R"hlsl(
Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer VignetteParams : register(b0)
{
	float vignetteIntensity;
	float vignetteRadius;
	float vignetteSoftness;
	float vignettePadding;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
	float4 color = sceneTexture.Sample(linearSampler, input.texCoord);
	float2 uv_centered = input.texCoord - 0.5;
	float dist = length(uv_centered);
	float vignette = smoothstep(vignetteRadius,
		vignetteRadius - vignetteSoftness, dist);
	color.rgb *= lerp(1.0 - vignetteIntensity, 1.0, vignette);
	return color;
}
)hlsl";

// ============================================================================
// HLSL定数 — 色収差パス
// ============================================================================

constexpr std::string_view PP_CHROMATIC_ABERRATION_PS = R"hlsl(
Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer ChromaticParams : register(b0)
{
	float caIntensity;
	float3 caPadding;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
	float2 dir = input.texCoord - 0.5;
	float dist = length(dir);
	float2 offset = dir * dist * caIntensity * 0.01;

	float r = sceneTexture.Sample(linearSampler,
		input.texCoord + offset).r;
	float g = sceneTexture.Sample(linearSampler,
		input.texCoord).g;
	float b = sceneTexture.Sample(linearSampler,
		input.texCoord - offset).b;
	float a = sceneTexture.Sample(linearSampler,
		input.texCoord).a;

	return float4(r, g, b, a);
}
)hlsl";

// ============================================================================
// HLSL定数 — フィルムグレインパス
// ============================================================================

constexpr std::string_view PP_FILM_GRAIN_PS = R"hlsl(
Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer FilmGrainParams : register(b0)
{
	float grainIntensity;
	float grainTime;
	float2 grainPadding;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
	float4 color = sceneTexture.Sample(linearSampler, input.texCoord);
	float noise = frac(sin(dot(input.texCoord * grainTime,
		float2(12.9898, 78.233))) * 43758.5453);
	color.rgb += (noise - 0.5) * grainIntensity;
	return float4(saturate(color.rgb), color.a);
}
)hlsl";

// ============================================================================
// HLSL定数 — フェードパス
// ============================================================================

constexpr std::string_view PP_FADE_PS = R"hlsl(
Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer FadeParams : register(b0)
{
	float4 fadeColor;
	float fadeProgress;
	float3 fadePadding;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
	float4 scene = sceneTexture.Sample(linearSampler, input.texCoord);
	return lerp(scene, fadeColor, fadeProgress);
}
)hlsl";

// ============================================================================
// HLSL定数 — フロストグラスパス
// ============================================================================

constexpr std::string_view PP_FROST_GLASS_PS = R"hlsl(
Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer FrostParams : register(b0)
{
	float frostBlurAmount;
	float3 frostTint;
	float frostTime;
	float3 frostPad;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
	float2 rnd = float2(
		frac(sin(dot(input.texCoord + frostTime * 0.1, float2(12.9898, 78.233))) * 43758.5453),
		frac(sin(dot(input.texCoord + frostTime * 0.1, float2(39.346, 11.135))) * 43758.5453)
	);
	float2 offset = (rnd - 0.5) * frostBlurAmount * 0.01;

	float3 blurred = sceneTexture.Sample(linearSampler,
		input.texCoord + offset).rgb;

	return float4(blurred * frostTint, 1.0);
}
)hlsl";

} // namespace mitiru::render
