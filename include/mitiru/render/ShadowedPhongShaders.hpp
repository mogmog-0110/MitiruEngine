#pragma once

/// @file ShadowedPhongShaders.hpp
/// @brief シャドウマップ対応の最小 3D シェーダー (HLSL SM5.0)
///
/// @details `ShadowPass3D` が出力するシャドウマップ SRV を t0 にバインドし、
///          comparison sampler s0 で深度比較するパターン。
///          頂点入力は POSITION のみ、法線・UV は要求しない (3-A.1.b スコープ最小化)。
///          色情報も「ライト時 = 明るい灰、シャドウ時 = 暗い灰」のフラット表示で、
///          シャドウ統合の視覚確認に必要十分。Phong 完全実装は将来の sub-task。
///
/// ## cbuffer レイアウト
/// - b0: cameraViewProj  (4x4)。カメラの VP
/// - b1: lightViewProj   (4x4)。シャドウパスの VP
/// - t0: shadowMap       Texture2D<float>
/// - s0: shadowSampler   SamplerComparisonState (LESS_EQUAL)
///
/// 行列は upload 前に転置してから渡す前提 (HLSL column-major 規約)。

namespace mitiru::render
{

/// @brief シャドウ付き Phong (簡易版) 頂点シェーダー
constexpr const char* SHADOWED_VS_3D = R"hlsl(
cbuffer CameraVP : register(b0)
{
    float4x4 cameraViewProj;
};

cbuffer LightVP : register(b1)
{
    float4x4 lightViewProj;
};

struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 clipPos    : SV_POSITION;
    float4 shadowPos  : SHADOWPOS;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    float4 worldPos = float4(input.position, 1.0);
    // 行列は転置済みで upload されるため HLSL では mul(mat, col_vec) = sgc * vec
    o.clipPos   = mul(cameraViewProj, worldPos);
    o.shadowPos = mul(lightViewProj, worldPos);
    return o;
}
)hlsl";

/// @brief シャドウ付き Phong (簡易版) ピクセルシェーダー
/// @details ライト面 = 明るい灰、シャドウ面 = 暗い灰。バイアスは 0.005 固定。
constexpr const char* SHADOWED_PS_3D = R"hlsl(
Texture2D<float>           shadowMap     : register(t0);
SamplerComparisonState     shadowSampler : register(s0);

struct PSInput
{
    float4 clipPos   : SV_POSITION;
    float4 shadowPos : SHADOWPOS;
};

float4 PSMain(PSInput input) : SV_TARGET
{
    // shadowPos.xyz / shadowPos.w で NDC→[0,1] テクスチャ座標へ変換
    float3 shadowNdc = input.shadowPos.xyz / input.shadowPos.w;
    float2 shadowUv  = shadowNdc.xy * float2(0.5, -0.5) + float2(0.5, 0.5);
    float  receiverZ = shadowNdc.z - 0.005; // バイアス

    // テクスチャ範囲外はライト扱い
    float lit = 1.0;
    if (shadowUv.x >= 0.0 && shadowUv.x <= 1.0 &&
        shadowUv.y >= 0.0 && shadowUv.y <= 1.0)
    {
        lit = shadowMap.SampleCmpLevelZero(shadowSampler, shadowUv, receiverZ);
    }

    // 視覚スポットチェック用に強コントラスト: シャドウ=黒、ライト=白
    // (将来 Phong 拡張時に置き換える)
    return float4(lit, lit, lit, 1.0);
}
)hlsl";

} // namespace mitiru::render
