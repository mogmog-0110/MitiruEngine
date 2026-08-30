#pragma once

/// @file DX12Tonemap.hpp
/// @brief HDR FP16 → LDR R8G8B8A8 tonemap シェーダ (ACES filmic)
/// @details DX12 メインパスは ENG-105 v2 で MSAA 4x になり、ENG-106 で
///          R16G16B16A16_FLOAT (HDR FP16) の MSAA color RT に書き込むよう
///          になった。Resolve 後の single-sample HDR を backbuffer
///          (R8G8B8A8_UNORM) に焼き付ける際にこの tonemap PS を通す。
///
///          tonemap は ACES filmic curve (Krzysztof Narkowicz の近似式) を
///          使用。HDR 値 (>1.0) を 0..1 に圧縮しつつ、ハイライトとシャドウの
///          コントラストを維持する。
///
///          フローは:
///            scene (HDR FP16 MSAA) → ResolveSubresource → HDR intermediate
///              → applyTonemap (this) → backbuffer LDR
///              → outline post-process
///              → FXAA
///              → overlay 2D HUD

namespace mitiru::render
{

/// @brief Tonemap 用 VS。フルスクリーン三角形 (OUTLINE_POST_VS と同形)
constexpr const char* DX12_TONEMAP_VS = R"HLSL(
struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput output;
    output.TexCoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(output.TexCoord * float2(2, -2) + float2(-1, 1), 0, 1);
    return output;
}
)HLSL";

/// @brief Tonemap 用 PS。ACES filmic + Exposure + Gamma 2.2
/// @details exposure と gamma は CbTonemap (b0) で external から指定可。
constexpr const char* DX12_TONEMAP_PS = R"HLSL(
Texture2D<float4> g_hdr  : register(t0);
SamplerState      g_samp : register(s0);

cbuffer CbTonemap : register(b0)
{
    float Exposure;   // EV stops を線形係数に変換した値 (default 1.0)
    float Gamma;      // 出力ガンマ (default 2.2)
    float2 _pad0;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

// ACES filmic (Krzysztof Narkowicz 近似)
// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
float3 acesFilmic(float3 x)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 hdr = g_hdr.Sample(g_samp, input.TexCoord);

    float3 exposed = hdr.rgb * Exposure;
    float3 mapped  = acesFilmic(exposed);

    // Gamma encode (linear → sRGB approximation)
    float invG = 1.0f / max(Gamma, 1e-4f);
    float3 outRGB = pow(max(mapped, 0.0f), invG.xxx);

    return float4(outRGB, hdr.a);
}
)HLSL";

/// @brief Tonemap CB (b0) レイアウト。HLSL 側と一致させる
struct alignas(16) TonemapCB
{
    float exposure = 1.0f;
    float gamma    = 2.2f;
    float _pad0    = 0.0f;
    float _pad1    = 0.0f;
};

static_assert(sizeof(TonemapCB) == 16,
    "TonemapCB byte size mismatch — HLSL CB layout will break");

} // namespace mitiru::render
