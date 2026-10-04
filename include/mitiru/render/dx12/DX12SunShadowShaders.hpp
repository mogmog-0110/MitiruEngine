#pragma once

/// @file DX12SunShadowShaders.hpp
/// @brief 太陽のカスケードの影を引く HLSL (余白、PCF、カスケードの選び方)
/// @details DX12_LIT_COMMON_HLSL (CbShadow、g_shadow、g_shadowFar、g_pcf) の後に連結する。
///          clod の resolve (tools/clod_shaders/clod_engine.hlsl) も同じ文字列を generate_blobs.py が取り出して取り込む。
///          ここを変えたら blob を作り直す (作り直し忘れは TestClodIndirectLighting の hash の試験が見つける)。

namespace mitiru::render
{

inline constexpr const char* DX12_SUN_SHADOW_HLSL = R"hlsl(
// 影の比較の余白。タップを広げた分と、受ける面が光に傾いた分 (タップ間の深度差 = 間隔 × tan) だけ伸ばす
float shadowBiasFor(float3 N, float3 L)
{
    if (ShadowBiasNdc <= 0.0) { return 0.001 * max(ShadowSoftness, 1.0); }
    float c = max(saturate(dot(N, L)), 0.1);
    float t = min(sqrt(1.0 - c * c) / c, 6.0);
    return ShadowBiasNdc * (1.0 + max(ShadowSoftness, 1.0) * t);
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
    const float texelSize = ShadowSoftness / 1024.0;
    return pcfFilter(shadowTex, uv, depthRef, float2(texelSize, texelSize), -1.0e9, 1.0e9);
}

// g_shadowFar は 2 列のアトラス (左 = カスケード1、右 = カスケード2)。PCF のタップは列の内側に収める
float samplePCFAtlas(float3 ndc, float column, float bias)
{
    float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    float depthRef = ndc.z - bias;
    if (any(uv < 0) || any(uv > 1) || ndc.z < 0.0 || ndc.z > 1.0) return 1.0;
    const float texelV = ShadowSoftness / 1024.0;
    const float texelU = texelV * 0.5;
    const float uMin = column * 0.5 + texelU * 0.5;
    const float uMax = (column + 1.0) * 0.5 - texelU * 0.5;
    return pcfFilter(g_shadowFar, float2((uv.x + column) * 0.5, uv.y), depthRef, float2(texelU, texelV), uMin, uMax);
}

// カメラ距離で使うカスケードを選ぶ。カスケード無効時は CPU が分割距離を非常に大きくするので常にカスケード0
float sampleCascadedShadow(float3 worldPos, float4 lightSpacePos0, float distanceFromCamera, float bias)
{
    if (distanceFromCamera < CascadeSplitDistance)
    {
        float3 ndc = lightSpacePos0.xyz / max(lightSpacePos0.w, 1e-4);
        return samplePCFTex(g_shadow, ndc, bias);
    }
    if (distanceFromCamera < CascadeSplitDistance2)
    {
        float4 lsFar = mul(LightViewProjFar, float4(worldPos, 1.0));
        return samplePCFAtlas(lsFar.xyz / max(lsFar.w, 1e-4), 0.0, bias);
    }
    float4 lsFar2 = mul(LightViewProjFar2, float4(worldPos, 1.0));
    return samplePCFAtlas(lsFar2.xyz / max(lsFar2.w, 1e-4), 1.0, bias);
}
)hlsl";

} // namespace mitiru::render
