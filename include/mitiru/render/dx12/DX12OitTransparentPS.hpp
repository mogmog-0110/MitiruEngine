#pragma once

/// @file DX12OitTransparentPS.hpp
/// @brief 半透明メッシュ用 PS — Phong と同じシェーディングを Weighted-Blended OIT へ出力する。
/// @details main の Phong PS (DX12ShaderModePS.hpp) と同一の入力 (VSOutput) / CB (b1) /
///          ルートシグネチャを使い、最終出力だけを `o.Color` から WBOIT の
///          accum(SV_TARGET0) / reveal(SV_TARGET1) に差し替える。これにより透明メッシュも
///          不透明と同じライティング・影で陰影が付く。weight は WeightedBlendedOIT.hpp と一致。

namespace mitiru::render
{

inline constexpr const char* DX12_OIT_TRANSPARENT_PS_3D = R"hlsl(
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
    float4 FogColor;
    float4 FogParams;
    float4 MaterialParams;
};

Texture2D                g_albedo  : register(t0);
Texture2D                g_shadow  : register(t1);
SamplerState             g_samp    : register(s0);
SamplerState             g_sampPoint : register(s2);
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

struct PSOut { float4 accum : SV_TARGET0; float reveal : SV_TARGET1; };

float samplePCF(float3 ndc)
{
    float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    float depthRef = ndc.z - 0.001;
    // 光の錐台の外は影なし。奥行きも見る (遠方クリップ面の外は影マップに何も無い)
    if (any(uv < 0) || any(uv > 1) || ndc.z < 0.0 || ndc.z > 1.0) return 1.0;
    float shadow = 0.0;
    const float texelSize = 1.0 / 1024.0;
    [unroll] for (int y = -1; y <= 1; ++y)
    [unroll] for (int x = -1; x <= 1; ++x)
        shadow += g_shadow.SampleCmpLevelZero(g_pcf, uv + float2(x, y) * texelSize, depthRef);
    return shadow / 9.0;
}

PSOut PSMain(PSInput input)
{
    // ── 不透明 Phong と同一のシェーディング ──
    float3 N = normalize(input.WorldNorm);
    float3 L = normalize(-LightDir);
    float3 V = normalize(CameraPos - input.WorldPos);
    float4 texSample = (MaterialParams.y > 0.5)
        ? g_albedo.Sample(g_sampPoint, input.TexCoord)
        : g_albedo.Sample(g_samp, input.TexCoord);
    float3 albedo = MaterialDiffuse.rgb * input.Color.rgb * texSample.rgb;
    float3 lsNdc = input.LightSpacePos.xyz / max(input.LightSpacePos.w, 1e-4);
    float shadow = samplePCF(lsNdc);
    float3 ambient = AmbientColor * albedo;
    float NdotL = saturate(dot(N, L));
    float3 diffuse = LightColor * albedo * NdotL * shadow;
    float3 H = normalize(L + V);
    float specFactor = pow(saturate(dot(N, H)), max(MaterialShininess, 1.0)) * NdotL * shadow;
    float3 specular = LightColor * MaterialSpecular.rgb * specFactor;
    float3 shaded = ambient + diffuse + specular;
    float  alpha  = MaterialDiffuse.a * input.Color.a * texSample.a;

    // ── Weighted-Blended OIT 出力 (WeightedBlendedOIT.hpp の weight と一致) ──
    float z = input.Position.z;
    float w = clamp(pow(min(1.0, alpha * 10.0) + 0.01, 3.0)
                    * 1e3 * pow(1.0 - z * 0.9, 3.0), 1e-2, 3e3);
    PSOut o;
    o.accum  = float4(shaded * alpha, alpha) * w;
    o.reveal = alpha;
    return o;
}
)hlsl";

} // namespace mitiru::render
