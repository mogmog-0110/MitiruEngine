#pragma once

/// @file DX12ClusterShaders.hpp
/// @brief 局所光を froxel へ割り当てる compute (LocalLights.hpp の assignLocalLightsToClusters と同じ判定)
/// @details 1 スレッドが 1 froxel を受け持ち、光の包み球を 64 個ずつ groupshared に載せて箱と比べる。
///          結果は froxel ごとに 8 語 (256 bit) のビット集合で、書き込みは自分の froxel だけなので atomic は要らない。

namespace mitiru::render
{

inline constexpr const char* DX12_CLUSTER_BUILD_CS = R"hlsl(
cbuffer CbBuild : register(b0)
{
    uint4  Grid;     // xyz=froxel の数 w=光の数
    float4 Frustum;  // x=tan(水平半角) y=tan(垂直半角) z=near w=far
};

struct LocalLightGpu
{
    float3 positionWS;  float range;
    float3 color;       float spotScale;
    float3 directionWS; float spotOffset;
    float3 boundCenterVS; float boundRadius;
};

StructuredBuffer<LocalLightGpu> Lights : register(t0);
RWStructuredBuffer<uint>        Masks  : register(u0);

groupshared float4 s_bounds[64];

float sliceDepth(uint k)
{
    return Frustum.z * pow(Frustum.w / Frustum.z, (float)k / (float)Grid.z);
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
    const uint cluster = dtid.x;
    const uint total = Grid.x * Grid.y * Grid.z;
    const uint cx = cluster % Grid.x;
    const uint cy = (cluster / Grid.x) % Grid.y;
    const uint cz = cluster / (Grid.x * Grid.y);

    const float zn = sliceDepth(cz);
    const float zf = sliceDepth(cz + 1);
    const float x0 = -1.0 + 2.0 * (float)cx / (float)Grid.x;
    const float x1 = -1.0 + 2.0 * (float)(cx + 1) / (float)Grid.x;
    const float y0 = 1.0 - 2.0 * (float)(cy + 1) / (float)Grid.y;
    const float y1 = 1.0 - 2.0 * (float)cy / (float)Grid.y;
    const float3 bmin = float3(min(x0 * zn, x0 * zf) * Frustum.x, min(y0 * zn, y0 * zf) * Frustum.y, zn);
    const float3 bmax = float3(max(x1 * zn, x1 * zf) * Frustum.x, max(y1 * zn, y1 * zf) * Frustum.y, zf);

    uint words[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const uint count = Grid.w;
    for (uint base = 0; base < count; base += 64)
    {
        const uint li = base + gi;
        s_bounds[gi] = (li < count) ? float4(Lights[li].boundCenterVS, Lights[li].boundRadius)
                                    : float4(0.0, 0.0, 0.0, -1.0);
        GroupMemoryBarrierWithGroupSync();
        const uint n = min(64u, count - base);
        for (uint k = 0; k < n; ++k)
        {
            const float4 b = s_bounds[k];
            const float3 d = max(max(bmin - b.xyz, 0.0), b.xyz - bmax);
            if (dot(d, d) <= b.w * b.w)
            {
                const uint idx = base + k;
                words[idx >> 5] |= 1u << (idx & 31u);
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (cluster < total)
    {
        [unroll]
        for (uint w = 0; w < 8; ++w) { Masks[cluster * 8 + w] = words[w]; }
    }
}
)hlsl";

} // namespace mitiru::render
