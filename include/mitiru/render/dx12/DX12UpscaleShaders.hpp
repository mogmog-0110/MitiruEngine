#pragma once

/// @file DX12UpscaleShaders.hpp
/// @brief 内部解像度の絵を TAA の履歴で出力解像度へ戻す (TAAU) と、仕上げの鮮鋭化の HLSL
/// @details 内部の画素 i の標本は、ずらしを抜いた画面で i + 0.5 - ずらし (画素、x 右 y 下) の位置にある。
///          出力の画素の中心に近い標本ほど重く混ぜ (Blackman-Harris の近似の exp(-2.29 d^2)、d は出力の画素)、近い標本が無い
///          画素は履歴に任せる。履歴の切り詰めと動きベクトルの選び方は DX12_TAA_PS と同じ。
///          root sig は temporal のもの (SRV t0..t3 + CBV b0、s0 = 線形、s1 = 最近傍)。

namespace mitiru::render
{

inline constexpr const char* DX12_TAAU_PS = R"hlsl(
Texture2D<float4>     g_color    : register(t0);   // 内部解像度の LDR
Texture2D<float4>     g_history  : register(t1);   // 出力解像度
Texture2D<float2>     g_velocity : register(t2);   // 内部解像度 (UV、前 − 今)
Texture2DMS<float, 4> g_depth    : register(t3);   // 内部解像度
SamplerState          s_linear   : register(s0);

cbuffer CbTaau : register(b0)
{
    float2 InputSize;
    float2 OutputSize;
    float2 OutputTexel;
    float2 JitterPx;      // 内部の画素単位のずらし (x 右、y 下)
    float  BlendStill;
    float  BlendMoving;
    float  MovingPixels;  // 出力の画素で数える
    float  HistoryValid;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float3 toYCoCg(float3 c)
{
    return float3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

float3 fromYCoCg(float3 c)
{
    return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

float3 sampleHistory(float2 uv)
{
    float2 samplePos = uv * OutputSize;
    float2 tc1 = floor(samplePos - 0.5) + 0.5;
    float2 f   = samplePos - tc1;
    float2 w0  = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1  = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2  = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3  = f * f * (-0.5 + 0.5 * f);
    float2 w12 = w1 + w2;
    float2 tc0  = (tc1 - 1.0) * OutputTexel;
    float2 tc3  = (tc1 + 2.0) * OutputTexel;
    float2 tc12 = (tc1 + w2 / w12) * OutputTexel;
    float3 r = g_history.SampleLevel(s_linear, float2(tc12.x, tc0.y), 0).rgb * (w12.x * w0.y)
             + g_history.SampleLevel(s_linear, float2(tc0.x, tc12.y), 0).rgb * (w0.x * w12.y)
             + g_history.SampleLevel(s_linear, tc12, 0).rgb * (w12.x * w12.y)
             + g_history.SampleLevel(s_linear, float2(tc3.x, tc12.y), 0).rgb * (w3.x * w12.y)
             + g_history.SampleLevel(s_linear, float2(tc12.x, tc3.y), 0).rgb * (w12.x * w3.y);
    float wsum = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max(r / wsum, 0.0);
}

float3 clipToBox(float3 boxMin, float3 boxMax, float3 h)
{
    float3 c = 0.5 * (boxMax + boxMin);
    float3 e = 0.5 * (boxMax - boxMin) + 1e-4;
    float3 v = h - c;
    float3 a = abs(v / e);
    float  m = max(a.x, max(a.y, a.z));
    return (m > 1.0) ? c + v / m : h;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float2 uv = input.Position.xy * OutputTexel;
    float2 inPos = uv * InputSize;
    // 近傍の 3x3 はずらしに関係なく同じ画素を取る。相ごとに近傍が入れ替わると履歴の箱が揺れてちらつく
    int2 base = int2(floor(inPos));
    int2 hi = int2(InputSize) - 1;

    float3 sum = 0.0;
    float  wsum = 0.0;
    float  wmax = 0.0;
    float3 m1 = 0.0;
    float3 m2 = 0.0;
    float3 cmin = 1e9;
    float3 cmax = -1e9;
    float  closest = 2.0;
    int2   closestPix = clamp(base, int2(0, 0), hi);
    float  alpha = g_color.Load(int3(closestPix, 0)).a;
    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            int2   q = clamp(base + int2(dx, dy), int2(0, 0), hi);
            float3 c = toYCoCg(g_color.Load(int3(q, 0)).rgb);
            float2 d = (float2(base + int2(dx, dy)) + 0.5 - JitterPx) - inPos;
            float2 dOut = d * OutputSize / InputSize;
            float  w = exp(-2.29 * dot(dOut, dOut));
            sum += c * w;
            wsum += w;
            wmax = max(wmax, w);
            m1 += c;
            m2 += c * c;
            cmin = min(cmin, c);
            cmax = max(cmax, c);
            float z = g_depth.Load(q, 0);
            if (z < closest) { closest = z; closestPix = q; }
        }
    }
    float3 current = sum / max(wsum, 1e-5);
    float2 vel = g_velocity.Load(int3(closestPix, 0));
    float2 prevUv = uv + vel;
    if (HistoryValid < 0.5 || any(prevUv < 0.0) || any(prevUv > 1.0))
    {
        return float4(saturate(fromYCoCg(current)), alpha);
    }
    float3 mean  = m1 / 9.0;
    // 内部の 1 画素は出力の数画素にまたがるので、TAA より少し広い箱にする
    float3 sigma = 1.5 * sqrt(max(m2 / 9.0 - mean * mean, 0.0));
    float3 hist  = clipToBox(max(cmin, mean - sigma), min(cmax, mean + sigma), toYCoCg(sampleHistory(prevUv)));

    float movedPx = length(vel * OutputSize);
    float blend = lerp(BlendStill, BlendMoving, saturate(movedPx / MovingPixels)) * wmax;
    float wc = blend / (1.0 + current.x);
    float wh = (1.0 - blend) / (1.0 + hist.x);
    float3 result = (current * wc + hist * wh) / max(wc + wh, 1e-5);
    return float4(saturate(fromYCoCg(result)), alpha);
}
)hlsl";

/// @brief 出力の鮮鋭化 (AMD の CAS の考え方: 近傍の明暗の幅が狭い所ほど強く掛け、縁で輪郭を立てすぎない)
inline constexpr const char* DX12_UPSCALE_SHARPEN_PS = R"hlsl(
Texture2D<float4> g_src : register(t0);

cbuffer CbSharpen : register(b0)
{
    float2 Size;
    float  Sharpness;   // 0 = そのまま写す、1 = 最大
    float  _pad;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
    int2 p = int2(input.Position.xy);
    int2 hi = int2(Size) - 1;
    float4 c4 = g_src.Load(int3(p, 0));
    float3 c = c4.rgb;
    float3 n = g_src.Load(int3(clamp(p + int2(0, -1), 0, hi), 0)).rgb;
    float3 s = g_src.Load(int3(clamp(p + int2(0, 1), 0, hi), 0)).rgb;
    float3 e = g_src.Load(int3(clamp(p + int2(1, 0), 0, hi), 0)).rgb;
    float3 w = g_src.Load(int3(clamp(p + int2(-1, 0), 0, hi), 0)).rgb;
    float3 mn = min(c, min(min(n, s), min(e, w)));
    float3 mx = max(c, max(max(n, s), max(e, w)));
    float3 amp = sqrt(saturate(min(mn, 1.0 - mx) / max(mx, 1e-4)));
    float3 k = -amp * lerp(0.125, 0.2, Sharpness) * step(1e-4, Sharpness);
    float3 r = (c + k * (n + s + e + w)) / (1.0 + 4.0 * k);
    return float4(saturate(r), c4.a);
}
)hlsl";

/// @brief 出力の大きさが履歴と合わない間 (lo-fi の低解像 RT) に、内部解像度の絵を引き伸ばすだけの写し
inline constexpr const char* DX12_UPSCALE_BLIT_PS = R"hlsl(
Texture2D<float4> g_scene  : register(t1);
SamplerState      s_linear : register(s0);

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
    return g_scene.SampleLevel(s_linear, input.TexCoord, 0);
}
)hlsl";

} // namespace mitiru::render
