#pragma once

/// @file DX12FXAAShaders.hpp
/// @brief DX12 用 FXAA 3.11 ピクセルシェーダー HLSL ソース
/// @details NVIDIA FXAA 3.11 アルゴリズムに基づく fast approximate anti-aliasing。
///          DX11 版 (`FXAAShader.hpp`) と HLSL ソースは等価。register binding は
///          DX12 root signature と整合する形 (t0 / s0 / b0)。
///
///          VS は `OUTLINE_POST_VS` (フルスクリーン三角形) を流用する。
///          ピクセルシェーダーのみここで定義する。

namespace mitiru::render
{

/// @brief FXAA 3.11 ピクセルシェーダー (DX12)
/// @details 6 ステップ:
///          1. 中心 + 4 近傍輝度
///          2. ローカルコントラストで早期 reject
///          3. 対角輝度 → 水平/垂直エッジ判定
///          4. エッジ垂直方向のステップサイズ決定
///          5. エッジ端点探索 (最大 12 ステップ)
///          6. サブピクセル AA をブレンド
inline constexpr const char* DX12_FXAA_PS_3D = R"hlsl(
Texture2D    sceneTexture  : register(t0);
SamplerState linearSampler : register(s0);

cbuffer FXAAParams : register(b0)
{
    float2 rcpFrame;          // 1.0 / screenSize
    float  subpixQuality;     // サブピクセル品質 (0.0-1.0)
    float  edgeThreshold;     // エッジ検出閾値
    float  edgeThresholdMin;  // 最小エッジ閾値
    float3 fxaaPadding;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float FxaaLuma(float3 rgb)
{
    return dot(rgb, float3(0.299, 0.587, 0.114));
}

float3 FxaaTexOff(float2 uv, float2 offset)
{
    return sceneTexture.Sample(linearSampler, uv + offset * rcpFrame).rgb;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float2 uv = input.TexCoord;

    // ── ステップ 1: 中心 + 4 近傍の輝度 ─────────────────────
    float3 rgbM = sceneTexture.Sample(linearSampler, uv).rgb;
    float lumaM = FxaaLuma(rgbM);
    float lumaN = FxaaLuma(FxaaTexOff(uv, float2( 0, -1)));
    float lumaS = FxaaLuma(FxaaTexOff(uv, float2( 0,  1)));
    float lumaE = FxaaLuma(FxaaTexOff(uv, float2( 1,  0)));
    float lumaW = FxaaLuma(FxaaTexOff(uv, float2(-1,  0)));

    // ── ステップ 2: 局所コントラストで reject ───────────────
    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    float lumaRange = lumaMax - lumaMin;

    if (lumaRange < max(edgeThresholdMin, lumaMax * edgeThreshold))
    {
        return float4(rgbM, 1.0);
    }

    // ── ステップ 3: 対角輝度を取り、水平/垂直エッジを判定 ────
    float lumaNW = FxaaLuma(FxaaTexOff(uv, float2(-1, -1)));
    float lumaNE = FxaaLuma(FxaaTexOff(uv, float2( 1, -1)));
    float lumaSW = FxaaLuma(FxaaTexOff(uv, float2(-1,  1)));
    float lumaSE = FxaaLuma(FxaaTexOff(uv, float2( 1,  1)));

    float lumaNS    = lumaN  + lumaS;
    float lumaEW    = lumaE  + lumaW;
    float lumaNWSW  = lumaNW + lumaSW;
    float lumaNESE  = lumaNE + lumaSE;
    float lumaNWNE  = lumaNW + lumaNE;
    float lumaSWSE  = lumaSW + lumaSE;

    float edgeHorz = abs(lumaNWSW - 2.0 * lumaW) +
                     abs(lumaNS   - 2.0 * lumaM) * 2.0 +
                     abs(lumaNESE - 2.0 * lumaE);
    float edgeVert = abs(lumaNWNE - 2.0 * lumaN) +
                     abs(lumaEW   - 2.0 * lumaM) * 2.0 +
                     abs(lumaSWSE - 2.0 * lumaS);
    bool isHorizontal = (edgeHorz >= edgeVert);

    // ── ステップ 4: ステップ方向 ─────────────────────────────
    float stepLength = isHorizontal ? rcpFrame.y : rcpFrame.x;
    float luma1 = isHorizontal ? lumaN : lumaW;
    float luma2 = isHorizontal ? lumaS : lumaE;
    float gradient1 = luma1 - lumaM;
    float gradient2 = luma2 - lumaM;
    bool is1Steepest = abs(gradient1) >= abs(gradient2);
    float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));
    if (!is1Steepest) stepLength = -stepLength;

    float lumaLocalAvg = is1Steepest
        ? 0.5 * (luma1 + lumaM)
        : 0.5 * (luma2 + lumaM);

    float2 currentUV = uv;
    if (isHorizontal) currentUV.y += stepLength * 0.5;
    else              currentUV.x += stepLength * 0.5;

    // ── ステップ 5: エッジ端点探索 (最大 12 ステップ) ────────
    float2 offset2 = isHorizontal
        ? float2(rcpFrame.x, 0.0)
        : float2(0.0, rcpFrame.y);
    float2 uv1 = currentUV - offset2;
    float2 uv2 = currentUV + offset2;

    float lumaEnd1 = FxaaLuma(sceneTexture.Sample(linearSampler, uv1).rgb)
                   - lumaLocalAvg;
    float lumaEnd2 = FxaaLuma(sceneTexture.Sample(linearSampler, uv2).rgb)
                   - lumaLocalAvg;

    bool reached1 = abs(lumaEnd1) >= gradientScaled;
    bool reached2 = abs(lumaEnd2) >= gradientScaled;
    bool reachedBoth = reached1 && reached2;

    if (!reached1) uv1 -= offset2;
    if (!reached2) uv2 += offset2;

    static const float QUALITY[12] = {
        1.0, 1.0, 1.0, 1.0, 1.0,
        1.5, 2.0, 2.0, 2.0, 2.0,
        4.0, 8.0
    };

    [unroll]
    for (int i = 0; i < 12 && !reachedBoth; i++)
    {
        if (!reached1)
        {
            lumaEnd1 = FxaaLuma(
                sceneTexture.Sample(linearSampler, uv1).rgb) - lumaLocalAvg;
        }
        if (!reached2)
        {
            lumaEnd2 = FxaaLuma(
                sceneTexture.Sample(linearSampler, uv2).rgb) - lumaLocalAvg;
        }
        reached1 = abs(lumaEnd1) >= gradientScaled;
        reached2 = abs(lumaEnd2) >= gradientScaled;
        reachedBoth = reached1 && reached2;

        if (!reached1) uv1 -= offset2 * QUALITY[i];
        if (!reached2) uv2 += offset2 * QUALITY[i];
    }

    // ── ステップ 6: ブレンド係数 ──────────────────────────────
    float dist1 = isHorizontal ? (uv.x - uv1.x) : (uv.y - uv1.y);
    float dist2 = isHorizontal ? (uv2.x - uv.x) : (uv2.y - uv.y);
    bool isDir1 = dist1 < dist2;
    float distFinal = min(dist1, dist2);
    float edgeLength = dist1 + dist2;
    float pixelOffset = -distFinal / max(edgeLength, 1e-5) + 0.5;

    bool isLumaCenterSmaller = lumaM < lumaLocalAvg;
    bool correctVariation =
        ((isDir1 ? lumaEnd1 : lumaEnd2) < 0.0) != isLumaCenterSmaller;
    float finalOffset = correctVariation ? pixelOffset : 0.0;

    // サブピクセル AA をブレンド
    float lumaAvg = (1.0 / 12.0) *
        (2.0 * lumaNS + 2.0 * lumaEW + lumaNWSW + lumaNESE);
    float subpixOffset1 = saturate(
        abs(lumaAvg - lumaM) / max(lumaRange, 1e-5));
    float subpixOffset2 = (-2.0 * subpixOffset1 + 3.0)
        * subpixOffset1 * subpixOffset1;
    float subpixOffsetFinal = subpixOffset2 * subpixOffset2 * subpixQuality;

    finalOffset = max(finalOffset, subpixOffsetFinal);

    // 最終サンプリング
    float2 finalUV = uv;
    if (isHorizontal) finalUV.y += finalOffset * stepLength;
    else              finalUV.x += finalOffset * stepLength;

    float3 finalColor = sceneTexture.Sample(linearSampler, finalUV).rgb;
    return float4(finalColor, 1.0);
}
)hlsl";

} // namespace mitiru::render
