#pragma once

/// @file DX12ShaderModePS.hpp
/// @brief DX12 メインパス用の ShaderMode3D 別ピクセルシェーダー（MRT 出力）
/// @details DX12 のメインパスは MRT (RT0=color + RT1=worldNormal) なので、
///          DX11 用の `DefaultShaders3D.hpp` の単一 SV_TARGET PS をそのまま使えず、
///          バリアントごとに MRT 対応版を用意する。
///
///          全モード共通の頂点シェーダー: `TOON_VS_3D` (出力 VSOutput が同形)。
///          ライティングは `CbLighting (b1)` から material + cameraPos + 単一光源を読む。
///          マルチライト経路は `DX12MultiLightShaders.hpp` (b2=CbLightArray) に分離。

namespace mitiru::render
{

/// @brief MRT 用 Toon PS — 量子化 Lambert + リム + albedo テクスチャ
///        DX11 の TOON_PS_3D は単一 SV_TARGET。DX12 メインパスは MRT (color+normal)
///        + t0 albedo を扱うため、ここで DX12 専用の変種を用意する。
inline constexpr const char* DX12_TOON_PS_3D = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;    float _pad0;
    float3 LightColor;  float _pad1;
    float3 AmbientColor; float _pad2;
    float3 CameraPos;   float _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float  MaterialShininess;
    float3 _pad4;
};

Texture2D    g_albedo : register(t0);
SamplerState g_samp   : register(s0);

// LightSpacePos は VS が出力するため PSInput でも宣言してレジスタ整合を取る。
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

PSOutput PSMain(PSInput input)
{
    float3 N = normalize(input.WorldNorm);
    float3 L = normalize(-LightDir);
    float3 V = normalize(CameraPos - input.WorldPos);

    float4 texSample = g_albedo.Sample(g_samp, input.TexCoord);
    float3 albedo = MaterialDiffuse.rgb * input.Color.rgb * texSample.rgb;

    // アンビエント
    float3 ambient = AmbientColor * albedo;

    // ディフューズ — NdotL を 3 段階に量子化（toon 帯）
    float rawNdotL = saturate(dot(N, L));
    float toon = (rawNdotL > 0.5) ? 1.0 : (rawNdotL > 0.15) ? 0.6 : 0.3;
    float3 diffuse = LightColor * albedo * toon;

    // ハイライト
    float3 H = normalize(L + V);
    float NdotH = saturate(dot(N, H));
    float specFactor = pow(NdotH, max(MaterialShininess, 1.0)) * 0.3;
    float3 specular = LightColor * MaterialSpecular.rgb * specFactor;

    float alpha = MaterialDiffuse.a * input.Color.a * texSample.a;

    PSOutput o;
    o.Color = float4(ambient + diffuse + specular, alpha);
    float NdotV = saturate(dot(N, V));
    o.Normal = float4(N * 0.5 + 0.5, NdotV);
    return o;
}
)hlsl";

/// @brief MRT 用 Phong PS — 単一光源 Lambert + Phong + albedo テクスチャ + shadow PCF
inline constexpr const char* DX12_PHONG_PS_3D = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;    float _pad0;
    float3 LightColor;  float _pad1;
    float3 AmbientColor; float _pad2;
    float3 CameraPos;   float _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float  MaterialShininess;
    float3 _pad4;
};

Texture2D                g_albedo  : register(t0);
Texture2D                g_shadow  : register(t1);
SamplerState             g_samp    : register(s0);
SamplerComparisonState   g_pcf     : register(s1);

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

// 3x3 PCF (depth bias 込み)
float samplePCF(float3 ndc)
{
    // ndc: [-1,1] xy → UV [0,1], z → DX [0,1] そのまま
    float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    float depthRef = ndc.z - 0.001;  // shadow acne 抑制
    if (any(uv < 0) || any(uv > 1)) return 1.0;  // light frustum 外は影なし

    float shadow = 0.0;
    const float texelSize = 1.0 / 1024.0;  // map size に整合させること
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2(x, y) * texelSize;
            shadow += g_shadow.SampleCmpLevelZero(g_pcf, uv + offset, depthRef);
        }
    }
    return shadow / 9.0;
}

PSOutput PSMain(PSInput input)
{
    float3 N = normalize(input.WorldNorm);
    float3 L = normalize(-LightDir);
    float3 V = normalize(CameraPos - input.WorldPos);

    float4 texSample = g_albedo.Sample(g_samp, input.TexCoord);
    float3 albedo = MaterialDiffuse.rgb * input.Color.rgb * texSample.rgb;

    // shadow factor (1.0 = unshadowed)
    float3 lsNdc = input.LightSpacePos.xyz / max(input.LightSpacePos.w, 1e-4);
    float shadow = samplePCF(lsNdc);

    float3 ambient = AmbientColor * albedo;

    float NdotL = saturate(dot(N, L));
    float3 diffuse = LightColor * albedo * NdotL * shadow;

    float3 H = normalize(L + V);
    float NdotH = saturate(dot(N, H));
    float specPow = max(MaterialShininess, 1.0);
    float specFactor = pow(NdotH, specPow) * NdotL * shadow;
    float3 specular = LightColor * MaterialSpecular.rgb * specFactor;

    float alpha = MaterialDiffuse.a * input.Color.a * texSample.a;

    PSOutput o;
    o.Color  = float4(ambient + diffuse + specular, alpha);
    float NdotV = saturate(dot(N, V));
    o.Normal = float4(N * 0.5 + 0.5, NdotV);
    return o;
}
)hlsl";

/// @brief MRT 用 Unlit PS — 頂点色 × material diffuse × albedo テクスチャ
inline constexpr const char* DX12_UNLIT_PS_3D = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 _unusedLightDir;    float _pad0;
    float3 _unusedLightColor;  float _pad1;
    float3 _unusedAmbient;     float _pad2;
    float3 _unusedCameraPos;   float _pad3;
    float4 MaterialDiffuse;
    float4 _unusedSpecular;
    float  _unusedShininess;
    float3 _pad4;
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

PSOutput PSMain(PSInput input)
{
    float3 N = normalize(input.WorldNorm);
    float4 texSample = g_albedo.Sample(g_samp, input.TexCoord);
    float3 albedo = MaterialDiffuse.rgb * input.Color.rgb * texSample.rgb;
    float alpha = MaterialDiffuse.a * input.Color.a * texSample.a;

    PSOutput o;
    o.Color  = float4(albedo, alpha);
    o.Normal = float4(N * 0.5 + 0.5, 1.0);
    return o;
}
)hlsl";

/// @brief MRT 用 Flat PS — 面ごと一様陰影 + albedo テクスチャ
inline constexpr const char* DX12_FLAT_PS_3D = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;    float _pad0;
    float3 LightColor;  float _pad1;
    float3 AmbientColor; float _pad2;
    float3 CameraPos;   float _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float  MaterialShininess;
    float3 _pad4;
};

Texture2D    g_albedo : register(t0);
SamplerState g_samp   : register(s0);

struct PSInput
{
    float4 Position                  : SV_POSITION;
    float3 WorldPos                  : TEXCOORD0;
    nointerpolation float3 WorldNorm : TEXCOORD1;
    float2 TexCoord                  : TEXCOORD2;
    float4 LightSpacePos             : TEXCOORD3;
    float4 Color                     : COLOR0;
};

struct PSOutput
{
    float4 Color  : SV_TARGET0;
    float4 Normal : SV_TARGET1;
};

PSOutput PSMain(PSInput input)
{
    float3 N = normalize(input.WorldNorm);
    float3 L = normalize(-LightDir);
    float4 texSample = g_albedo.Sample(g_samp, input.TexCoord);
    float3 albedo = MaterialDiffuse.rgb * input.Color.rgb * texSample.rgb;

    float3 ambient = AmbientColor * albedo;
    float NdotL = saturate(dot(N, L));
    float3 diffuse = LightColor * albedo * NdotL;
    float alpha = MaterialDiffuse.a * input.Color.a * texSample.a;

    PSOutput o;
    o.Color  = float4(ambient + diffuse, alpha);
    o.Normal = float4(N * 0.5 + 0.5, NdotL);
    return o;
}
)hlsl";

} // namespace mitiru::render
