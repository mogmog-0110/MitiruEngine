#pragma once

/// @file DX12SsrShaders.hpp
/// @brief 画面の反射が次のフレームで辿る、深度の最小値の階層 (HZB) と色の mip を作る compute (cs_5_0)
/// @details どれも t0 を読んで u0 へ書く。b0 の xy は書く段の大きさ、zw は読む段の大きさ。
///          HZB の段 k の 1 画素は段 k-1 の 2x2 (読む段の幅か高さが奇数なら端の 1 列・1 行も含める) の最小値。

namespace mitiru::render
{

inline constexpr const char* DX12_SSR_COMMON_HLSL = R"hlsl(
cbuffer CbSsrBuild : register(b0)
{
    uint4 Size;   // xy = 書く段の大きさ zw = 読む段の大きさ
};
)hlsl";

/// @brief 段 0: MSAA の深度の 4 標本の一番手前
inline constexpr const char* DX12_SSR_HZB_FIRST_CS = R"hlsl(
Texture2DMS<float> g_depth : register(t0);
RWTexture2D<float> g_out   : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= Size.xy)) { return; }
    float z = 1.0;
    [unroll]
    for (int s = 0; s < 4; ++s) { z = min(z, g_depth.Load(int2(id.xy), s)); }
    g_out[id.xy] = z;
}
)hlsl";

inline constexpr const char* DX12_SSR_HZB_DOWN_CS = R"hlsl(
Texture2D<float>   g_src : register(t0);
RWTexture2D<float> g_out : register(u0);

float tap(int2 p) { return g_src.Load(int3(min(p, int2(Size.zw) - 1), 0)); }

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= Size.xy)) { return; }
    int2 s = int2(id.xy) * 2;
    float z = min(min(tap(s), tap(s + int2(1, 0))), min(tap(s + int2(0, 1)), tap(s + int2(1, 1))));
    bool lastX = (id.x == Size.x - 1) && (Size.z & 1);
    bool lastY = (id.y == Size.y - 1) && (Size.w & 1);
    if (lastX) { z = min(z, min(tap(s + int2(2, 0)), tap(s + int2(2, 1)))); }
    if (lastY) { z = min(z, min(tap(s + int2(0, 2)), tap(s + int2(1, 2)))); }
    if (lastX && lastY) { z = min(z, tap(s + int2(2, 2))); }
    g_out[id.xy] = z;
}
)hlsl";

/// @brief 色の段 0: HDR の写し
inline constexpr const char* DX12_SSR_COLOR_COPY_CS = R"hlsl(
Texture2D<float4>   g_src : register(t0);
RWTexture2D<float4> g_out : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= Size.xy)) { return; }
    g_out[id.xy] = float4(g_src.Load(int3(id.xy, 0)).rgb, 1.0);
}
)hlsl";

/// @brief 色の段 k: 段 k-1 の 2x2 の平均
inline constexpr const char* DX12_SSR_COLOR_DOWN_CS = R"hlsl(
Texture2D<float4>   g_src : register(t0);
RWTexture2D<float4> g_out : register(u0);

float4 tap(int2 p) { return g_src.Load(int3(min(p, int2(Size.zw) - 1), 0)); }

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= Size.xy)) { return; }
    int2 s = int2(id.xy) * 2;
    g_out[id.xy] = (tap(s) + tap(s + int2(1, 0)) + tap(s + int2(0, 1)) + tap(s + int2(1, 1))) * 0.25;
}
)hlsl";

} // namespace mitiru::render
