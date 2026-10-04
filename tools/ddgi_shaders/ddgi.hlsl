// 動く光の GI (DDGI)。プローブごとに決まった数のレイを DXR 1.1 の inline ray query で飛ばし、
// 当たった面の放射輝度を SH L2 へ射影して、焼いた光と同じ格子 (float4 x 7 / プローブと距離の地図) へ履歴つきで混ぜる。
// generate_blobs.py が SM 6.6 でコンパイルして DdgiShaderBlobs_tables.hpp に置く (d3dcompiler は SM 6.5 以上を扱えない)。
// 当たった面の光は render/gi/BakeScene.hpp の directIrradiance と同じ式で、跳ね返りは前のフレームの格子を
// 前方の描画と同じ HLSL (lighting_probes.hlsli) で引く。

#define DDGI_GROUP 64
#define DDGI_MAX_RAYS 256

// lighting_probes.hlsli が読む名前。格子の数値だけを使い、反射のプローブと SSR の値は置き場として持つ
cbuffer CbDdgi : register(b0)
{
    float4 GiOrigin;      // xyz = (0,0,0) のプローブ、w = 前のフレームの格子を引けるか
    float4 GiSpacing;     // xyz = 間隔、w = 強さ (使わない)
    uint4  GiDims;        // xyz = プローブの数、w = 距離の地図の 1 行の枚数
    float4 GiParams;      // x = 面から浮かせる距離、y = 段 (0)、zw = 1 / 地図の大きさ
    float4 ReflParams;
    float4 ReflBoxMin[1];
    float4 ReflBoxMax[1];
    float4 ReflPos[1];
    float4 SsrParams;
    float4 SsrScreen;
    float4 SsrRay;
    float4 SsrDepth;
    float4x4 SsrPrevViewProj;
    float4 CameraPosPad;
    float4 RayRot0;       // このフレームのレイの回転 (行)
    float4 RayRot1;
    float4 RayRot2;
    uint4  RayParams;     // x = 1 プローブのレイの数、y = プローブの数、z = 局所光の数、w = kDdgiFlag*
    float4 SunDir;        // xyz = 光の進む向き
    float4 SunColor;      // 線形の色 x 強さ
    float4 SkyZenith;
    float4 SkyHorizon;
    float4 SkyGround;
    float4 Blend;         // x = 履歴の重み、y = 大きく変わった時の重み、z = 大きく変わったとみなす割合、w = 距離の上限
    float4 Misc;          // x = 面の中とみなす裏面の割合、y = 距離の地図の鋭さ、z = 裏面の割合の履歴の重み、w = 局所光の影のレイを止める光の手前の距離
}

#define CameraPos (CameraPosPad.xyz)
static const float PI = 3.14159265359;
static const uint kDdgiFlagSun = 1u;
static const uint kDdgiFlagHistory = 2u;      // 放射照度に履歴がある (無ければ今のフレームの値をそのまま置く)
static const uint kDdgiFlagVisHistory = 4u;   // 距離の地図に履歴がある

SamplerState g_sampClamp : register(s1);
#include "lighting_probes.hlsli"

struct InstanceInfo
{
    uint   attrib;       // 三角形ごとの法線と色 (StructuredBuffer<uint4>) の記述子の番号
    uint   flags;        // bit 0 = 両面
    float2 pad;
    float4 albedo;       // 線形の基本色
    float4 n0;           // 法線の行列 (world の 3x3 の逆の転置) の行
    float4 n1;
    float4 n2;
};

struct DdgiLight
{
    float4 posRange;     // xyz = 位置、w = 届く距離
    float4 colorScale;   // xyz = 線形の色 x 強さ、w = スポットの scale (点光源は 0)
    float4 dirOffset;    // xyz = スポットの向き、w = スポットの offset (点光源は 1)
};

RaytracingAccelerationStructure g_scene     : register(t0);
StructuredBuffer<InstanceInfo>  g_instances : register(t1);
StructuredBuffer<DdgiLight>     g_lights    : register(t2);
RWStructuredBuffer<float4>      g_rays      : register(u0);   // rgb = 放射輝度、a = 距離 (裏面は負)
RWStructuredBuffer<float4>      g_probesOut : register(u1);
RWTexture2D<float2>             g_visOut    : register(u2);
RWStructuredBuffer<float4>      g_state     : register(u3);   // x = 裏面の割合の履歴

float3 probePosition(uint probe)
{
    uint3 d = GiDims.xyz;
    uint3 c = uint3(probe % d.x, (probe / d.x) % d.y, probe / (d.x * d.y));
    return GiOrigin.xyz + float3(c) * GiSpacing.xyz;
}

// render/gi/ProbeMath.hpp の sphericalFibonacci と同じ
float3 sphericalFibonacci(uint i, uint n)
{
    const float golden = 1.61803398874989484820;
    float phi = 2.0 * PI * frac((float)i / golden);
    float cosT = 1.0 - (2.0 * (float)i + 1.0) / (float)n;
    float sinT = sqrt(saturate(1.0 - cosT * cosT));
    return float3(cos(phi) * sinT, cosT, sin(phi) * sinT);
}

float3 rayDirection(uint i, uint n)
{
    float3 v = sphericalFibonacci(i, n);
    return normalize(float3(dot(RayRot0.xyz, v), dot(RayRot1.xyz, v), dot(RayRot2.xyz, v)));
}

float3 skyRadiance(float3 dir)
{
    if (dir.y < 0.0) { return SkyGround.rgb; }
    return lerp(SkyHorizon.rgb, SkyZenith.rgb, sqrt(dir.y));
}

bool occluded(float3 o, float3 d, float tMax)
{
    RayDesc ray;
    ray.Origin = o;
    ray.Direction = d;
    ray.TMin = 0.0;
    ray.TMax = tMax;
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
    q.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFF, ray);
    q.Proceed();
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

// render/gi/BakeScene.hpp の localIrradiance と同じ (範囲の窓 x スポットの円錐 x 1 / (距離^2 + 1))。
// 影のレイは光の手前 Misc.w で止め、灯りに描いた形が光を塞がないようにする
float3 localIrradiance(DdgiLight l, float3 p, float3 n, float3 origin)
{
    float3 toLight = l.posRange.xyz - p;
    float dist2 = dot(toLight, toLight);
    float r2 = l.posRange.w * l.posRange.w;
    if (!(dist2 < r2) || dist2 <= 1e-12) { return 0.0; }
    float dist = sqrt(dist2);
    float3 L = toLight / dist;
    float ndl = dot(n, L);
    if (ndl <= 0.0) { return 0.0; }
    float win = max(0.0, 1.0 - (dist2 * dist2) / (r2 * r2));
    float spot = saturate(dot(-L, l.dirOffset.xyz) * l.colorScale.w + l.dirOffset.w);
    float shape = win * win * spot * spot;
    if (shape <= 0.0 || (dist > Misc.w && occluded(origin, L, dist - max(Misc.w, 2e-3)))) { return 0.0; }
    return l.colorScale.rgb * (PI * ndl * shape / (dist2 + 1.0));
}

// 面 (位置 p、表の向き n) が太陽と局所光から直接受ける放射照度。影はレイで調べる
float3 directIrradiance(float3 p, float3 n)
{
    float3 origin = p + n * 2e-3;
    float3 e = 0.0;
    if ((RayParams.w & kDdgiFlagSun) != 0)
    {
        float3 L = -SunDir.xyz;
        float ndl = dot(n, L);
        if (ndl > 0.0 && !occluded(origin, L, 1e5)) { e += SunColor.rgb * (PI * ndl); }
    }
    for (uint i = 0; i < RayParams.z; ++i) { e += localIrradiance(g_lights[i], p, n, origin); }
    return e;
}

float3 unpackHalf3(uint a, uint b)
{
    return float3(f16tof32(a), f16tof32(a >> 16), f16tof32(b));
}

// 当たった面の放射輝度 (表) か、裏に当たった印 (負の距離)
float4 shadeHit(float3 origin, float3 dir, float t, uint instance, uint prim)
{
    InstanceInfo info = g_instances[instance];
    StructuredBuffer<uint4> attribs = ResourceDescriptorHeap[NonUniformResourceIndex(info.attrib)];
    uint4 a = attribs[prim];
    float3 nObj = unpackHalf3(a.x, a.y);
    float3 albedo = float3(f16tof32(a.y >> 16), f16tof32(a.z), f16tof32(a.z >> 16)) * info.albedo.rgb;
    float3 n = normalize(float3(dot(info.n0.xyz, nObj), dot(info.n1.xyz, nObj), dot(info.n2.xyz, nObj)));
    if (dot(n, dir) > 0.0)
    {
        if ((info.flags & 1u) == 0) { return float4(0.0, 0.0, 0.0, -t); }
        n = -n;
    }
    float3 p = origin + dir * t;
    float3 radiance = albedo * directIrradiance(p, n) / PI;
    if (GiOrigin.w > 0.5)
    {
        bool ok;
        float3 e = giIrradiance(p, n, -dir, ok);
        if (ok) { radiance += albedo * e / PI; }
    }
    return float4(radiance, t);
}

[numthreads(DDGI_GROUP, 1, 1)]
void TraceCS(uint3 id : SV_DispatchThreadID)
{
    uint rays = RayParams.x;
    uint probe = id.x / rays;
    if (probe >= RayParams.y) { return; }
    float3 origin = probePosition(probe);
    float3 dir = rayDirection(id.x % rays, rays);
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = 0.0;
    ray.TMax = 1e5;
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
    q.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFF, ray);
    q.Proceed();
    float4 result = float4(skyRadiance(dir), 1e5);
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        result = shadeHit(origin, dir, q.CommittedRayT(), q.CommittedInstanceID(), q.CommittedPrimitiveIndex());
    }
    g_rays[id.x] = result;
}

groupshared float4 gsRay[DDGI_MAX_RAYS];
groupshared float3 gsDir[DDGI_MAX_RAYS];
groupshared float  gsSh[27][DDGI_GROUP];
groupshared uint   gsBack[DDGI_GROUP];
groupshared float2 gsVis[256];

float shBasis(uint k, float3 n)
{
    switch (k)
    {
    case 0: return 0.282095;
    case 1: return 0.488603 * n.y;
    case 2: return 0.488603 * n.z;
    case 3: return 0.488603 * n.x;
    case 4: return 1.092548 * n.x * n.y;
    case 5: return 1.092548 * n.y * n.z;
    case 6: return 0.315392 * (3.0 * n.z * n.z - 1.0);
    case 7: return 1.092548 * n.x * n.z;
    default: return 0.546274 * (n.x * n.x - n.y * n.y);
    }
}

float shCosineBand(uint k) { return k == 0 ? PI : (k < 4 ? 2.0 * PI / 3.0 : PI / 4.0); }

// render/gi/ProbeMath.hpp の octDecode と同じ
float3 octDecode(float2 uv)
{
    float3 n = float3(uv.x, 1.0 - abs(uv.x) - abs(uv.y), uv.y);
    if (n.y < 0.0)
    {
        float2 f = (1.0 - abs(uv.yx)) * float2(uv.x >= 0.0 ? 1.0 : -1.0, uv.y >= 0.0 ? 1.0 : -1.0);
        n.x = f.x;
        n.z = f.y;
    }
    return normalize(n);
}

// 各スレッドが受け持つレイの SH を足し、group で 27 個の係数へまとめる
void accumulateSh(uint tid, uint rays)
{
    float acc[27];
    [unroll] for (uint k = 0; k < 27; ++k) { acc[k] = 0.0; }
    uint back = 0;
    for (uint r = tid; r < rays; r += DDGI_GROUP)
    {
        float4 v = gsRay[r];
        back += v.a < 0.0 ? 1u : 0u;
        float3 dir = gsDir[r];
        [unroll] for (uint c = 0; c < 9; ++c)
        {
            float y = shBasis(c, dir);
            acc[c * 3 + 0] += v.r * y;
            acc[c * 3 + 1] += v.g * y;
            acc[c * 3 + 2] += v.b * y;
        }
    }
    [unroll] for (uint m = 0; m < 27; ++m) { gsSh[m][tid] = acc[m]; }
    gsBack[tid] = back;
}

// 新しい係数を前の係数と混ぜる。前の値から大きく変わったプローブ (光が点いた・扉が開いた) は履歴を軽くする
void blendIrradiance(uint probe, uint rays, uint tid)
{
    if (tid != 0) { return; }
    float weight = 4.0 * PI / (float)rays;
    float fresh[27];
    [unroll] for (uint k = 0; k < 27; ++k)
    {
        float s = 0.0;
        for (uint t = 0; t < DDGI_GROUP; ++t) { s += gsSh[k][t]; }
        fresh[k] = s * weight * shCosineBand(k / 3);
    }
    uint back = 0;
    for (uint t2 = 0; t2 < DDGI_GROUP; ++t2) { back += gsBack[t2]; }
    float frac = (float)back / (float)rays;
    float4 state = g_state[probe];
    bool history = (RayParams.w & kDdgiFlagHistory) != 0;
    state.x = history ? lerp(frac, state.x, Misc.z) : frac;
    g_state[probe] = state;

    uint base = probe * 7;
    float old[28];
    [unroll] for (uint j = 0; j < 7; ++j)
    {
        float4 o = g_probesOut[base + j];
        old[j * 4 + 0] = o.x; old[j * 4 + 1] = o.y; old[j * 4 + 2] = o.z; old[j * 4 + 3] = o.w;
    }
    float3 oldDc = float3(old[0], old[1], old[2]);
    float3 newDc = float3(fresh[0], fresh[1], fresh[2]);
    float change = max(max(abs(newDc.r - oldDc.r), abs(newDc.g - oldDc.g)), abs(newDc.b - oldDc.b));
    float scale = max(max(max(oldDc.r, oldDc.g), max(oldDc.b, newDc.r)), max(max(newDc.g, newDc.b), 0.05));
    float h = history ? Blend.x : 0.0;
    if (change > Blend.z * scale) { h = min(h, Blend.y); }
    float outv[28];
    [unroll] for (uint m = 0; m < 27; ++m) { outv[m] = lerp(fresh[m], old[m], h); }
    outv[27] = state.x > Misc.x ? 0.0 : 1.0;
    [unroll] for (uint w = 0; w < 7; ++w)
    {
        g_probesOut[base + w] = float4(outv[w * 4 + 0], outv[w * 4 + 1], outv[w * 4 + 2], outv[w * 4 + 3]);
    }
}

// 距離の地図の内側 16x16 の 1 画素。render/gi/GiBaker.hpp の fillVisibilityTile と同じ重みで、履歴と混ぜる。
// このフレームのレイが 1 本も寄与しない画素は前の値のまま (遮る物なしへ寄せると壁の向こうの光を拾う)
float2 visibilityTexel(uint2 tile, uint texel, uint rays)
{
    uint tx = texel % 16, ty = texel / 16;
    float2 uv = (float2(tx, ty) + 0.5) / 16.0 * 2.0 - 1.0;
    float3 texDir = octDecode(uv);
    float wsum = 0.0, m1 = 0.0, m2 = 0.0;
    for (uint r = 0; r < rays; ++r)
    {
        float c = dot(texDir, gsDir[r]);
        if (c <= 0.0) { continue; }
        float w = pow(c, Misc.y);
        float d = min(abs(gsRay[r].a), Blend.w);
        wsum += w;
        m1 += w * d;
        m2 += w * d * d;
    }
    uint2 px = tile + 1 + uint2(tx, ty);
    bool history = (RayParams.w & kDdgiFlagVisHistory) != 0;
    float2 v;
    if (wsum > 1e-4) { v = lerp(float2(m1, m2) / wsum, g_visOut[px], history ? Blend.x : 0.0); }
    else { v = history ? g_visOut[px] : float2(Blend.w, Blend.w * Blend.w); }
    g_visOut[px] = v;
    return v;
}

// 縁の 1 画素に、八面体の折り返しの向こう側の内側の画素を写す (GiBaker.hpp の fillVisibilityBorder と同じ)
void visibilityBorder(uint2 tile, uint b)
{
    const uint I = 16;
    uint2 dst, src;
    if (b < 4 * I)
    {
        uint side = b / I, i = b % I + 1;
        if (side == 0) { dst = uint2(i, 0);     src = uint2(I + 1 - i, 1); }
        else if (side == 1) { dst = uint2(i, I + 1); src = uint2(I + 1 - i, I); }
        else if (side == 2) { dst = uint2(0, i);     src = uint2(1, I + 1 - i); }
        else { dst = uint2(I + 1, i); src = uint2(I, I + 1 - i); }
    }
    else
    {
        uint c = b - 4 * I;
        dst = uint2(c & 1 ? I + 1 : 0, c & 2 ? I + 1 : 0);
        src = uint2(c & 1 ? 1 : I, c & 2 ? 1 : I);
    }
    g_visOut[tile + dst] = gsVis[(src.y - 1) * I + (src.x - 1)];
}

[numthreads(DDGI_GROUP, 1, 1)]
void UpdateCS(uint3 gid : SV_GroupID, uint tid : SV_GroupIndex)
{
    uint probe = gid.x;
    uint rays = RayParams.x;
    for (uint r = tid; r < rays; r += DDGI_GROUP)
    {
        gsRay[r] = g_rays[probe * rays + r];
        gsDir[r] = rayDirection(r, rays);
    }
    GroupMemoryBarrierWithGroupSync();
    accumulateSh(tid, rays);
    uint tiles = GiDims.w;
    uint2 tile = uint2(probe % tiles, probe / tiles) * 18;
    for (uint texel = tid; texel < 256; texel += DDGI_GROUP) { gsVis[texel] = visibilityTexel(tile, texel, rays); }
    GroupMemoryBarrierWithGroupSync();
    blendIrradiance(probe, rays, tid);
    for (uint b = tid; b < 68; b += DDGI_GROUP) { visibilityBorder(tile, b); }
}
