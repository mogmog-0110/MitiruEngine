#pragma once

/// @file DX12SplatSortShaders.hpp
/// @brief GPU 深度ソート (2048-bin single-pass radix/bucket sort) の compute HLSL。
/// @details CPU バケット計数ソートの GPU 移植。奥 (bucket0) → 手前 (bucket NB-1)。
///          バケット内順不同 — splat アルファ合成はバケット内順序に鈍感なので
///          CPU 版と同じ割り切り。full LSD radix の厳密全順序は視覚的に無意味な一方
///          4 パス + stable scatter の複雑さを負うため、単一パス bucket に統一する。
///
///          5 pass: reset → range(min/max) → hist → scan(prefix) → scatter。
///          key は camPos からの距離²。dist² は常に >= 0 なので asuint が単調で、
///          min/max を InterlockedMin/Max (uint) で取れる。

namespace mitiru::render {

/// @brief 5 つの compute entry (CSReset/CSRange/CSHist/CSScan/CSScatter) を持つ HLSL。
inline constexpr const char* SPLAT_SORT_CS_HLSL = R"HLSL(
#define NB 2048

cbuffer Params : register(b0)
{
    uint  gCount;   // splat 数
    uint  gStride;  // position 要素の byte stride
    uint  gOffset;  // 要素内の pos への byte offset
    float gCamX;
    float gCamY;
    float gCamZ;
    uint  _pad0;
    uint  _pad1;
};

ByteAddressBuffer        gPos   : register(t0);   // raw: 位置を stride/offset で読む
RWStructuredBuffer<uint> gRange : register(u0);   // [0]=min bits, [1]=max bits
RWStructuredBuffer<uint> gHist  : register(u1);   // [NB] 度数 → 走査後は exclusive offset
RWStructuredBuffer<uint> gOrder : register(u2);   // [gCount] 出力 index

float depthKey(uint i)
{
    float3 p = asfloat(gPos.Load3(i * gStride + gOffset));
    float dx = p.x - gCamX, dy = p.y - gCamY, dz = p.z - gCamZ;
    return dx * dx + dy * dy + dz * dz;   // 距離² (奥ほど大)
}

int bucketOf(float k, float kmin, float kmax)
{
    float inv = (kmax > kmin) ? ((float)(NB - 1) / (kmax - kmin)) : 0.0f;
    int b = (NB - 1) - (int)((k - kmin) * inv);   // 奥(大 key) → bucket 0
    return clamp(b, 0, NB - 1);
}

[numthreads(256, 1, 1)]
void CSReset(uint3 t : SV_DispatchThreadID)
{
    if (t.x < NB) { gHist[t.x] = 0u; }
    if (t.x == 0u) { gRange[0] = 0xFFFFFFFFu; gRange[1] = 0u; }
}

[numthreads(256, 1, 1)]
void CSRange(uint3 t : SV_DispatchThreadID)
{
    if (t.x >= gCount) { return; }
    uint ku = asuint(depthKey(t.x));   // dist² >= 0 → uint 単調
    InterlockedMin(gRange[0], ku);
    InterlockedMax(gRange[1], ku);
}

[numthreads(256, 1, 1)]
void CSHist(uint3 t : SV_DispatchThreadID)
{
    if (t.x >= gCount) { return; }
    int b = bucketOf(depthKey(t.x), asfloat(gRange[0]), asfloat(gRange[1]));
    InterlockedAdd(gHist[b], 1u);
}

[numthreads(1, 1, 1)]
void CSScan(uint3 t : SV_DispatchThreadID)
{
    // NB=2048 の exclusive prefix sum を単一スレッドで (1 ソート 1 回、無視できる)。
    uint run = 0u;
    [loop] for (uint b = 0u; b < NB; ++b) { uint c = gHist[b]; gHist[b] = run; run += c; }
}

[numthreads(256, 1, 1)]
void CSScatter(uint3 t : SV_DispatchThreadID)
{
    if (t.x >= gCount) { return; }
    int b = bucketOf(depthKey(t.x), asfloat(gRange[0]), asfloat(gRange[1]));
    uint slot;
    InterlockedAdd(gHist[b], 1u, slot);   // バケット内の席を原子的に確保 (順不同)
    gOrder[slot] = t.x;
}
)HLSL";

} // namespace mitiru::render
