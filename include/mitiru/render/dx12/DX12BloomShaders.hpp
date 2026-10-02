#pragma once

/// @file DX12BloomShaders.hpp
/// @brief bloom の縮小 PS (しきい値付き) と戻し PS (tent 9 タップ + 加算) (v41、ADR 0043)
/// @details resolve 済みの HDR intermediate (t0) を 1/2 → 1/4 に縮小し、1/4 を tent で 1/2 に戻して
///          1/2 と足す。結果 (1/2 解像、R16G16B16A16_FLOAT) を tonemap PS が t2 で読み、露出の前に
///          HDR 色へ Strength 倍して足す。root sig / VS は tonemap のもの (SRV t0..t2 + CBV b0、
///          static sampler s0 = linear/clamp、フルスクリーン三角形) を共用する。

namespace mitiru::render
{

/// @brief bloom 共通の cbuffer と入力。縮小・戻しの両 PS が同じ並びで読む
inline constexpr const char* DX12_BLOOM_COMMON_HLSL = R"hlsl(
Texture2D<float4> g_src : register(t0);
Texture2D<float4> g_add : register(t1);
SamplerState      g_samp : register(s0);

cbuffer CbBloom : register(b0)
{
    float2 TexelSize;       // 1 / 読む側 (t0) のサイズ
    float  Threshold;       // ApplyThreshold のときだけ使う
    float  ApplyThreshold;  // 1 なら明部だけ残す (最初の縮小)
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};
)hlsl";

/// @brief 2x2 の線形サンプルを 4 つ (= 4x4 の箱) で縮小する。しきい値は各タップに掛けてから平均する
///        (平均してから切ると、明るい 1 px が周りに薄められて拾えない)
inline constexpr const char* DX12_BLOOM_DOWN_PS_BODY = R"hlsl(
float3 prefilter(float3 c)
{
    if (ApplyThreshold < 0.5) { return c; }
    float br = max(c.r, max(c.g, c.b));
    float w  = max(br - Threshold, 0.0) / max(br, 1e-4);
    return c * w;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float2 uv = input.TexCoord;
    float2 o  = TexelSize;
    float3 c = prefilter(g_src.Sample(g_samp, uv + float2(-o.x, -o.y)).rgb)
             + prefilter(g_src.Sample(g_samp, uv + float2( o.x, -o.y)).rgb)
             + prefilter(g_src.Sample(g_samp, uv + float2(-o.x,  o.y)).rgb)
             + prefilter(g_src.Sample(g_samp, uv + float2( o.x,  o.y)).rgb);
    return float4(c * 0.25, 1.0);
}
)hlsl";

/// @brief 低解像 (t0) を 3x3 tent で広げ、同解像の t1 を足す
inline constexpr const char* DX12_BLOOM_UP_PS_BODY = R"hlsl(
float4 PSMain(PSInput input) : SV_TARGET
{
    float2 uv = input.TexCoord;
    float2 o  = TexelSize;
    float3 c = g_src.Sample(g_samp, uv + float2(-o.x, -o.y)).rgb
             + g_src.Sample(g_samp, uv + float2( 0.0, -o.y)).rgb * 2.0
             + g_src.Sample(g_samp, uv + float2( o.x, -o.y)).rgb
             + g_src.Sample(g_samp, uv + float2(-o.x,  0.0)).rgb * 2.0
             + g_src.Sample(g_samp, uv).rgb * 4.0
             + g_src.Sample(g_samp, uv + float2( o.x,  0.0)).rgb * 2.0
             + g_src.Sample(g_samp, uv + float2(-o.x,  o.y)).rgb
             + g_src.Sample(g_samp, uv + float2( 0.0,  o.y)).rgb * 2.0
             + g_src.Sample(g_samp, uv + float2( o.x,  o.y)).rgb;
    return float4(c / 16.0 + g_add.Sample(g_samp, uv).rgb, 1.0);
}
)hlsl";

} // namespace mitiru::render
