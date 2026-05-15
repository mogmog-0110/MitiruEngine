#pragma once

/// @file DX12MultiLightShaders.hpp
/// @brief DX12 用マルチライト Phong PS（MRT 互換）
/// @details DX11 の `MultiLightShaders3D.hpp` と機能等価だが、以下の差異がある:
///          - DX12 のメインパスは MRT (color RT0 + normal RT1) のため、
///            `PSOutput` を返して両 RT に書き込む
///          - Texture2D / SamplerState を参照しない（DX12 メインパスでは
///            albedo テクスチャを bind しないため）
///          - `CbLighting (b1)` の cameraPos / materialDiffuse / materialSpecular /
///            materialShininess を使う（DX11 版と CB レイアウトは同じ）
///          - `CbLightArray (b2)` で最大 8 ライトを評価する
///
///          シェーダーは `TOON_VS_3D` の `VSOutput` (= `PSInput`) と同形の入力を取る。
///          → メイン PSO と頂点シェーダーを共有できる。

namespace mitiru::render
{

/// @brief DX12 メインパス用マルチライト Phong PS（MRT 出力）
inline constexpr const char* DX12_MULTI_LIGHT_PS_3D = R"HLSL(
cbuffer CbLighting : register(b1)
{
    float3 _unusedLightDir;    float _pad0;
    float3 _unusedLightColor;  float _pad1;
    float3 _unusedAmbient_b1;  float _pad2;
    float3 CameraPos;          float _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float  MaterialShininess;
    float3 _pad4;
};

struct LightEntry
{
    float4 typeAndIntensity;
    float4 position;
    float4 direction;
    float4 color;
};

cbuffer CbLightArray : register(b2)
{
    int    lightCount;
    int3   _pad_la;
    float4 SceneAmbient;
    LightEntry Lights[8];
};

Texture2D    g_albedo : register(t0);
SamplerState g_samp   : register(s0);

struct PSInput
{
    float4 Position      : SV_POSITION;
    float3 WorldPos      : TEXCOORD0;
    float3 WorldNorm     : TEXCOORD1;
    float2 TexCoord      : TEXCOORD2;
    float4 LightSpacePos : TEXCOORD3;
    float4 Color         : COLOR0;
};

struct PSOutput
{
    float4 Color  : SV_TARGET0;
    float4 Normal : SV_TARGET1;
};

float3 evaluateLight(LightEntry L, float3 worldPos, float3 N, float3 V,
                     float3 albedo, float3 specularCol, float shininess)
{
    int   type      = (int)L.typeAndIntensity.x;
    float intensity = L.typeAndIntensity.y;
    float3 Lcol     = L.color.rgb * intensity;
    float range     = L.color.a;

    float3 Ldir;
    float  attenuation = 1.0;

    if (type == 0)
    {
        Ldir = normalize(-L.direction.xyz);
    }
    else
    {
        float3 toLight = L.position.xyz - worldPos;
        float dist = length(toLight);
        Ldir = toLight / max(dist, 1e-4);

        attenuation = saturate(1.0 - dist / max(range, 1e-4));
        attenuation *= attenuation;

        if (type == 2)
        {
            float3 spotDir = normalize(L.direction.xyz);
            float cosTheta = dot(-Ldir, spotDir);
            float inner = L.typeAndIntensity.z;
            float outer = L.typeAndIntensity.w;
            float spotFactor = saturate(
                (cosTheta - outer) / max(inner - outer, 1e-4));
            attenuation *= spotFactor;
        }
    }

    float NdotL = saturate(dot(N, Ldir));

    float3 H = normalize(Ldir + V);
    float specPow = max(shininess, 1.0);
    float NdotH = saturate(dot(N, H));
    float spec = pow(NdotH, specPow) * NdotL;

    float3 diffuse = albedo * NdotL;
    float3 specular = specularCol * spec;
    return (diffuse + specular) * Lcol * attenuation;
}

PSOutput PSMain(PSInput input)
{
    float3 N = normalize(input.WorldNorm);
    float3 V = normalize(CameraPos - input.WorldPos);

    float4 texSample = g_albedo.Sample(g_samp, input.TexCoord);
    float3 albedo = MaterialDiffuse.rgb * input.Color.rgb * texSample.rgb;
    float3 result = SceneAmbient.rgb * albedo;

    [unroll]
    for (int n = 0; n < 8; ++n)
    {
        if (n >= lightCount) break;
        result += evaluateLight(Lights[n], input.WorldPos, N, V,
                                albedo, MaterialSpecular.rgb,
                                MaterialShininess);
    }

    float alpha = MaterialDiffuse.a * input.Color.a * texSample.a;

    PSOutput o;
    o.Color = float4(result, alpha);
    float NdotV = max(dot(N, V), 0.0);
    o.Normal = float4(N * 0.5 + 0.5, NdotV);
    return o;
}
)HLSL";

} // namespace mitiru::render
