#pragma once

/// @file DX12SunShadowShaders.hpp
/// @brief 太陽のカスケードの影を引く HLSL (余白、PCF、カスケードの選び方)
/// @details DX12_LIT_COMMON_HLSL (CbShadow、g_shadow、g_shadowFar、g_pcf) の後に連結する。
///          clod の resolve (tools/clod_shaders/clod_engine.hlsl) も同じ文字列を generate_blobs.py が取り出して取り込む。
///          ここを変えたら blob を作り直す (作り直し忘れは TestClodIndirectLighting の hash の試験が見つける)。

namespace mitiru::render
{

inline constexpr const char* DX12_SUN_SHADOW_HLSL = R"hlsl(
// 受ける面が光に傾いた分 (タップ間の深度差 = 間隔 × tan)。真横に近い面で余白が際限なく伸びないよう 6 で止める
float shadowSlope(float3 N, float3 L)
{
    float c = max(saturate(dot(N, L)), 0.1);
    return min(sqrt(1.0 - c * c) / c, 6.0);
}

// カスケード (光の view * proj が vp、影マップの 1 texel が texel01) で比べる位置 ndc と余白 bias。
// 既定 (ShadowBiasNdc が 0) は余白を影マップの 1 texel の世界の幅に比例させる。点を法線の向きへ 1 texel 浮かせ、
// 深度は PCF の広がりと傾きの分だけ手前で比べる。固定の余白は影の細かいカスケードほど影を物から浮かせる
void shadowLookup(float4x4 vp, float texel01, float3 P, float3 N, float slope, out float3 ndc, out float bias)
{
    float soft = max(ShadowSoftness, 1.0);
    float3 q = P;
    if (ShadowBiasNdc > 0.0) { bias = ShadowBiasNdc * (1.0 + soft * slope); }
    else
    {
        float texelWorld = 2.0 * texel01 / max(length(vp[0].xyz), 1e-6);
        q = P + N * texelWorld;
        bias = length(vp[2].xyz) * texelWorld * (0.5 + soft * slope);
    }
    float4 c = mul(vp, float4(q, 1.0));
    ndc = c.xyz / max(c.w, 1e-4);
}

// softness 1 までは 3x3 を softness texel おき。広いときは 5x5 のテント重みで softness / 2 おきに埋める
// (3x3 のまま広げると双線形の比較の間が空き、縁が 3 段に分かれる)。u は [uMin, uMax] に収める
float pcfFilter(Texture2D shadowTex, float2 uv, float depthRef, float2 texel, float uMin, float uMax)
{
    float shadow = 0.0;
    if (ShadowSoftness <= 1.0)
    {
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            [unroll]
            for (int x = -1; x <= 1; ++x)
            {
                float2 tap = float2(clamp(uv.x + x * texel.x, uMin, uMax), uv.y + y * texel.y);
                shadow += shadowTex.SampleCmpLevelZero(g_pcf, tap, depthRef);
            }
        }
        return shadow / 9.0;
    }
    const float2 stepUv = texel * 0.5;
    [unroll]
    for (int ty = -2; ty <= 2; ++ty)
    {
        [unroll]
        for (int tx = -2; tx <= 2; ++tx)
        {
            float2 tap = float2(clamp(uv.x + tx * stepUv.x, uMin, uMax), uv.y + ty * stepUv.y);
            shadow += (3.0 - abs(tx)) * (3.0 - abs(ty)) * shadowTex.SampleCmpLevelZero(g_pcf, tap, depthRef);
        }
    }
    return shadow / 81.0;
}

float samplePCFTex(Texture2D shadowTex, float3 ndc, float bias)
{
    float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    float depthRef = ndc.z - bias;
    // 光の錐台の外は影なし。奥行きも見る (遠方クリップ面の外は影マップに何も無い)
    if (any(uv < 0) || any(uv > 1) || ndc.z < 0.0 || ndc.z > 1.0) return 1.0;
    const float texelSize = ShadowSoftness * ShadowTexel.x;
    return pcfFilter(shadowTex, uv, depthRef, float2(texelSize, texelSize), -1.0e9, 1.0e9);
}

// g_shadowFar は 2 列のアトラス (左 = カスケード1、右 = カスケード2)。PCF のタップは列の内側に収める
float samplePCFAtlas(float3 ndc, float column, float bias)
{
    float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    float depthRef = ndc.z - bias;
    if (any(uv < 0) || any(uv > 1) || ndc.z < 0.0 || ndc.z > 1.0) return 1.0;
    const float texelV = ShadowSoftness * ShadowTexel.y;
    const float texelU = texelV * 0.5;
    const float uMin = column * 0.5 + texelU * 0.5;
    const float uMax = (column + 1.0) * 0.5 - texelU * 0.5;
    return pcfFilter(g_shadowFar, float2((uv.x + column) * 0.5, uv.y), depthRef, float2(texelU, texelV), uMin, uMax);
}

// カメラ距離で使うカスケードを選ぶ。カスケード無効時は CPU が分割距離を非常に大きくするので常にカスケード0。
// N は受ける面の向き、L は光へ向かう向き
float sampleCascadedShadow(float3 worldPos, float3 N, float3 L, float distanceFromCamera)
{
    float slope = shadowSlope(N, L);
    float3 ndc;
    float bias;
    if (distanceFromCamera < CascadeSplitDistance)
    {
        shadowLookup(LightViewProj, ShadowTexel.x, worldPos, N, slope, ndc, bias);
        return samplePCFTex(g_shadow, ndc, bias);
    }
    if (distanceFromCamera < CascadeSplitDistance2)
    {
        shadowLookup(LightViewProjFar, ShadowTexel.y, worldPos, N, slope, ndc, bias);
        return samplePCFAtlas(ndc, 0.0, bias);
    }
    shadowLookup(LightViewProjFar2, ShadowTexel.y, worldPos, N, slope, ndc, bias);
    return samplePCFAtlas(ndc, 1.0, bias);
}
)hlsl";

} // namespace mitiru::render
