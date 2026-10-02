#pragma once

/// @file DX12TrailShaders.hpp
/// @brief 剣筋の帯 (TrailRibbon.hpp が作る三角形ストリップ) の HLSL
/// @details MSAA の HDR 色へ深度テストだけして描く (深度は書かない)。色は 1 を超えてよく、bloom が拾う。
///          加算は不透明度を色に掛けて ONE/ONE で足し、アルファは SRC_ALPHA/INV_SRC_ALPHA で重ねる。

namespace mitiru::render
{

inline constexpr const char* DX12_TRAIL_HLSL = R"hlsl(
cbuffer CbTrail : register(b0)
{
    float4x4 ViewProj;       // 主パスと同じずらし込みの射影
    float    EdgeSoftness;   // 帯の半幅に対する、縁をぼかす幅の割合
    float    Additive;       // 1 なら加算 (色に不透明度を掛けて出す)
    float2   _pad;
};

struct VSInput
{
    float3 Position : POSITION;
    float4 Color    : COLOR0;
    float2 Uv       : TEXCOORD0;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float4 Color    : COLOR0;
    float2 Uv       : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.Position = mul(ViewProj, float4(input.Position, 1.0));
    o.Color = input.Color;
    o.Uv = input.Uv;
    return o;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float centre = 1.0 - abs(input.Uv.y * 2.0 - 1.0);   // 帯の中央で 1、縁で 0
    float edge = (EdgeSoftness > 0.0) ? saturate(centre / EdgeSoftness) : 1.0;
    float a = saturate(input.Color.a * edge);
    return (Additive > 0.5) ? float4(input.Color.rgb * a, a) : float4(input.Color.rgb, a);
}
)hlsl";

} // namespace mitiru::render
