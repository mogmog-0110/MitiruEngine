#pragma once

/// @file DX12AutoExposureShaders.hpp
/// @brief 自動露出 (ADR 0109) の測定 PS。resolve 済みの HDR を等間隔の格子で読み、log2 輝度の平均を 1 画素に書く
/// @details tonemap と同じ root sig (SRV t0..t2、CBV b0、静的サンプラー s0) を使うので、t0 の HDR だけを読む。
///          格子と頭打ちは AutoExposureMath.hpp の kAutoExposureGrid / kAutoExposureLumaMin / kAutoExposureLumaMax と同じ値。

namespace mitiru::render
{

inline constexpr const char* DX12_AUTO_EXPOSURE_PS = R"HLSL(
Texture2D<float4> g_hdr  : register(t0);
SamplerState      g_samp : register(s0);

cbuffer CbAutoExposure : register(b0)
{
    float4 _unused;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

static const int   kGrid    = 32;
static const float kLumaMin = 1.0 / 1024.0;
static const float kLumaMax = 64.0;

float PSMain(PSInput input) : SV_TARGET
{
    float sum = 0.0;
    [loop] for (int y = 0; y < kGrid; ++y)
    {
        [loop] for (int x = 0; x < kGrid; ++x)
        {
            float2 uv = (float2(x, y) + 0.5) / float(kGrid);
            float3 c = g_hdr.SampleLevel(g_samp, uv, 0).rgb;
            float l = dot(c, float3(0.2126, 0.7152, 0.0722));
            l = (l == l) ? l : 0.0;
            sum += log2(clamp(l, kLumaMin, kLumaMax));
        }
    }
    return sum / float(kGrid * kGrid);
}
)HLSL";

}  // namespace mitiru::render
