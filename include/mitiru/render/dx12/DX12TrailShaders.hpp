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
    float    MaxReactive;    // PSReactive が書く反応の上限 (FSR 3.1 の間だけ使う)
    float    _pad;
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

// 頂点色は書いた sRGB (ColorSpace.hpp の linearRgb と同じ: 1 を超える色は 1 に収めて線形にし、倍率を戻す)
float3 srgbToLinear(float3 c)
{
    float m = max(max(c.r, c.g), max(c.b, 1.0));
    c /= m;
    return ((c <= 0.04045) ? c / 12.92 : pow((max(c, 0.04045) + 0.055) / 1.055, 2.4)) * m;
}
float4 srgbToLinear(float4 c) { return float4(srgbToLinear(c.rgb), c.a); }

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.Position = mul(ViewProj, float4(input.Position, 1.0));
    o.Color = srgbToLinear(input.Color);
    o.Uv = input.Uv;
    return o;
}

float coverage(VSOutput input)
{
    float centre = 1.0 - abs(input.Uv.y * 2.0 - 1.0);   // 帯の中央で 1、縁で 0
    float edge = (EdgeSoftness > 0.0) ? saturate(centre / EdgeSoftness) : 1.0;
    return saturate(input.Color.a * edge);
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float a = coverage(input);
    return (Additive > 0.5) ? float4(input.Color.rgb * a, a) : float4(input.Color.rgb, a);
}

// FSR 3.1 の反応マスク (R8) へ、帯の被覆率を MAX で重ねる
float4 PSReactive(VSOutput input) : SV_TARGET
{
    return min(coverage(input), MaxReactive).xxxx;
}
)hlsl";

} // namespace mitiru::render
