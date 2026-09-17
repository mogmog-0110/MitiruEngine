#pragma once

/// @file DX12PBRShaders.hpp
/// @brief PBR (Cook-Torrance + IBL) 用 HLSL シェーダーソース (DX12、B17)
/// @details `Renderer3D_DX12_PBR.hpp` から参照される。CbTransform (b0) は
///          Toon/Phong と同じレイアウト（`uploadTransformCB` を流用するため）。
///          CbLighting (b1) は `DX12_TOON_PS_3D` の宣言と同一レイアウト
///          （`uploadLightingCB` をそのまま流用するため、フィールド順・型を
///          変えてはいけない）。CbPBRExtra (b2) だけが PBR 固有の追加分。

namespace mitiru::render
{

/// @brief PBR 専用の追加定数バッファ（register b2, PS 専用）
/// @details CbTransform (b0) / CbLighting (b1) は Toon/Phong と同じ構造体・
///          同じ upload 関数 (uploadTransformCB/uploadLightingCB) をそのまま流用する。
///          metallic/roughness/ao と IBL 有効フラグだけがここに追加で要る。
struct alignas(256) DX12CbPBRExtra
{
	float metallicRoughnessAO[4]{0.0f, 1.0f, 1.0f, 0.0f};  ///< x=metallic y=roughness z=ao
	float ambientIntensityHasIBL[4]{1.0f, 0.0f, 0.0f, 0.0f};  ///< x=ambientIntensity y=hasIBL(0/1)
};

/// @brief プリフィルター鏡面キューブマップの mip 数。mip i の roughness = i / (数 - 1) で、
///        シェーダーは `SampleLevel(R, roughness * (数 - 1))` で粗さに応じた段を引く。
inline constexpr int kPbrPrefilterMipCount = 5;
/// @brief split-sum の環境 BRDF 表 (t2) の一辺。
inline constexpr int kPbrBrdfLutSize = 64;

/// @brief PBR 用頂点シェーダー。Toon 系の TOON_VS_3D と異なりシャドウ用
///        LightSpacePos を出力しない（PBR モードはシャドウ非対応、B17 スコープ外）。
inline constexpr const char* PBR_VS_3D = R"hlsl(
cbuffer CbTransform : register(b0)
{
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
};

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

struct VSOutput
{
    float4 Position  : SV_POSITION;
    float3 WorldPos  : TEXCOORD0;
    float3 WorldNorm : TEXCOORD1;
    float2 TexCoord  : TEXCOORD2;
    float4 Color     : COLOR0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    float4 worldPos = mul(World, float4(input.Position, 1.0));
    o.WorldPos = worldPos.xyz;
    float4 viewPos = mul(View, worldPos);
    o.Position = mul(Projection, viewPos);
    o.WorldNorm = normalize(mul((float3x3)World, input.Normal));
    o.TexCoord = input.TexCoord;
    o.Color = input.Color;
    return o;
}
)hlsl";

/// @brief PBR (Cook-Torrance, 単一ディレクショナルライト) + IBL アンビエントの
///        ピクセルシェーダー。
/// @details 鏡面 IBL は split-sum: roughness 別 mip 連鎖 (`kPbrPrefilterMipCount`) の prefiltered
///          cubemap × 環境 BRDF 表 (t2、`IblBrdfLut.hpp`)。normal map / albedo map は非対応
///          (Material.diffuse を albedo として使う、頂点色を乗算)。
inline constexpr const char* PBR_IBL_PS_3D = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;    float _pad0;
    float3 LightColor;  float _pad1;
    float3 AmbientColor; float _pad2;
    float3 CameraPos;   float _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float  MaterialShininess;
    float3 ShadowTint;
    float4 FogColor;
    float4 FogParams;
    float4 MaterialParams;
};

cbuffer CbPBRExtra : register(b2)
{
    float4 MetallicRoughnessAO;      // x=metallic y=roughness z=ao
    float4 AmbientIntensityHasIBL;   // x=ambientIntensity y=hasIBL(0/1)
};

TextureCube tIrradiance  : register(t0);
TextureCube tPrefiltered : register(t1);
Texture2D   tBrdfLut     : register(t2);
SamplerState sEnv        : register(s0);
static const float kPrefilterMaxMip = 4.0;

struct PSInput
{
    float4 Position  : SV_POSITION;
    float3 WorldPos  : TEXCOORD0;
    float3 WorldNorm : TEXCOORD1;
    float2 TexCoord  : TEXCOORD2;
    float4 Color     : COLOR0;
};

struct PSOutput
{
    float4 Color  : SV_TARGET0;
    float4 Normal : SV_TARGET1;
};

static const float PI = 3.14159265359;

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 1e-5);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

PSOutput PSMain(PSInput input)
{
    float3 N = normalize(input.WorldNorm);
    float3 V = normalize(CameraPos - input.WorldPos);
    float3 L = normalize(-LightDir);
    float3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);

    float3 albedo    = MaterialDiffuse.rgb * input.Color.rgb;
    float  metallic  = saturate(MetallicRoughnessAO.x);
    float  roughness = clamp(MetallicRoughnessAO.y, 0.04, 1.0);
    float  ao        = saturate(MetallicRoughnessAO.z);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    float3 kD = (1.0 - F) * (1.0 - metallic);
    float3 direct = (kD * albedo / PI + specular) * LightColor * NdotL;

    float3 ambient;
    if (AmbientIntensityHasIBL.y > 0.5)
    {
        // 粗い面ほどフレネルの立ち上がりが鈍る (Lagarde)。kD の配分にはこちらを使う
        float3 kS_ibl = F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0)
                        * pow(saturate(1.0 - NdotV), 5.0);
        float3 kD_ibl = (1.0 - kS_ibl) * (1.0 - metallic);
        float3 irradiance = tIrradiance.Sample(sEnv, N).rgb;
        float3 diffuseIBL = kD_ibl * irradiance * albedo;

        // split-sum: 粗さに応じた mip の prefiltered × 環境 BRDF 表 (A, B)
        float3 R = reflect(-V, N);
        float3 prefiltered = tPrefiltered.SampleLevel(sEnv, R, roughness * kPrefilterMaxMip).rgb;
        float2 envBrdf = tBrdfLut.Sample(sEnv, float2(NdotV, roughness)).rg;
        float3 specularIBL = prefiltered * (F0 * envBrdf.x + envBrdf.y);

        ambient = (diffuseIBL + specularIBL) * ao * AmbientIntensityHasIBL.x;
    }
    else
    {
        ambient = AmbientColor * albedo * ao;
    }

    // Toon/Phong と同じく、トーンマップ・ガンマ補正はしない (線形 HDR のまま MSAA
    // color へ書き、共有の resolve/tonemap パス (ACES + gamma) に任せる)
    float3 color = ambient + direct;

    PSOutput o;
    o.Color = float4(color, MaterialDiffuse.a * input.Color.a);
    o.Normal = float4(N * 0.5 + 0.5, NdotV);
    return o;
}
)hlsl";

} // namespace mitiru::render
