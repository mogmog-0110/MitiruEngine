#pragma once

/// @file DX12ColorFinishShaders.hpp
/// @brief 色の仕上げ (LUT・ビネット・フィルムグレイン) の全画面 PS。式は ColorFinishMath.hpp と同じ並びで、
///        Dawn の shaders/dawn/colorfinish.slang とも揃える。VS は outline post のフルスクリーン三角形を共用する

namespace mitiru::render
{

inline constexpr const char* DX12_COLOR_FINISH_PS = R"hlsl(
Texture2D<float4> g_scene : register(t0);
Texture2D<float4> g_lut   : register(t1);

cbuffer CbColorFinish : register(b0)
{
    float LutSize;
    float LutStrength;
    float VignetteStrength;
    float VignetteRadius;
    float GrainStrength;
    uint  GrainCell;
    uint  Seed;
    float _pad0;
    float2 OutSize;
    float2 _pad1;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

uint grainHash(uint x, uint y, uint seed)
{
    uint h = (x * 0x8da6b343u) ^ (y * 0xd8163841u) ^ (seed * 0xcb1ab31fu);
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

float3 lutTexel(int n, int r, int g, int b)
{
    return g_lut.Load(int3(b * n + r, g, 0)).rgb;
}

float3 sampleLut(float3 c)
{
    int n = (int)LutSize;
    float3 s = saturate(c) * (LutSize - 1.0);
    float3 fl = floor(s);
    int3 lo = (int3)fl;
    int3 hi = min(lo + 1, n - 1);
    float3 f = s - fl;
    float3 sum = 0.0;
    [unroll]
    for (int corner = 0; corner < 8; ++corner)
    {
        bool hr = (corner & 1) != 0;
        bool hg = (corner & 2) != 0;
        bool hb = (corner & 4) != 0;
        float w = (hr ? f.x : 1.0 - f.x) * (hg ? f.y : 1.0 - f.y) * (hb ? f.z : 1.0 - f.z);
        sum += lutTexel(n, hr ? hi.x : lo.x, hg ? hi.y : lo.y, hb ? hi.z : lo.z) * w;
    }
    return sum;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    uint2 px = uint2(input.Position.xy);
    float4 src = g_scene.Load(int3(px, 0));
    float3 c = src.rgb;
    if (LutSize >= 2.0)
    {
        c += (sampleLut(c) - c) * LutStrength;
    }
    if (VignetteStrength > 0.0)
    {
        float2 halfSize = OutSize * 0.5;
        float2 d = float2(px) + 0.5 - halfSize;
        float r = sqrt(dot(d, d)) / sqrt(dot(halfSize, halfSize));
        c *= 1.0 - VignetteStrength * smoothstep(VignetteRadius, 1.0, r);
    }
    if (GrainStrength > 0.0)
    {
        float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
        float midtone = 0.5 + 0.5 * (4.0 * luma * (1.0 - luma));
        uint cell = max(GrainCell, 1u);
        float noise = float(grainHash(px.x / cell, px.y / cell, Seed) >> 8) * (2.0 / 16777216.0) - 1.0;
        c += noise * GrainStrength * midtone;
    }
    return float4(saturate(c), src.a);
}
)hlsl";

}  // namespace mitiru::render
