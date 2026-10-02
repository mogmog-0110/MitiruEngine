#pragma once

/// @file DX12FogShaders.hpp
/// @brief froxel の体積フォグ (注入 → 奥行き方向の積分) と、空気遠近とフォグを HDR に掛ける合成の HLSL
/// @details DX12AtmosphereShaders.hpp の共通部 (CbAtmosphere、viewRay、toonBand) の後ろに連結して使う。
///          froxel の奥行きは VolumetricFog.hpp と同じ t^2 の切り方。注入は TAA と同じずらしの相で froxel の中の
///          標本の位置を動かし、前フレームの froxel へ戻して混ぜる (Wronski 2014)。積分はエネルギーを保つ式 (Hillaire 2015)。

namespace mitiru::render
{

inline constexpr const char* DX12_FOG_COMMON_HLSL = R"hlsl(
cbuffer CbFog : register(b1)
{
    float4x4 PrevViewProj;   // ずらしを抜いた前フレーム
    float4   FogDims;        // xyz = froxel の数 w = 奥の端 (世界単位)
    float4   FogDensity;     // x = 基準の密度 y = 高さの減り方 z = 基準の高さ w = 一様に足す密度
    float4   FogAlbedo;      // rgb = 散乱の割合 w = HG の g
    float4   SunRadiance;    // rgb = 主光源の色 × 強さ × 倍率 w = 影を見るか
    float4   AmbientUp;      // rgb = 上からの環境光 w = 局所光の倍率
    float4   AmbientDown;    // rgb = 下からの環境光 w = 履歴が使えるか
    float4   FogJitter;      // xyz = froxel の中の標本の位置 (0..1) w = 今フレームを混ぜる割合
    float4   PrevCamera;     // xyz = 前フレームのカメラ w = フォグが有効か
};

float fogSliceToDistance(float t) { return FogDims.w * t * t; }
float fogDistanceToSlice(float d) { return sqrt(saturate(d / max(FogDims.w, 1e-4))); }
)hlsl";

inline constexpr const char* DX12_FOG_INJECT_CS = R"hlsl(
cbuffer CbShadow : register(b2)
{
    float4x4 LightViewProj;
    float4x4 LightViewProjFar;
    float    CascadeSplitDistance;
    float    CascadeSplitDistance2;
    float    ShadowSoftness;
    float    ShadowBiasNdc;
    float4x4 LightViewProjFar2;
};

cbuffer CbCluster : register(b3)
{
    uint4  ClusterGrid;    // xyz = froxel の数 w = 局所光の数
    float4 ClusterDepth;   // x = near y = far z = Z/log(far/near) w = -Z*log(near)/log(far/near)
    float4 ClusterScreen;
    float4 ClusterForward; // xyz = 視線
};

struct LocalLightGpu
{
    float3 positionWS;  float range;
    float3 color;       float spotScale;
    float3 directionWS; float spotOffset;
    float3 boundCenterVS; float boundRadius;
};

Texture2D<float>                g_shadow     : register(t3);
Texture2D<float>                g_shadowFar  : register(t4);
Texture3D<float4>               g_history    : register(t5);
StructuredBuffer<LocalLightGpu> g_lights     : register(t8);
StructuredBuffer<uint>          g_masks      : register(t9);
SamplerComparisonState          s_cmp        : register(s1);
RWTexture3D<float4>             g_out        : register(u0);

float hgPhase(float g, float c)
{
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * PI * pow(max(1.0 + g2 - 2.0 * g * c, 1e-4), 1.5));
}

float shadowTap(Texture2D<float> tex, float4 clip, float column, float columns)
{
    float3 ndc = clip.xyz / max(clip.w, 1e-4);
    float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    if (any(uv < 0.0) || any(uv > 1.0) || ndc.z < 0.0 || ndc.z > 1.0) { return 1.0; }
    uv.x = (uv.x + column) / columns;
    return tex.SampleCmpLevelZero(s_cmp, uv, ndc.z - 0.002);
}

// 前方の影と同じ規則でカスケードを選ぶ。froxel はぼけるので PCF はせず 1 タップ
float sunShadow(float3 wp, float dist)
{
    if (SunRadiance.w < 0.5) { return 1.0; }
    if (dist < CascadeSplitDistance) { return shadowTap(g_shadow, mul(LightViewProj, float4(wp, 1.0)), 0.0, 1.0); }
    if (dist < CascadeSplitDistance2) { return shadowTap(g_shadowFar, mul(LightViewProjFar, float4(wp, 1.0)), 0.0, 2.0); }
    return shadowTap(g_shadowFar, mul(LightViewProjFar2, float4(wp, 1.0)), 1.0, 2.0);
}

// 前方の描画と同じ froxel の光の一覧から、点光源とスポットの散乱を足す
float3 localLightScatter(float3 wp, float2 uv, float3 dir)
{
    uint count = ClusterGrid.w;
    if (count == 0 || AmbientUp.w <= 0.0) { return 0.0; }
    float depth = max(dot(wp - CameraWorld.xyz, ClusterForward.xyz), ClusterDepth.x);
    int z = clamp((int)floor(log(depth) * ClusterDepth.z + ClusterDepth.w), 0, (int)ClusterGrid.z - 1);
    uint x = min((uint)(uv.x * ClusterGrid.x), ClusterGrid.x - 1);
    uint y = min((uint)(uv.y * ClusterGrid.y), ClusterGrid.y - 1);
    uint cluster = ((uint)z * ClusterGrid.y + y) * ClusterGrid.x + x;
    float3 sum = 0.0;
    [loop]
    for (uint w = 0; w < 8; ++w)
    {
        uint bits = g_masks[cluster * 8 + w];
        [loop]
        while (bits != 0)
        {
            uint b = firstbitlow(bits);
            bits &= bits - 1;
            LocalLightGpu l = g_lights[w * 32 + b];
            float3 d = l.positionWS - wp;
            float dist2 = dot(d, d);
            float r2 = l.range * l.range;
            if (dist2 >= r2) { continue; }
            float3 L = d * rsqrt(max(dist2, 1e-8));
            float win = saturate(1.0 - (dist2 * dist2) / (r2 * r2));
            float spot = saturate(dot(-L, l.directionWS) * l.spotScale + l.spotOffset);
            float shape = win * win * spot * spot;
            sum += l.color * shape / (dist2 + 1.0) * hgPhase(FogAlbedo.w, dot(L, dir));
        }
    }
    return sum * AmbientUp.w;
}

float fogDensityAt(float y)
{
    return FogDensity.x * exp(-FogDensity.y * (y - FogDensity.z)) + FogDensity.w;
}

float4 reprojectHistory(float3 wp, float4 cur)
{
    if (AmbientDown.w < 0.5) { return cur; }
    float4 pc = mul(PrevViewProj, float4(wp, 1.0));
    if (pc.w <= 1e-4) { return cur; }
    float2 pn = pc.xy / pc.w;
    float2 puv = float2(pn.x * 0.5 + 0.5, 0.5 - pn.y * 0.5);
    float pd = length(wp - PrevCamera.xyz);
    if (any(puv < 0.0) || any(puv > 1.0) || pd > FogDims.w) { return cur; }
    float4 h = g_history.SampleLevel(s_linear, float3(puv, fogDistanceToSlice(pd)), 0);
    return lerp(h, cur, FogJitter.w);
}

[numthreads(4, 4, 4)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= (uint3)FogDims.xyz)) { return; }
    float2 uv = (id.xy + FogJitter.xy) / FogDims.xy;
    float t = (id.z + FogJitter.z) / FogDims.z;
    float dist = fogSliceToDistance(t);
    float3 dir = viewRay(uv);
    float3 wp = CameraWorld.xyz + dir * dist;
    float ext = max(fogDensityAt(wp.y), 0.0);
    float3 light = SunRadiance.rgb * sunShadow(wp, dist) * hgPhase(FogAlbedo.w, dot(dir, SunDir.xyz));
    light += 0.5 * (AmbientUp.rgb + AmbientDown.rgb);
    light += localLightScatter(wp, uv, dir);
    float4 cur = float4(FogAlbedo.rgb * ext * light, ext);
    g_out[id] = reprojectHistory(wp, cur);
}
)hlsl";

/// @brief カメラから奥へ froxel を積む。rgb = そこまでの散乱、a = そこまでの透過率 (froxel の奥の端までの値)
inline constexpr const char* DX12_FOG_INTEGRATE_CS = R"hlsl(
Texture3D<float4>   g_inject : register(t5);
RWTexture3D<float4> g_out    : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= (uint2)FogDims.xy)) { return; }
    float3 S = 0.0;
    float T = 1.0;
    float d0 = 0.0;
    uint n = (uint)FogDims.z;
    [loop]
    for (uint z = 0; z < n; ++z)
    {
        float d1 = fogSliceToDistance((z + 1.0) / n);
        float4 v = g_inject[uint3(id.xy, z)];
        float ext = max(v.a, 1e-6);
        float segT = exp(-ext * (d1 - d0));
        S += T * (v.rgb - v.rgb * segT) / ext;
        T *= segT;
        g_out[uint3(id.xy, z)] = float4(S, T);
        d0 = d1;
    }
}
)hlsl";

/// @brief resolve 済みの HDR に空気遠近とフォグを掛ける。出力は (散乱, 透過率) で、混ぜは dst × a + src
/// @details MSAA の 4 標本の深度でそれぞれ求めて平均する。縁で空と物の画素が混ざっても、どちらの側も
///          自分の距離の霞みを受ける。空の標本は空気遠近を描画済みなので、フォグだけを受ける。
inline constexpr const char* DX12_FOG_COMPOSITE_HLSL = R"hlsl(
Texture2DMS<float, 4> g_depth  : register(t0);
Texture3D<float4>     g_aerial : register(t1);
Texture3D<float4>     g_fog    : register(t2);

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

void aerialAt(float2 uv, float dist, inout float3 S, inout float T)
{
    if (ApParams.w < 0.5) { return; }
    float km = dist * CameraKm.w * ApParams.y;
    float w = sqrt(saturate(km / ApParams.x));
    float4 ap = g_aerial.SampleLevel(s_linear, float3(uv, w), 0);
    // 1 枚目の froxel の中心より手前は 0 へ寄せる (カメラの位置では霞みがない)
    float f = saturate(w * kApSlices * 2.0);
    float apT = lerp(1.0, ap.a, f);
    float3 apS = ap.rgb * SunDir.w * f;
    if (Radii.w > 1.5)
    {
        float q = ceil(apT * Radii.w) / Radii.w;
        apS *= (1.0 - q) / max(1.0 - apT, 1e-4);
        apT = q;
    }
    S = S + apS * T;
    T *= apT;
}

void fogAt(float2 uv, float dist, inout float3 S, inout float T)
{
    if (PrevCamera.w < 0.5) { return; }
    float t = fogDistanceToSlice(dist);
    float n = FogDims.z;
    // 積分の値は froxel の奥の端のもの。t の位置の値を読むために半分ずらし、1 枚目より手前は割合で薄める
    float4 v = g_fog.SampleLevel(s_linear, float3(uv, max(t - 0.5 / n, 0.5 / n)), 0);
    float f = saturate(t * n);
    // 手前のフォグは奥の霞み (空気遠近) の前にかかる
    S = lerp(0.0, v.rgb, f) + S * lerp(1.0, v.a, f);
    T *= lerp(1.0, v.a, f);
}

float4 PSMain(PSInput input) : SV_TARGET
{
    int2 pix = int2(input.Position.xy);
    float2 uv = (float2(pix) + 0.5) * Screen.zw;
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float3 sumS = 0.0;
    float sumT = 0.0;
    [unroll]
    for (int s = 0; s < 4; ++s)
    {
        float d = g_depth.Load(pix, s);
        float3 S = 0.0;
        float T = 1.0;
        if (d < 1.0)
        {
            float4 wp = mul(InvViewProj, float4(ndc, d, 1.0));
            float dist = length(wp.xyz / wp.w - CameraWorld.xyz);
            aerialAt(uv, dist, S, T);
            fogAt(uv, dist, S, T);
        }
        else
        {
            fogAt(uv, FogDims.w, S, T);
        }
        sumS += S;
        sumT += T;
    }
    return float4(sumS * 0.25, sumT * 0.25);
}
)hlsl";

} // namespace mitiru::render
