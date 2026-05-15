#pragma once

/// @file MultiLightShaders3D.hpp
/// @brief マルチライト対応 Phong 系 HLSL（b2 = CbLightArray を消費）
/// @details `DefaultShaders3D.hpp` の Phong PS を多光源対応にした変種。
///          b0 = CbTransform、b1 = CbLighting（material 部のみ使用、light 部は
///          無視）、b2 = CbLightArray（複数ライト）。
///
///          サポート種別: Directional / Point / Spot。
///          ライティングモデル: Lambert + Phong specular（既存 PS と同等）。
///          注: HDR は無し、tone-map は無し、エネルギー保存も近似のみ。
///          IBL や PBR が必要なら別シェーダーで対応する。

namespace mitiru::render
{

/// @brief マルチライト対応 Phong VS（DEFAULT_VS_3D と同形）
inline constexpr const char* MULTI_LIGHT_VS_3D = R"HLSL(
cbuffer CbTransform : register(b0)
{
    float4x4 world;
    float4x4 view;
    float4x4 projection;
};

struct VSIn
{
    float3 pos    : POSITION;
    float3 normal : NORMAL;
    float2 uv     : TEXCOORD0;
    float4 color  : COLOR0;
};

struct VSOut
{
    float4 svpos    : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 worldNrm : NORMAL;
    float2 uv       : TEXCOORD1;
    float4 color    : COLOR0;
};

VSOut VSMain(VSIn i)
{
    VSOut o;
    float4 wp = mul(float4(i.pos, 1.0), world);
    o.worldPos = wp.xyz;
    o.svpos = mul(mul(wp, view), projection);

    // 法線変換は world の 3x3 部分（無一様 scale 非対応の簡易版）。
    o.worldNrm = normalize(mul(float4(i.normal, 0.0), world).xyz);
    o.uv = i.uv;
    o.color = i.color;
    return o;
}
)HLSL";

/// @brief マルチライト Phong PS
inline constexpr const char* MULTI_LIGHT_PS_3D = R"HLSL(
struct LightEntry
{
    float4 typeAndIntensity; // x=type (0=Dir,1=Point,2=Spot), y=intensity, z=spotInnerCos, w=spotOuterCos
    float4 position;         // xyz=position
    float4 direction;        // xyz=direction
    float4 color;            // rgb=color, a=range
};

cbuffer CbLighting : register(b1)
{
    float4 _lightDir;        // 未使用（multi-light は b2 を使う）
    float4 _lightColor;      // 未使用
    float4 ambientColor_b1;  // 未使用（b2 の ambient を使う）
    float4 cameraPos;
    float4 materialDiffuse;
    float4 materialSpecular;
    float  materialShininess;
    float3 _pad_lighting;
};

cbuffer CbLightArray : register(b2)
{
    int    lightCount;
    int3   _pad_la;
    float4 sceneAmbient;
    LightEntry lights[8];
};

Texture2D    g_albedo : register(t0);
SamplerState g_samp   : register(s0);

struct VSOut
{
    float4 svpos    : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 worldNrm : NORMAL;
    float2 uv       : TEXCOORD1;
    float4 color    : COLOR0;
};

float3 evaluateLight(LightEntry L, float3 worldPos, float3 N, float3 V,
                     float3 albedo)
{
    int   type      = (int)L.typeAndIntensity.x;
    float intensity = L.typeAndIntensity.y;
    float3 Lcol     = L.color.rgb * intensity;
    float range     = L.color.a;

    float3 Ldir;
    float  attenuation = 1.0;

    if (type == 0)
    {
        // Directional: L からの光は -direction 方向から来る。
        Ldir = normalize(-L.direction.xyz);
    }
    else
    {
        // Point / Spot: 光源位置から worldPos へのベクトル。
        float3 toLight = L.position.xyz - worldPos;
        float dist = length(toLight);
        Ldir = toLight / max(dist, 1e-4);

        // smooth distance attenuation: 1 at center, 0 at range.
        attenuation = saturate(1.0 - dist / max(range, 1e-4));
        attenuation *= attenuation;

        if (type == 2)
        {
            // Spot: dot(Ldir, -direction) > outerCos で減衰開始。
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

    // Phong specular
    float3 H = normalize(Ldir + V);
    float specPow = max(materialShininess, 1.0);
    float NdotH = saturate(dot(N, H));
    float spec = pow(NdotH, specPow) * NdotL;

    float3 diffuse = albedo * NdotL;
    float3 specular = materialSpecular.rgb * spec;
    return (diffuse + specular) * Lcol * attenuation;
}

float4 PSMain(VSOut i) : SV_TARGET
{
    float3 N = normalize(i.worldNrm);
    float3 V = normalize(cameraPos.xyz - i.worldPos);

    float4 texCol = g_albedo.Sample(g_samp, i.uv);
    float3 albedo = materialDiffuse.rgb * i.color.rgb * texCol.rgb;

    float3 result = sceneAmbient.rgb * albedo;

    [unroll]
    for (int n = 0; n < 8; ++n)
    {
        if (n >= lightCount) break;
        result += evaluateLight(lights[n], i.worldPos, N, V, albedo);
    }

    float alpha = materialDiffuse.a * i.color.a * texCol.a;
    return float4(result, alpha);
}
)HLSL";

} // namespace mitiru::render
