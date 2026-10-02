#pragma once

/// @file DX12MotionBlurShaders.hpp
/// @brief 動きのぼけの HLSL。タイルの構造は McGuire ら (2012)、集めの重みは Jimenez (2014、CoD:AW)
/// @details 画面を TILE 画素角のタイルに分け、タイルごとの最大の動き → 3x3 の近傍の最大を求め、
///          画素ごとに近傍の最大の向きへ対称に 8 組 16 点を集める。重みは前後関係と広がりで決め、
///          組の片方が手前の物なら奥の側にも同じ重みを写す。こうすると動く物の縁は内側も外側も
///          同じ幅で透け、ぼけの両端が直線の傾斜になる。
///          ぼけの長さは「動き × シャッターの割合」の半分 (前後へ半分ずつ) で、TILE 画素で頭打ちにする。
///          root sig / VS は temporal のもの (SRV t0..t3 + CBV b0) を共用する。

namespace mitiru::render
{

inline constexpr int kMotionBlurTile = 20;   ///< 720p で 1 タイル 20 画素 (McGuire らの k)

/// @brief 先頭に "#define TILE <kMotionBlurTile>" を足してからコンパイルする
inline constexpr const char* DX12_MOTION_BLUR_COMMON_HLSL = R"hlsl(
#define PAIRS 8

cbuffer CbMotionBlur : register(b0)
{
    float2 ScreenSize;
    float  HalfExposure;   // シャッターの割合の半分
    float  NearZ;
    float  FarZ;
    float3 _pad;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

// 動きベクトル (UV、前 − 今) から、ぼけの半分の長さのベクトル (画素、動く向き) を作る
float2 blurHalfVector(float2 mvUv)
{
    float2 v = -mvUv * ScreenSize * HalfExposure;
    float  len = length(v);
    return (len > TILE) ? v * (TILE / len) : v;
}
)hlsl";

/// @brief タイル内の最大の動き。出力はタイル解像度
inline constexpr const char* DX12_MOTION_BLUR_TILE_MAX_PS = R"hlsl(
Texture2D<float2> g_velocity : register(t0);

float2 PSMain(PSInput input) : SV_TARGET
{
    int2 base = int2(input.Position.xy) * TILE;
    int2 hi   = int2(ScreenSize) - 1;
    float2 best = 0.0;
    float  bestLen2 = 0.0;
    [loop]
    for (int y = 0; y < TILE; ++y)
    {
        [loop]
        for (int x = 0; x < TILE; ++x)
        {
            float2 v = blurHalfVector(g_velocity.Load(int3(min(base + int2(x, y), hi), 0)));
            float  l2 = dot(v, v);
            if (l2 > bestLen2) { best = v; bestLen2 = l2; }
        }
    }
    return best;
}
)hlsl";

/// @brief 3x3 のタイルの最大。隣のタイルから伸びてくるぼけを取りこぼさない
inline constexpr const char* DX12_MOTION_BLUR_NEIGHBOR_MAX_PS = R"hlsl(
Texture2D<float2> g_tileMax : register(t0);

float2 PSMain(PSInput input) : SV_TARGET
{
    int2 tile = int2(input.Position.xy);
    int2 hi   = int2(ceil(ScreenSize / TILE)) - 1;
    float2 best = 0.0;
    float  bestLen2 = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 v  = g_tileMax.Load(int3(clamp(tile + int2(x, y), int2(0, 0), hi), 0));
            float  l2 = dot(v, v);
            if (l2 > bestLen2) { best = v; bestLen2 = l2; }
        }
    }
    return best;
}
)hlsl";

/// @brief 画素ごとの集め
inline constexpr const char* DX12_MOTION_BLUR_GATHER_PS = R"hlsl(
Texture2D<float4>     g_color       : register(t0);
Texture2D<float2>     g_velocity    : register(t1);
Texture2D<float2>     g_neighborMax : register(t2);
Texture2DMS<float, 4> g_depth       : register(t3);

float linearDepth(int2 p)
{
    float d = g_depth.Load(p, 0);
    return NearZ * FarZ / (FarZ - d * (FarZ - NearZ));
}

// x = 標本が中心より奥 (中心が手前の物)、y = 標本が中心より手前。境の幅は距離に比例させる
float2 depthCompare(float centre, float sample)
{
    float scale = 1.0 / max(0.05, 0.02 * centre);
    return saturate(0.5 + float2(scale, -scale) * (sample - centre));
}

// 中心から offset 画素の標本に、中心 / 標本自身のぼけが届くか (1 画素ぶん柔らかく)
float2 spreadCompare(float offset, float centreSpread, float sampleSpread)
{
    return saturate(float2(centreSpread, sampleSpread) - offset + 1.0);
}

float sampleWeight(float zC, float zS, float offset, float spreadC, float spreadS)
{
    return dot(depthCompare(zC, zS), spreadCompare(offset, spreadC, spreadS));
}

float4 PSMain(PSInput input) : SV_TARGET
{
    int2   X  = int2(input.Position.xy);
    float4 cX = g_color.Load(int3(X, 0));
    float2 vN = g_neighborMax.Load(int3(X / TILE, 0));
    float  lenN = length(vN);
    if (lenN <= 0.5) { return cX; }

    int2  hi      = int2(ScreenSize) - 1;
    float spreadC = length(blurHalfVector(g_velocity.Load(int3(X, 0))));
    float zC      = linearDepth(X);
    // 組ごとの間隔を画素ごとにずらし、点の並びの縞を砂目にする (時刻に依らないので撮影は毎回同じ)
    float j = frac(52.9829189 * frac(dot(float2(X), float2(0.06711056, 0.00583715)))) - 0.5;

    float3 sum = 0.0;
    float  wsum = 0.0;
    [unroll]
    for (int i = 0; i < PAIRS; ++i)
    {
        float  t      = (i + 0.5 + j * 0.5) / PAIRS;
        float2 off    = vN * t;
        float  offLen = lenN * t;
        int2   Y1 = clamp(int2(floor(float2(X) + 0.5 + off)), int2(0, 0), hi);
        int2   Y2 = clamp(int2(floor(float2(X) + 0.5 - off)), int2(0, 0), hi);
        float  z1 = linearDepth(Y1);
        float  z2 = linearDepth(Y2);
        float  s1 = length(blurHalfVector(g_velocity.Load(int3(Y1, 0))));
        float  s2 = length(blurHalfVector(g_velocity.Load(int3(Y2, 0))));
        float  w1 = sampleWeight(zC, z1, offLen, spreadC, s1);
        float  w2 = sampleWeight(zC, z2, offLen, spreadC, s2);
        // 片方が手前の物の端なら、奥の側にも同じ重みを写す (手前の物の後ろに隠れていた背景を補う)
        bool2 mirror = bool2(z1 > z2, s2 > s1);
        w1 = all(mirror) ? w2 : w1;
        w2 = any(mirror) ? w2 : w1;
        sum  += w1 * g_color.Load(int3(Y1, 0)).rgb + w2 * g_color.Load(int3(Y2, 0)).rgb;
        wsum += w1 + w2;
    }
    sum  /= (2.0 * PAIRS);
    wsum /= (2.0 * PAIRS);
    return float4(sum + (1.0 - wsum) * cX.rgb, cX.a);
}
)hlsl";

} // namespace mitiru::render
