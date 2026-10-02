#pragma once

/// @file DX12HitFeelShaders.hpp
/// @brief 当たった瞬間の画面の演出 (HitFeel.hpp) の全画面 PS。出力の大きさの LDR の写しを読んで書き戻す
/// @details 放射状のぼけは中心へ向かって 8 タップ、色ずれはそのぼけを赤と青で外と内へずらして 3 回引く。
///          白飛びは最後に flashColor へ寄せる。VS は outline post のフルスクリーン三角形を共用する。

namespace mitiru::render
{

inline constexpr const char* DX12_HIT_FEEL_PS = R"hlsl(
Texture2D<float4> g_scene  : register(t0);
SamplerState      s_linear : register(s0);

cbuffer CbHitFeel : register(b0)
{
    float4 FlashColor;   // rgb = 寄せる色 a = 割合
    float4 Params;       // x = 色ずれ y = 放射状のぼけ zw = 中心 (uv)
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

// 中心から uv までを、ぼけの強さ 1 で 1/4 だけ中心へ寄せた所まで 8 タップで均す。
// タップの位置を画素ごとの雑音 (interleaved gradient noise) でずらし、8 段の縞を細かい粒に変える
float3 sampleBlurred(float2 uv, float2 pixel)
{
    if (Params.y <= 0.0) { return g_scene.SampleLevel(s_linear, uv, 0).rgb; }
    float noise = frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
    float2 d = uv - Params.zw;
    float3 sum = 0.0;
    [unroll]
    for (int k = 0; k < 8; ++k)
    {
        float s = 1.0 - Params.y * 0.25 * ((float)k + noise) / 8.0;
        sum += g_scene.SampleLevel(s_linear, Params.zw + d * s, 0).rgb;
    }
    return sum / 8.0;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float2 uv = input.TexCoord;
    float3 c;
    if (Params.x > 0.0)
    {
        // 画面の端 (中心から高さの半分) で高さの Params.x 倍だけ、赤は外側・青は内側の画素を読む (絵では赤が中心へ寄る)
        float2 off = (uv - Params.zw) * (2.0 * Params.x);
        c = float3(sampleBlurred(uv + off, input.Position.xy).r, sampleBlurred(uv, input.Position.xy).g,
                   sampleBlurred(uv - off, input.Position.xy).b);
    }
    else
    {
        c = sampleBlurred(uv, input.Position.xy);
    }
    return float4(lerp(c, FlashColor.rgb, FlashColor.a), 1.0);
}
)hlsl";

} // namespace mitiru::render
