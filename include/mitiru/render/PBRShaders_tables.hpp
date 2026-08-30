#pragma once

/// @file PBRShaders_tables.hpp
/// @brief PBRシェーダーコレクション（HLSL SM5.0）。シェーダ文字列のデータ表 (_tables 規約で 800 行超を許容)
/// @details Cook-Torrance BRDF + IBL + 法線マッピング + シャドウマッピングの
///          完全なPBRパイプライン。Nadrin/PBR (MIT License) を参考に
///          MitiruEngineの頂点フォーマット・定数バッファレイアウトに適合させている。
///
/// シェーダー一覧:
/// - kPBR_VS:              PBR頂点シェーダー（TBN生成）
/// - kPBR_BRDF_COMMON:     BRDF関数群の共通HLSL定義（各PSから参照）
/// - kPBR_CBUFFER_COMMON:  CbPBR定数バッファ+PSInput構造体の共通定義
/// - kPBR_PS_BODY:         PBRピクセルシェーダー本体（Cook-Torrance + 多光源）
/// - kPBR_IBL_PS_BODY:     IBLピクセルシェーダー本体（Split-sum近似）
/// - kPBR_SHADOW_PS_BODY:  シャドウ付きPBRピクセルシェーダー本体（PCF 3x3）
/// - kIBL_IRRADIANCE_PS:   イラディアンスマップ畳み込み
/// - kIBL_PREFILTER_PS:    プリフィルター環境マップ
/// - kIBL_BRDF_LUT_PS:     BRDF LUT生成
///
/// CompilePBRShaders()内でkPBR_BRDF_COMMON + kPBR_CBUFFER_COMMON + *_BODYを
/// 結合してフルシェーダーソースを構成する。

#ifdef _WIN32

#include <cstring>
#include <string>
#include <string_view>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")

#include <mitiru/debug/Log.hpp>

namespace mitiru::render
{

// ─── PBR Vertex Shader ─────────────────────────────────────

/// @brief PBR頂点シェーダー
/// @details Position, Normal, TexCoord, Tangent入力からTBN行列を生成し
///          ワールド空間の位置・法線・接線・従接線をピクセルシェーダーに渡す。
constexpr std::string_view kPBR_VS = R"hlsl(
cbuffer CbPBR : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;

    float4 gCameraPos;
    float4 gLightDir;
    float4 gLightColor;

    float4 gAlbedo;
    float4 gMetallicRoughness;
    float4 gEmissive;

    int gHasAlbedoMap;
    int gHasNormalMap;
    int gHasMetallicRoughnessMap;
    int gHasAoMap;
};

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 Tangent  : TANGENT;
};

struct VSOutput
{
    float4 Position   : SV_POSITION;
    float3 WorldPos   : TEXCOORD0;
    float3 WorldNorm  : TEXCOORD1;
    float3 WorldTan   : TEXCOORD2;
    float3 WorldBitan : TEXCOORD3;
    float2 TexCoord   : TEXCOORD4;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;

    float4 worldPos = mul(float4(input.Position, 1.0), gWorld);
    output.WorldPos = worldPos.xyz;

    float4 viewPos = mul(worldPos, gView);
    output.Position = mul(viewPos, gProj);

    float3x3 worldNormalMat = (float3x3)gWorld;
    output.WorldNorm = normalize(mul(input.Normal, worldNormalMat));
    output.WorldTan  = normalize(mul(input.Tangent.xyz, worldNormalMat));

    // Tangent.w carries the bitangent sign (handedness)
    output.WorldBitan = cross(output.WorldNorm, output.WorldTan)
                      * input.Tangent.w;

    output.TexCoord = input.TexCoord;

    return output;
}
)hlsl";

// ─── PBR BRDF Helpers (shared HLSL) ────────────────────────

/// @brief BRDF関数群の共通HLSL定義
/// @details Cook-Torrance BRDFのコアとなるGGX NDF、Schlick Fresnel、
///          Smith GGX Geometry関数を定義する。各ピクセルシェーダーから参照される。
constexpr std::string_view kPBR_BRDF_COMMON = R"hlsl(
// ── Constants ──────────────────────────────────────────────
static const float PI      = 3.14159265359;
static const float INV_PI  = 0.31830988618;
static const float EPSILON = 0.00001;

// ── GGX / Trowbridge-Reitz Normal Distribution ────────────
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    denom = PI * denom * denom;
    return a2 / max(denom, EPSILON);
}

// ── Schlick-GGX Geometry (single direction) ───────────────
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// ── Smith's Geometry (combined) ───────────────────────────
float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness)
         * GeometrySchlickGGX(NdotL, roughness);
}

// ── Schlick-GGX for IBL (different k remapping) ──────────
float GeometrySchlickGGX_IBL(float NdotV, float roughness)
{
    float a = roughness;
    float k = (a * a) / 2.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith_IBL(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX_IBL(NdotV, roughness)
         * GeometrySchlickGGX_IBL(NdotL, roughness);
}

// ── Fresnel (Schlick approximation) ──────────────────────
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// ── Fresnel with roughness (for IBL ambient) ────────────
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    float3 maxRefl = max(float3(1.0 - roughness, 1.0 - roughness,
                                1.0 - roughness), F0);
    return F0 + (maxRefl - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// ── ACES Filmic Tone Mapping ─────────────────────────────
float3 ACESFilm(float3 x)
{
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// ── Normal mapping via TBN matrix ────────────────────────
float3 ApplyNormalMap(float3 sampledNormal, float3 T, float3 B, float3 N)
{
    float3 tangentNormal = sampledNormal * 2.0 - 1.0;
    float3x3 TBN = float3x3(normalize(T), normalize(B), normalize(N));
    return normalize(mul(tangentNormal, TBN));
}
)hlsl";

// ─── PBR Shared Constant Buffer (CbPBR) ────────────────────

/// @brief CbPBR定数バッファの共通HLSL定義
/// @details 各PBRピクセルシェーダーで共通のCbPBR宣言とPSInput構造体。
constexpr std::string_view kPBR_CBUFFER_COMMON = R"hlsl(
// ── Constant Buffers ─────────────────────────────────────
cbuffer CbPBR : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;

    float4 gCameraPos;
    float4 gLightDir;
    float4 gLightColor;    // rgb=color, a=intensity

    float4 gAlbedo;
    float4 gMetallicRoughness; // x=metallic, y=roughness, z=ao
    float4 gEmissive;

    int gHasAlbedoMap;
    int gHasNormalMap;
    int gHasMetallicRoughnessMap;
    int gHasAoMap;
};

// ── Structs ─────────────────────────────────────────────
struct PSInput
{
    float4 Position   : SV_POSITION;
    float3 WorldPos   : TEXCOORD0;
    float3 WorldNorm  : TEXCOORD1;
    float3 WorldTan   : TEXCOORD2;
    float3 WorldBitan : TEXCOORD3;
    float2 TexCoord   : TEXCOORD4;
};
)hlsl";

// ─── PBR Pixel Shader (Cook-Torrance + Multi-Light) ────────

/// @brief PBRピクセルシェーダー（多光源対応）
/// @details Cook-Torrance BRDF（GGX NDF + Schlick Fresnel + Smith GGX Geometry）
///          による物理ベースライティング。最大4ディレクショナル+4ポイントライト対応。
///          アルベド・法線・メタリック-ラフネス・AO・エミッシブマップをサポートする。
///          ACESフィルミックトーンマッピング + sRGBガンマ補正。
///          ランタイムでkPBR_BRDF_COMMON + kPBR_CBUFFER_COMMONと結合して使用する。
constexpr std::string_view kPBR_PS_BODY = R"hlsl(
// Extended multi-light buffer (optional, register b1)
cbuffer CbLights : register(b1)
{
    // Directional lights [0..3]
    float4 gDirLightDir[4];
    float4 gDirLightColor[4];   // rgb=color, a=intensity

    // Point lights [0..3]
    float4 gPointLightPos[4];
    float4 gPointLightColor[4]; // rgb=color, a=range

    int gNumDirLights;
    int gNumPointLights;
    int2 _lightPad;
};

// ── Textures & Samplers ─────────────────────────────────
Texture2D tAlbedo     : register(t0);
Texture2D tNormal     : register(t1);
Texture2D tMetalRough : register(t2);
Texture2D tAO         : register(t3);
Texture2D tEmissive   : register(t4);

SamplerState sSampler : register(s0);

// ── Lighting Calculation ────────────────────────────────

// Evaluate Cook-Torrance for a single directional light
float3 EvalDirectionalLight(float3 N, float3 V, float3 lightDir,
                            float3 lightColor, float lightIntensity,
                            float3 albedo, float metallic, float roughness,
                            float3 F0)
{
    float3 L = normalize(-lightDir);
    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);

    float  D = DistributionGGX(N, H, roughness);
    float  G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 numerator  = D * G * F;
    float  denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
    float3 specular = numerator / denominator;

    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);

    float3 radiance = lightColor * lightIntensity;
    return (kD * albedo * INV_PI + specular) * radiance * NdotL;
}

// Evaluate Cook-Torrance for a single point light
float3 EvalPointLight(float3 N, float3 V, float3 worldPos,
                      float3 lightPos, float3 lightColor,
                      float lightRange,
                      float3 albedo, float metallic, float roughness,
                      float3 F0)
{
    float3 toLight = lightPos - worldPos;
    float dist = length(toLight);
    float3 L = toLight / max(dist, EPSILON);
    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);

    // Distance attenuation (inverse square with range falloff)
    float attenuation = saturate(1.0 - (dist * dist)
                      / (lightRange * lightRange));
    attenuation *= attenuation;

    float  D = DistributionGGX(N, H, roughness);
    float  G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 numerator  = D * G * F;
    float  denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
    float3 specular = numerator / denominator;

    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);

    float3 radiance = lightColor * attenuation;
    return (kD * albedo * INV_PI + specular) * radiance * NdotL;
}

// ── Main ────────────────────────────────────────────────

float4 PSMain(PSInput input) : SV_TARGET
{
    // ── Sample material parameters ──
    float3 albedo = gAlbedo.rgb;
    if (gHasAlbedoMap)
        albedo = pow(tAlbedo.Sample(sSampler, input.TexCoord).rgb, 2.2);

    float metallic  = gMetallicRoughness.x;
    float roughness = gMetallicRoughness.y;
    float ao        = gMetallicRoughness.z;

    if (gHasMetallicRoughnessMap)
    {
        float4 mr = tMetalRough.Sample(sSampler, input.TexCoord);
        roughness = mr.g;   // glTF: green=roughness
        metallic  = mr.b;   // glTF: blue=metallic
    }

    if (gHasAoMap)
        ao = tAO.Sample(sSampler, input.TexCoord).r;

    float3 emissiveColor = gEmissive.rgb;
    // Sample emissive map if bound (check via metallic-roughness flag
    // repurpose not needed; emissive always sampled when texture is bound)
    float4 emissiveSample = tEmissive.Sample(sSampler, input.TexCoord);
    if (emissiveSample.a > 0.0)
        emissiveColor += emissiveSample.rgb;

    // ── Normal mapping ──
    float3 N = normalize(input.WorldNorm);
    if (gHasNormalMap)
    {
        float3 sampledNormal = tNormal.Sample(sSampler, input.TexCoord).rgb;
        float3 tangentNormal = sampledNormal * 2.0 - 1.0;
        float3 T = normalize(input.WorldTan);
        float3 B = normalize(input.WorldBitan);
        float3x3 TBN = float3x3(T, B, N);
        N = normalize(mul(tangentNormal, TBN));
    }

    float3 V = normalize(gCameraPos.xyz - input.WorldPos);

    // Dielectric F0 = 0.04, metals use albedo
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    // ── Accumulate lighting ──
    float3 Lo = float3(0.0, 0.0, 0.0);

    // Primary directional light (from CbPBR)
    Lo += EvalDirectionalLight(N, V, gLightDir.xyz, gLightColor.rgb,
                               gLightColor.a, albedo, metallic, roughness, F0);

    // Additional directional lights (from CbLights)
    int numDir = min(gNumDirLights, 4);
    for (int i = 0; i < numDir; ++i)
    {
        Lo += EvalDirectionalLight(N, V, gDirLightDir[i].xyz,
                                   gDirLightColor[i].rgb,
                                   gDirLightColor[i].a,
                                   albedo, metallic, roughness, F0);
    }

    // Point lights (from CbLights)
    int numPt = min(gNumPointLights, 4);
    for (int j = 0; j < numPt; ++j)
    {
        Lo += EvalPointLight(N, V, input.WorldPos,
                             gPointLightPos[j].xyz,
                             gPointLightColor[j].rgb,
                             gPointLightColor[j].a,
                             albedo, metallic, roughness, F0);
    }

    // ── Ambient (constant, no IBL) ──
    float3 ambient = float3(0.03, 0.03, 0.03) * albedo * ao;

    float3 color = ambient + Lo + emissiveColor;

    // ── ACES filmic tone mapping ──
    color = ACESFilm(color);

    // ── Gamma correction (linear -> sRGB) ──
    color = pow(color, 1.0 / 2.2);

    return float4(color, gAlbedo.a);
}
)hlsl";

// ─── IBL Pixel Shader (Split-Sum Approximation) ───────────

/// @brief IBL付きPBRピクセルシェーダー
/// @details Image-Based Lightingによるアンビエント照明を追加する。
///          イラディアンスキューブマップ（拡散）、プリフィルター環境マップ+BRDF LUT
///          （スペキュラー）を使用するSplit-sum近似。
///          環境マップが未設定の場合は定数アンビエントにフォールバックする。
///          ランタイムでkPBR_BRDF_COMMON + kPBR_CBUFFER_COMMONと結合して使用する。
constexpr std::string_view kPBR_IBL_PS_BODY = R"hlsl(
cbuffer CbIBL : register(b2)
{
    float gAmbientIntensity;
    float gMaxReflectionLod;
    int   gHasIBL;
    float _iblPad;
};

// ── Textures & Samplers ─────────────────────────────────
Texture2D tAlbedo          : register(t0);
Texture2D tNormal          : register(t1);
Texture2D tMetalRough      : register(t2);
Texture2D tAO              : register(t3);
Texture2D tEmissive        : register(t4);
TextureCube tIrradiance    : register(t5);
TextureCube tPrefiltered   : register(t6);
Texture2D tBRDFLut         : register(t7);

SamplerState sSampler      : register(s0);
SamplerState sClampSampler : register(s1);

float4 PSMain(PSInput input) : SV_TARGET
{
    // ── Sample material parameters ──
    float3 albedo = gAlbedo.rgb;
    if (gHasAlbedoMap)
        albedo = pow(tAlbedo.Sample(sSampler, input.TexCoord).rgb, 2.2);

    float metallic  = gMetallicRoughness.x;
    float roughness = gMetallicRoughness.y;
    float ao        = gMetallicRoughness.z;

    if (gHasMetallicRoughnessMap)
    {
        float4 mr = tMetalRough.Sample(sSampler, input.TexCoord);
        roughness = mr.g;
        metallic  = mr.b;
    }

    if (gHasAoMap)
        ao = tAO.Sample(sSampler, input.TexCoord).r;

    float3 emissiveColor = gEmissive.rgb;

    // ── Normal mapping ──
    float3 N = normalize(input.WorldNorm);
    if (gHasNormalMap)
    {
        float3 sampledNormal = tNormal.Sample(sSampler, input.TexCoord).rgb;
        float3 tangentNormal = sampledNormal * 2.0 - 1.0;
        float3 T = normalize(input.WorldTan);
        float3 B = normalize(input.WorldBitan);
        float3x3 TBN = float3x3(T, B, N);
        N = normalize(mul(tangentNormal, TBN));
    }

    float3 V = normalize(gCameraPos.xyz - input.WorldPos);
    float3 R = reflect(-V, N);
    float NdotV = max(dot(N, V), 0.0);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    // ── Direct lighting (primary directional) ──
    float3 L = normalize(-gLightDir.xyz);
    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);

    float  D = DistributionGGX(N, H, roughness);
    float  G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 numerator  = D * G * F;
    float  denominator = 4.0 * NdotV * NdotL + 0.0001;
    float3 specular = numerator / denominator;

    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);

    float3 radiance = gLightColor.rgb * gLightColor.a;
    float3 Lo = (kD * albedo * INV_PI + specular) * radiance * NdotL;

    // ── IBL Ambient ──
    float3 ambient;
    if (gHasIBL)
    {
        // Diffuse IBL: sample irradiance cubemap
        float3 kS_ibl = FresnelSchlickRoughness(NdotV, F0, roughness);
        float3 kD_ibl = (1.0 - kS_ibl) * (1.0 - metallic);
        float3 irradiance = tIrradiance.Sample(sSampler, N).rgb;
        float3 diffuseIBL = kD_ibl * irradiance * albedo;

        // Specular IBL: pre-filtered env map + BRDF LUT
        float mipLevel = roughness * gMaxReflectionLod;
        float3 prefilteredColor = tPrefiltered.SampleLevel(
            sSampler, R, mipLevel).rgb;
        float2 brdf = tBRDFLut.Sample(sClampSampler,
            float2(NdotV, roughness)).rg;
        float3 specularIBL = prefilteredColor
            * (kS_ibl * brdf.x + brdf.y);

        ambient = (diffuseIBL + specularIBL) * ao * gAmbientIntensity;
    }
    else
    {
        // Fallback: constant ambient
        ambient = float3(0.03, 0.03, 0.03) * albedo * ao;
    }

    float3 color = ambient + Lo + emissiveColor;

    // ── Tone mapping & gamma ──
    color = ACESFilm(color);
    color = pow(color, 1.0 / 2.2);

    return float4(color, gAlbedo.a);
}
)hlsl";

// ─── Shadow-Integrated PBR Pixel Shader ───────────────────

/// @brief シャドウ付きPBRピクセルシェーダー
/// @details PCF 3x3ソフトシャドウ + カスケードシャドウマップ選択を統合した
///          Cook-Torrance PBRピクセルシェーダー。
///          ランタイムでkPBR_BRDF_COMMON + kPBR_CBUFFER_COMMONと結合して使用する。
constexpr std::string_view kPBR_SHADOW_PS_BODY = R"hlsl(
cbuffer CbShadow : register(b3)
{
    float4x4 gLightViewProj[4]; // cascade matrices
    float4   gCascadeSplits;    // view-space depth splits
    float    gShadowMapSize;    // shadow map resolution
    float    gShadowBias;       // depth bias
    int      gNumCascades;
    float    _shadowPad;
};

Texture2D tAlbedo     : register(t0);
Texture2D tNormal     : register(t1);
Texture2D tMetalRough : register(t2);
Texture2D tAO         : register(t3);
Texture2D tEmissive   : register(t4);
Texture2DArray tShadowMap : register(t8);

SamplerState sSampler : register(s0);
SamplerComparisonState sShadowSampler : register(s2);

// ── Cascade Shadow Map Selection ────────────────────────
int SelectCascade(float viewDepth)
{
    for (int i = 0; i < gNumCascades - 1; ++i)
    {
        if (viewDepth < gCascadeSplits[i])
            return i;
    }
    return gNumCascades - 1;
}

// ── PCF 3x3 Shadow Sampling ────────────────────────────
float SampleShadowPCF(float3 worldPos, int cascadeIndex)
{
    float4 lightSpacePos = mul(float4(worldPos, 1.0),
                               gLightViewProj[cascadeIndex]);
    float3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    // [-1,1] -> [0,1] (flip Y for texture space)
    float2 shadowUV;
    shadowUV.x = projCoords.x * 0.5 + 0.5;
    shadowUV.y = -projCoords.y * 0.5 + 0.5;
    float currentDepth = projCoords.z - gShadowBias;

    // Outside shadow map -> fully lit
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 ||
        shadowUV.y < 0.0 || shadowUV.y > 1.0 ||
        currentDepth > 1.0)
        return 1.0;

    // PCF 3x3
    float shadow = 0.0;
    float texelSize = 1.0 / gShadowMapSize;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float2 offset = float2(x, y) * texelSize;
            shadow += tShadowMap.SampleCmpLevelZero(
                sShadowSampler,
                float3(shadowUV + offset, (float)cascadeIndex),
                currentDepth);
        }
    }
    return shadow / 9.0;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    // ── Material sampling ──
    float3 albedo = gAlbedo.rgb;
    if (gHasAlbedoMap)
        albedo = pow(tAlbedo.Sample(sSampler, input.TexCoord).rgb, 2.2);

    float metallic  = gMetallicRoughness.x;
    float roughness = gMetallicRoughness.y;
    float ao        = gMetallicRoughness.z;

    if (gHasMetallicRoughnessMap)
    {
        float4 mr = tMetalRough.Sample(sSampler, input.TexCoord);
        roughness = mr.g;
        metallic  = mr.b;
    }

    if (gHasAoMap)
        ao = tAO.Sample(sSampler, input.TexCoord).r;

    float3 emissiveColor = gEmissive.rgb;

    // ── Normal mapping ──
    float3 N = normalize(input.WorldNorm);
    if (gHasNormalMap)
    {
        float3 sampledNormal = tNormal.Sample(sSampler, input.TexCoord).rgb;
        float3 tangentNormal = sampledNormal * 2.0 - 1.0;
        float3 T = normalize(input.WorldTan);
        float3 B = normalize(input.WorldBitan);
        float3x3 TBN = float3x3(T, B, N);
        N = normalize(mul(tangentNormal, TBN));
    }

    float3 V = normalize(gCameraPos.xyz - input.WorldPos);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    // ── Shadow ──
    float4 viewPos = mul(float4(input.WorldPos, 1.0), gView);
    float viewDepth = viewPos.z;
    int cascade = SelectCascade(viewDepth);
    float shadow = SampleShadowPCF(input.WorldPos, cascade);

    // ── Direct lighting with shadow ──
    float3 L = normalize(-gLightDir.xyz);
    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);

    float  D = DistributionGGX(N, H, roughness);
    float  G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 numerator  = D * G * F;
    float  denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
    float3 spec = numerator / denominator;

    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);

    float3 radiance = gLightColor.rgb * gLightColor.a;
    float3 Lo = (kD * albedo * INV_PI + spec) * radiance * NdotL * shadow;

    // ── Ambient (unaffected by shadow) ──
    float3 ambient = float3(0.03, 0.03, 0.03) * albedo * ao;

    float3 color = ambient + Lo + emissiveColor;

    color = ACESFilm(color);
    color = pow(color, 1.0 / 2.2);

    return float4(color, gAlbedo.a);
}
)hlsl";

// ─── IBL Precomputation: Irradiance Convolution ───────────

/// @brief イラディアンスマップ畳み込みピクセルシェーダー
/// @details 環境キューブマップを半球上で畳み込み、拡散イラディアンスマップを生成する。
///          キューブマップの各面に対してフルスクリーンクワッドで実行する。
constexpr std::string_view kIBL_IRRADIANCE_PS = R"hlsl(
TextureCube tEnvironment : register(t0);
SamplerState sSampler    : register(s0);

cbuffer CbIrradiance : register(b0)
{
    float4x4 gFaceRotation; // maps [0,1]x[0,1] UV to world direction
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float3 WorldDir : TEXCOORD0;
};

static const float PI     = 3.14159265359;
static const float TWO_PI = 6.28318530718;

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 normal = normalize(input.WorldDir);

    // Build local coordinate frame around normal
    float3 up = abs(normal.y) < 0.999
              ? float3(0.0, 1.0, 0.0)
              : float3(1.0, 0.0, 0.0);
    float3 right   = normalize(cross(up, normal));
    float3 forward = cross(normal, right);

    float3 irradiance = float3(0.0, 0.0, 0.0);
    float sampleCount = 0.0;

    // Hemisphere integration with uniform sampling
    float sampleDelta = 0.025;
    for (float phi = 0.0; phi < TWO_PI; phi += sampleDelta)
    {
        for (float theta = 0.0; theta < PI * 0.5; theta += sampleDelta)
        {
            // Spherical to cartesian (tangent space)
            float sinTheta = sin(theta);
            float cosTheta = cos(theta);
            float3 tangentSample = float3(
                sinTheta * cos(phi),
                sinTheta * sin(phi),
                cosTheta);

            // Tangent to world
            float3 sampleDir = tangentSample.x * right
                             + tangentSample.y * forward
                             + tangentSample.z * normal;

            irradiance += tEnvironment.Sample(sSampler, sampleDir).rgb
                        * cosTheta * sinTheta;
            sampleCount += 1.0;
        }
    }

    irradiance = PI * irradiance / max(sampleCount, 1.0);

    return float4(irradiance, 1.0);
}
)hlsl";

// ─── IBL Precomputation: Pre-filtered Environment Map ─────

/// @brief プリフィルター環境マップピクセルシェーダー
/// @details ラフネスレベルに応じてGGX重要度サンプリングで環境マップをぼかす。
///          各ミップレベルが異なるラフネス値に対応する。
constexpr std::string_view kIBL_PREFILTER_PS = R"hlsl(
TextureCube tEnvironment : register(t0);
SamplerState sSampler    : register(s0);

cbuffer CbPrefilter : register(b0)
{
    float gRoughness;
    float gResolution;
    float2 _prefilterPad;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float3 WorldDir : TEXCOORD0;
};

static const float PI      = 3.14159265359;
static const float EPSILON = 0.00001;
static const uint  SAMPLE_COUNT = 1024u;

// GGX NDF for prefilter
float DistributionGGX_Prefilter(float NdotH, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    denom = PI * denom * denom;
    return a2 / max(denom, EPSILON);
}

// Radical inverse (Van der Corput sequence)
float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

// GGX importance sampling
float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    // Spherical to cartesian
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    // Tangent to world
    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent   = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);

    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 N = normalize(input.WorldDir);
    float3 R = N;
    float3 V = R;

    float totalWeight = 0.0;
    float3 prefilteredColor = float3(0.0, 0.0, 0.0);

    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H  = ImportanceSampleGGX(Xi, N, gRoughness);
        float3 L  = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0)
        {
            // Sample with mip bias to reduce aliasing
            float NdotH = max(dot(N, H), 0.0);
            float HdotV = max(dot(H, V), 0.0);
            float D = DistributionGGX_Prefilter(NdotH, gRoughness);
            float pdf = D * NdotH / (4.0 * HdotV) + EPSILON;

            float saTexel  = 4.0 * PI / (6.0 * gResolution * gResolution);
            float saSample = 1.0 / (float(SAMPLE_COUNT) * pdf + EPSILON);
            float mipLevel = (gRoughness == 0.0)
                           ? 0.0
                           : 0.5 * log2(saSample / saTexel);

            prefilteredColor += tEnvironment.SampleLevel(
                sSampler, L, mipLevel).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    prefilteredColor /= max(totalWeight, EPSILON);
    return float4(prefilteredColor, 1.0);
}
)hlsl";

// ─── IBL Precomputation: BRDF Integration LUT ──────────────

/// @brief BRDF積分LUT生成ピクセルシェーダー
/// @details NdotV（横軸）とroughness（縦軸）のグリッドに対して
///          Split-sum近似のスケール(R)とバイアス(G)を計算する。
///          出力はRG16Fの2Dテクスチャ。
constexpr std::string_view kIBL_BRDF_LUT_PS = R"hlsl(
struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

static const float PI      = 3.14159265359;
static const float EPSILON = 0.00001;
static const uint  SAMPLE_COUNT = 1024u;

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent   = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);

    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

float GeometrySchlickGGX_IBL(float NdotV, float roughness)
{
    float a = roughness;
    float k = (a * a) / 2.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith_IBL(float NdotV, float NdotL, float roughness)
{
    return GeometrySchlickGGX_IBL(NdotV, roughness)
         * GeometrySchlickGGX_IBL(NdotL, roughness);
}

float2 IntegrateBRDF(float NdotV, float roughness)
{
    float3 V;
    V.x = sqrt(1.0 - NdotV * NdotV);
    V.y = 0.0;
    V.z = NdotV;

    float A = 0.0;
    float B = 0.0;

    float3 N = float3(0.0, 0.0, 1.0);

    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H  = ImportanceSampleGGX(Xi, N, roughness);
        float3 L  = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);

        if (NdotL > 0.0)
        {
            float G     = GeometrySmith_IBL(NdotV, NdotL, roughness);
            float G_Vis = (G * VdotH) / (NdotH * NdotV);
            float Fc    = pow(1.0 - VdotH, 5.0);

            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }

    A /= float(SAMPLE_COUNT);
    B /= float(SAMPLE_COUNT);
    return float2(A, B);
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float2 result = IntegrateBRDF(
        max(input.TexCoord.x, EPSILON),
        input.TexCoord.y);
    return float4(result, 0.0, 1.0);
}
)hlsl";

// ─── Fullscreen Quad Vertex Shader (for IBL precomputation) ─

/// @brief フルスクリーンクワッド頂点シェーダー
/// @details IBLプリコンピュテーション用。頂点ID→クリップ空間+UV変換。
constexpr std::string_view kFULLSCREEN_QUAD_VS = R"hlsl(
struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput output;

    // Generate fullscreen triangle from vertex ID
    output.TexCoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(output.TexCoord * float2(2.0, -2.0)
                           + float2(-1.0, 1.0), 0.0, 1.0);

    return output;
}
)hlsl";

/// @brief キューブマップ面描画用頂点シェーダー
/// @details IBLプリコンピュテーションでキューブマップ各面にレンダリングする際に使用する。
///          頂点IDからクリップ空間座標とワールド方向を生成する。
constexpr std::string_view kCUBEMAP_FACE_VS = R"hlsl(
cbuffer CbCubeFace : register(b0)
{
    float4x4 gFaceRotation;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 WorldDir : TEXCOORD0;
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput output;

    // Fullscreen triangle
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0),
                             0.0, 1.0);

    // Map UV to [-1, 1] and rotate for cube face
    float3 dir = float3(uv * 2.0 - 1.0, 1.0);
    output.WorldDir = mul(float4(dir, 0.0), gFaceRotation).xyz;

    return output;
}
)hlsl";

// ─── Shader Compilation Helper ──────────────────────────────

/// @brief PBRシェーダーコンパイル結果を保持する構造体
struct PBRShaderSet
{
	Microsoft::WRL::ComPtr<ID3D11VertexShader> pbrVS;
	Microsoft::WRL::ComPtr<ID3D11PixelShader>  pbrPS;
	Microsoft::WRL::ComPtr<ID3D11PixelShader>  pbrIBL_PS;
	Microsoft::WRL::ComPtr<ID3D11PixelShader>  pbrShadowPS;
	Microsoft::WRL::ComPtr<ID3D11PixelShader>  iblIrradiancePS;
	Microsoft::WRL::ComPtr<ID3D11PixelShader>  iblPrefilterPS;
	Microsoft::WRL::ComPtr<ID3D11PixelShader>  iblBrdfLutPS;
	Microsoft::WRL::ComPtr<ID3D11VertexShader> fullscreenQuadVS;
	Microsoft::WRL::ComPtr<ID3D11VertexShader> cubemapFaceVS;
	Microsoft::WRL::ComPtr<ID3D11InputLayout>  pbrInputLayout;
	bool valid = false;
};

/// @brief HLSLソースをコンパイルしてBlobを返す内部ヘルパー
/// @param source HLSLソース文字列
/// @param entryPoint エントリーポイント名
/// @param target コンパイルターゲット（vs_5_0 / ps_5_0）
/// @param[out] blob コンパイル済みBlobの出力先
/// @return コンパイル成功ならtrue
inline bool CompilePBRShader(std::string_view source,
                             const char* entryPoint,
                             const char* target,
                             ID3DBlob** blob)
{
	UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
	flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
	HRESULT hr = D3DCompile(
		source.data(),
		source.size(),
		nullptr, nullptr, nullptr,
		entryPoint, target,
		flags, 0,
		blob,
		errorBlob.GetAddressOf());

	if (FAILED(hr))
	{
		if (errorBlob)
		{
			MITIRU_LOG_ERROR("PBRShaders",
				static_cast<const char*>(errorBlob->GetBufferPointer()));
		}
		return false;
	}
	return true;
}

/// @brief PBRシェーダーセットを一括コンパイルする
/// @param device D3D11デバイス
/// @return コンパイル済みPBRShaderSet（valid==trueで成功）
[[nodiscard]] inline PBRShaderSet CompilePBRShaders(ID3D11Device* device)
{
	PBRShaderSet result;

	if (!device)
	{
		MITIRU_LOG_ERROR("PBRShaders", "CompilePBRShaders: null device");
		return result;
	}

	// ── Vertex Shader ──
	Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
	if (!CompilePBRShader(kPBR_VS, "VSMain", "vs_5_0",
	                      vsBlob.GetAddressOf()))
	{
		MITIRU_LOG_ERROR("PBRShaders", "failed to compile PBR vertex shader");
		return result;
	}

	HRESULT hr = device->CreateVertexShader(
		vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
		nullptr, result.pbrVS.GetAddressOf());
	if (FAILED(hr))
	{
		MITIRU_LOG_ERROR("PBRShaders", "failed to create PBR vertex shader");
		return result;
	}

	// ── Input Layout (Position, Normal, TexCoord, Tangent) ──
	D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0,
		 D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
		 D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24,
		 D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32,
		 D3D11_INPUT_PER_VERTEX_DATA, 0},
	};

	hr = device->CreateInputLayout(
		layoutDesc, _countof(layoutDesc),
		vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
		result.pbrInputLayout.GetAddressOf());
	if (FAILED(hr))
	{
		MITIRU_LOG_ERROR("PBRShaders", "failed to create PBR input layout");
		return result;
	}

	// ── Pixel Shaders ──
	// Concatenate shared BRDF + cbuffer commons with shader-specific bodies
	const std::string pbrCommon = std::string(kPBR_BRDF_COMMON)
	                            + std::string(kPBR_CBUFFER_COMMON);

	const std::string kPBR_PS        = pbrCommon + std::string(kPBR_PS_BODY);
	const std::string kPBR_IBL_PS    = pbrCommon + std::string(kPBR_IBL_PS_BODY);
	const std::string kPBR_SHADOW_PS = pbrCommon + std::string(kPBR_SHADOW_PS_BODY);

	struct ShaderEntry
	{
		std::string_view source;
		const char* name;
		Microsoft::WRL::ComPtr<ID3D11PixelShader>* target;
	};

	ShaderEntry pixelShaders[] = {
		{kPBR_PS,           "PBR_PS",           &result.pbrPS},
		{kPBR_IBL_PS,       "PBR_IBL_PS",       &result.pbrIBL_PS},
		{kPBR_SHADOW_PS,    "PBR_SHADOW_PS",    &result.pbrShadowPS},
		{kIBL_IRRADIANCE_PS,"IBL_IRRADIANCE_PS",&result.iblIrradiancePS},
		{kIBL_PREFILTER_PS, "IBL_PREFILTER_PS", &result.iblPrefilterPS},
		{kIBL_BRDF_LUT_PS,  "IBL_BRDF_LUT_PS", &result.iblBrdfLutPS},
	};

	for (const auto& entry : pixelShaders)
	{
		Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
		if (!CompilePBRShader(entry.source, "PSMain", "ps_5_0",
		                      psBlob.GetAddressOf()))
		{
			MITIRU_LOG_ERROR("PBRShaders",
				(std::string("failed to compile ") + entry.name).c_str());
			return result;
		}

		hr = device->CreatePixelShader(
			psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
			nullptr, entry.target->GetAddressOf());
		if (FAILED(hr))
		{
			MITIRU_LOG_ERROR("PBRShaders",
				(std::string("failed to create ") + entry.name).c_str());
			return result;
		}
	}

	// ── Utility Vertex Shaders ──
	Microsoft::WRL::ComPtr<ID3DBlob> fsqBlob;
	if (CompilePBRShader(kFULLSCREEN_QUAD_VS, "VSMain", "vs_5_0",
	                     fsqBlob.GetAddressOf()))
	{
		device->CreateVertexShader(
			fsqBlob->GetBufferPointer(), fsqBlob->GetBufferSize(),
			nullptr, result.fullscreenQuadVS.GetAddressOf());
	}

	Microsoft::WRL::ComPtr<ID3DBlob> cfBlob;
	if (CompilePBRShader(kCUBEMAP_FACE_VS, "VSMain", "vs_5_0",
	                     cfBlob.GetAddressOf()))
	{
		device->CreateVertexShader(
			cfBlob->GetBufferPointer(), cfBlob->GetBufferSize(),
			nullptr, result.cubemapFaceVS.GetAddressOf());
	}

	result.valid = true;
	MITIRU_LOG_INFO("PBRShaders", "all PBR shaders compiled successfully");
	return result;
}

} // namespace mitiru::render

#endif // _WIN32
