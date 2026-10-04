#pragma once

/// @file DX12LightingProbeShaders.hpp
/// @brief 前方の描画の PS が使う間接光の HLSL。焼いた放射照度のプローブ、箱で映す反射のプローブ、画面の反射 (SSR)
/// @details DX12_LIT_COMMON_HLSL (CbCluster の Gi* / Refl* / Ssr* と g_sampClamp) の後に連結する。
///          放射照度の引き方は render/gi/ProbeVolume.hpp の sampleIrradiance と同じ式。
///          SSR は前のフレームの深度の最小値の階層 (HZB) を前のフレームの画面で辿り、当たった所の前のフレームの色を返す。
///          clod の resolve (tools/clod_shaders/clod_engine.hlsl) も同じ文字列を generate_blobs.py が取り出して取り込む。
///          ここを変えたら blob を作り直す (作り直し忘れは TestClodIndirectLighting の hash の試験が見つける)。

namespace mitiru::render
{

inline constexpr const char* DX12_LIGHTING_PROBES_HLSL = R"hlsl(
StructuredBuffer<float4> g_giProbes     : register(t39);
Texture2D<float2>        g_giVisibility : register(t40);
TextureCubeArray         g_reflProbes   : register(t41);
Texture2D<float>         g_ssrHzb       : register(t42);
Texture2D                g_ssrColor     : register(t43);

bool giEnabled() { return GiOrigin.w > 0.5; }

// 1 プローブは float4 7 個: 係数 k の rgb が先頭から 3 個ずつ、最後の w が有効かどうか
float3 giProbeIrradiance(uint probe, float3 n)
{
    uint b = probe * 7;
    float4 a0 = g_giProbes[b + 0], a1 = g_giProbes[b + 1], a2 = g_giProbes[b + 2], a3 = g_giProbes[b + 3];
    float4 a4 = g_giProbes[b + 4], a5 = g_giProbes[b + 5], a6 = g_giProbes[b + 6];
    float3 e = a0.xyz * 0.282095
             + float3(a0.w, a1.xy) * (0.488603 * n.y) + float3(a1.zw, a2.x) * (0.488603 * n.z) + a2.yzw * (0.488603 * n.x)
             + a3.xyz * (1.092548 * n.x * n.y) + float3(a3.w, a4.xy) * (1.092548 * n.y * n.z)
             + float3(a4.zw, a5.x) * (0.315392 * (3.0 * n.z * n.z - 1.0)) + a5.yzw * (1.092548 * n.x * n.z)
             + a6.xyz * (0.546274 * (n.x * n.x - n.y * n.y));
    return max(e, 0.0);
}

float2 giOctEncode(float3 n)
{
    float2 uv = float2(n.x, n.z) / (abs(n.x) + abs(n.y) + abs(n.z));
    if (n.y < 0.0) { uv = (1.0 - abs(uv.yx)) * float2(uv.x >= 0.0 ? 1.0 : -1.0, uv.y >= 0.0 ? 1.0 : -1.0); }
    return uv;
}

// プローブから dir の向きの面までの距離の (平均, 2 乗の平均)。1 枚は縁を含めて 10 画素
float2 giVisibility(uint probe, float3 dir)
{
    uint tiles = GiDims.w;
    float2 tile = float2(probe % tiles, probe / tiles) * 10.0;
    float2 px = tile + 1.0 + (giOctEncode(dir) * 0.5 + 0.5) * 8.0;
    return g_giVisibility.SampleLevel(g_sampClamp, px * GiParams.zw, 0);
}

// 周りの 8 個のプローブを三線形で混ぜ、壁の向こうのプローブはチェビシェフの不等式で落とす。混ぜられなければ ok = false
float3 giIrradiance(float3 P, float3 N, float3 V, out bool ok)
{
    float3 biased = P + (N * 0.2 + V * 0.8) * GiParams.x;
    int3 dims = int3(GiDims.xyz);
    float3 c = clamp((biased - GiOrigin.xyz) / GiSpacing.xyz, 0.0, float3(dims - 1));
    int3 base = min(int3(floor(c)), max(dims - 2, 0));
    float3 f = saturate(c - float3(base));
    float3 sum = 0.0;
    float wsum = 0.0;
    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        int3 off = int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        int3 cc = min(base + off, dims - 1);
        uint idx = (uint)(cc.x + dims.x * (cc.y + dims.y * cc.z));
        if (g_giProbes[idx * 7 + 6].w < 0.5) { continue; }
        float3 probeP = GiOrigin.xyz + float3(cc) * GiSpacing.xyz;
        float3 tri = lerp(1.0 - f, f, float3(off));
        float wrap = (dot(normalize(probeP - P + 1e-6), N) + 1.0) * 0.5;
        float w = wrap * wrap + 0.2;
        float3 toPoint = biased - probeP;
        float r = length(toPoint);
        float2 m = giVisibility(idx, r > 1e-4 ? toPoint / r : float3(0.0, 1.0, 0.0));
        if (r > m.x)
        {
            float var = abs(m.y - m.x * m.x);
            float d = r - m.x;
            float ch = var / (var + d * d);
            w *= ch * ch * ch;
        }
        if (w < 0.2) { w *= w * w * 25.0; }
        w *= tri.x * tri.y * tri.z;
        sum += giProbeIrradiance(idx, N) * w;
        wsum += w;
    }
    ok = wsum > 1e-6;
    return ok ? sum / wsum : 0.0;
}

// トゥーン: 間接光の明るさを段に刻む。明るさ l を l / (l + 1) で 0..1 に畳んでから段の数を掛けた座標で等分する。
// 刻まない時 (段が 1 以下か暗すぎる) は負を返す
float giToonCoord(float3 e)
{
    float bands = GiParams.y;
    float l = max(max(e.r, e.g), e.b);
    return (bands < 1.5 || l <= 1e-5) ? -1.0 : l / (l + 1.0) * bands;
}

// x = giToonCoord(e) の段へ刻む。段の境は w (隣の画素との x の差) の幅で滑らかにする
float3 giToonBandAt(float3 e, float x, float w)
{
    if (x < 0.0) { return e; }
    float bands = GiParams.y;
    float l = max(max(e.r, e.g), e.b);
    w = max(w, 1e-4);
    float q = (floor(x) + smoothstep(1.0 - w, 1.0, frac(x)) + 0.5) / bands;
    float lq = q / max(1.0 - q, 1e-3);
    return e * (lq / l);
}

float3 giToonBand(float3 e)
{
    float x = giToonCoord(e);
    return giToonBandAt(e, x, fwidth(x));
}

// 拡散の間接光 (反射率を掛ける前の、放射照度 / pi に強さを掛けたもの)。焼いた光が無いか混ぜられなければ fallback
float3 giDiffuse(float3 P, float3 N, float3 V, float3 fallback)
{
    if (!giEnabled()) { return fallback; }
    bool ok;
    float3 e = giIrradiance(P, N, V, ok);
    return ok ? e * (GiSpacing.w / PI) : fallback;
}

// 箱で映す反射のプローブ。箱の中 (面の上を含む) は重み 1、箱の外は blend の距離で 0 へ落とし、重みで混ぜる。
// a = 重みの和 (0..1)
float4 reflectionProbes(float3 P, float3 R, float roughness)
{
    uint count = (uint)ReflParams.x;
    if (count == 0) { return 0.0; }
    float3 sum = 0.0;
    float wsum = 0.0;
    [loop]
    for (uint i = 0; i < count; ++i)
    {
        float3 lo = ReflBoxMin[i].xyz;
        float3 hi = ReflBoxMax[i].xyz;
        float3 out3 = max(max(lo - P, P - hi), 0.0);
        float w = saturate(1.0 - max(out3.x, max(out3.y, out3.z)) / max(ReflBoxMin[i].w, 1e-3));
        if (w <= 0.0) { continue; }
        float3 tFar = max((hi - P) / R, (lo - P) / R);
        float dist = max(min(tFar.x, min(tFar.y, tFar.z)), 0.0);
        float3 dir = P + R * dist - ReflPos[i].xyz;
        sum += g_reflProbes.SampleLevel(g_sampClamp, float4(dir, ReflPos[i].w), roughness * ReflParams.y).rgb * w;
        wsum += w;
    }
    if (wsum <= 0.0) { return 0.0; }
    return float4(sum / wsum * ReflParams.z, saturate(wsum));
}

// 前フレームの射影の深度を、目からの距離へ戻す
float ssrLinearDepth(float d) { return SsrDepth.y / (d + SsrDepth.x); }

// 画素とフレームで決まる 0..1 の値 (interleaved gradient noise)
float ssrNoise(float2 pix)
{
    pix += SsrRay.z * float2(47.0, 17.0);
    return frac(52.9829189 * frac(dot(pix, float2(0.06711056, 0.00583715))));
}

// GGX の重点標本で選んだ微小面で反射させた向き
float3 ssrGgxReflect(float3 N, float3 V, float roughness, float2 xi)
{
    float a = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float cosT = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinT = sqrt(saturate(1.0 - cosT * cosT));
    float3 up = abs(N.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 tx = normalize(cross(up, N));
    float3 ty = cross(N, tx);
    float3 H = tx * (cos(phi) * sinT) + ty * (sin(phi) * sinT) + N * cosT;
    return reflect(-V, H);
}

// 世界の点を前フレームの画面へ (x, y = HZB の段 0 の画素、z = 深度)
float3 ssrToPrevScreen(float4 clip)
{
    float2 ndc = clip.xy / clip.w;
    return float3((ndc * float2(0.5, -0.5) + 0.5) * SsrScreen.xy, clip.z / clip.w);
}

// 升 cell (段 level) から、o + d t が出る t
float ssrCellExit(float3 o, float3 d, int2 cell, float cellSize)
{
    float2 edge = (float2(cell) + step(0.0, d.xy)) * cellSize;
    // 成分ごとの三項演算子で書く (clod の DXC は HLSL 2021 で、ベクトルの条件の三項演算子を受けない)
    float2 dd = float2(abs(d.x) > 1e-6 ? d.x : 1e-6, abs(d.y) > 1e-6 ? d.y : 1e-6);
    float2 tb = (edge - o.xy) / dd;
    return min(abs(d.x) > 1e-6 ? tb.x : 1e9, abs(d.y) > 1e-6 ? tb.y : 1e9);
}

// HZB を辿る。当たれば true と、当たった画面の位置 hit (画素) とレイの進んだ割合 progress。
// 升の一番手前の深度より前にいる間は升ごと飛ばして段を粗くし、升の中でその深度に届くなら届く所まで進めて段を細かくする
bool ssrMarch(float3 o, float3 d, float len, float noise, out float3 hit, out float progress)
{
    int maxLevel = (int)SsrScreen.z - 1;
    int level = 0;
    float t = 1.0 + noise;
    bool wasInFront = false;
    hit = o;
    progress = 0.0;
    [loop]
    for (int i = 0; i < (int)SsrRay.y && t < len; ++i)
    {
        float3 p = o + d * t;
        if (any(p.xy < 0.0) || any(p.xy >= SsrScreen.xy)) { return false; }
        float cellSize = exp2((float)level);
        int2 mipSize = max(int2(SsrScreen.xy) >> level, int2(1, 1));
        int2 cell = min(int2(p.xy / cellSize), mipSize - 1);
        float zMin = g_ssrHzb.Load(int3(cell, level));
        float tExit = max(ssrCellExit(o, d, cell, cellSize), t);
        bool behind = p.z >= zMin;
        if (!behind)
        {
            wasInFront = true;
            float tz = d.z > 0.0 ? (zMin - o.z) / d.z : 1e9;
            if (tz >= tExit)
            {
                t = tExit + 0.02;
                level = min(level + 1, maxLevel);
                continue;
            }
            t = max(tz, t);
            p = o + d * t;
        }
        if (level > 0)
        {
            level -= 1;
            continue;
        }
        // 一度も面の手前に出ていないレイは、出発した面そのものに当たっている
        if (wasInFront && ssrLinearDepth(p.z) - ssrLinearDepth(zMin) < SsrParams.z)
        {
            hit = p;
            progress = t / len;
            return true;
        }
        t = tExit + 0.02;
    }
    return false;
}

// 向き R の画面の反射。N は面の向き (R がこの面の下へ潜るなら辿らない)。rgb = 映る色 x 強さ、
// a = 確からしさ (0 = 外れ。呼び出し側はプローブか IBL のまま)
float4 traceSsrRay(float3 P, float3 N, float3 R, float roughness, float noise)
{
    if (dot(R, N) <= 0.02) { return 0.0; }
    // 出発点を面から目の距離に比例して浮かせ、深度の丸めで自分の面に当たらないようにする
    float3 start = P + N * (0.01 + 0.002 * length(CameraPos - P));
    float4 c0 = mul(SsrPrevViewProj, float4(start, 1.0));
    float4 c1 = mul(SsrPrevViewProj, float4(start + R * SsrRay.x, 1.0));
    if (c0.w <= SsrDepth.z) { return 0.0; }
    if (c1.w < SsrDepth.z) { c1 = lerp(c0, c1, (c0.w - SsrDepth.z) / (c0.w - c1.w) * 0.99); }
    float3 o = ssrToPrevScreen(c0);
    float3 e = ssrToPrevScreen(c1);
    float3 d = e - o;
    float len = max(abs(d.x), abs(d.y));
    if (len < 1.0) { return 0.0; }
    float3 hit;
    float progress;
    if (!ssrMarch(o, d / len, len, noise, hit, progress)) { return 0.0; }
    float2 uv = hit.xy / SsrScreen.xy;
    float2 border = saturate(min(uv, 1.0 - uv) * 12.0);
    float fade = border.x * border.y * saturate((1.0 - progress) * 4.0);
    fade *= saturate((SsrParams.y - roughness) / max(SsrParams.y * 0.4, 1e-3));
    float mip = clamp(log2(max(progress * len * roughness * 0.5, 1.0)), 0.0, SsrScreen.w - 1.0);
    float3 color = g_ssrColor.SampleLevel(g_sampClamp, uv, mip).rgb;
    return float4(color * SsrParams.w, fade);
}

// 粗さ roughness の面 (法線 N、目への向き V) の画面の反射。TAA の間は粗さの分だけ向きを散らし、履歴で均す
float4 traceSsr(float3 P, float3 N, float3 V, float roughness, float2 svPos)
{
    if (SsrParams.x < 0.5 || roughness > SsrParams.y) { return 0.0; }
    float noise = ssrNoise(svPos);
    float3 R = reflect(-V, N);
    if (SsrRay.w > 0.5 && roughness > 0.05) { R = ssrGgxReflect(N, V, roughness, float2(noise, frac(noise * 7.13 + 0.37))); }
    return traceSsrRay(P, N, R, roughness, noise);
}

// 粗さ r の鏡面の環境の BRDF (A, B) の近似 (Karis 2014、IBL の表が無い時に使う)
float2 envBrdfApprox(float NdotV, float r)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 k = r * c0 + c1;
    float a004 = min(k.x * k.x, exp2(-9.28 * NdotV)) * k.x + k.y;
    return float2(-1.04, 1.04) * a004 + k.zw;
}
)hlsl";

} // namespace mitiru::render
