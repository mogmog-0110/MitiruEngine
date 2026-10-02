#pragma once

/// @file DX12Tonemap.hpp
/// @brief HDR FP16 → LDR R8G8B8A8 のトーンマップ (中立の肩 + sRGB の符号化)
/// @details 主パスは線形の HDR (書いた色は GPU へ上げる所で線形にしてある、ColorSpace.hpp) を書く。
///          ここで 1 を超える明部を肩で 1 へ寄せ、sRGB へ戻して backbuffer に書く。
///          肩は線形 0.9 まで恒等なので、強さ 1 の白い光が正面から当たった面は書いた色のまま出る
///          (ACES のように中間調を持ち上げたり、成分ごとに潰して橙を黄へずらしたりしない)。
///
///          フローは:
///            scene (HDR FP16 MSAA) → ResolveSubresource → HDR intermediate
///              → applyTonemap (this) → backbuffer LDR
///              → outline post-process
///              → FXAA
///              → overlay 2D HUD

namespace mitiru::render
{

/// @brief Tonemap 用 VS。フルスクリーン三角形 (OUTLINE_POST_VS と同形)
constexpr const char* DX12_TONEMAP_VS = R"HLSL(
struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput output;
    output.TexCoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(output.TexCoord * float2(2, -2) + float2(-1, 1), 0, 1);
    return output;
}
)HLSL";

/// @brief Tonemap 用 PS。露出 → 中立の肩 → 彩度・コントラスト → sRGB
/// @details t1 は SSAO。AoOn のときだけ HDR 色に乗算してから露出に入る (LDR に掛けると肩で潰れた明部まで暗くなる)。
///          t2 は bloom (1/2 解像)。BloomOn のとき HDR 色に BloomStrength 倍して足してから露出に入る。
///          Gamma は sRGB に対する表示の補正で、既定の 2.2 で sRGB そのものになる。
constexpr const char* DX12_TONEMAP_PS = R"HLSL(
Texture2D<float4> g_hdr   : register(t0);
Texture2D<float>  g_ao    : register(t1);
Texture2D<float4> g_bloom : register(t2);
SamplerState      g_samp  : register(s0);

cbuffer CbTonemap : register(b0)
{
    float Exposure;   // EV stops を線形係数に変換した値 (default 1.0)
    float Gamma;      // 表示ガンマ (default 2.2 = sRGB)
    float AoOn;       // 1 なら g_ao を掛ける
    float BloomOn;    // 1 なら g_bloom を足す
    float BloomStrength;
    float Saturation; // 1 = 無変換、0 = グレー
    float Contrast;   // 1 = 無変換。中間灰 0.18 を軸に伸縮
    float _pad0;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

// Khronos PBR Neutral の肩 (線形 0.9 から 1 へ漸近する有理式) を最大成分に掛け、色全体を同じ比で縮める。
// 色相も彩度も保つ: 4 倍の赤い光は赤のまま、強い橙は橙のまま (成分ごとの曲線や白への寄せは橙を黄や白へずらす)
float3 neutralShoulder(float3 c)
{
    const float knee = 0.9;
    const float d = 1.0 - knee;
    float peak = max(c.r, max(c.g, c.b));
    if (peak <= knee) { return c; }
    float newPeak = 1.0 - d * d / (peak + d - knee);
    return c * (newPeak / peak);
}

float3 linearToSrgb(float3 x)
{
    return (x <= 0.0031308) ? x * 12.92 : 1.055 * pow(max(x, 0.0031308), 1.0 / 2.4) - 0.055;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 hdr = g_hdr.Sample(g_samp, input.TexCoord);
    if (AoOn > 0.5f) { hdr.rgb *= g_ao.Sample(g_samp, input.TexCoord); }
    if (BloomOn > 0.5f) { hdr.rgb += g_bloom.Sample(g_samp, input.TexCoord).rgb * BloomStrength; }

    float3 mapped = neutralShoulder(max(hdr.rgb * Exposure, 0.0f));

    // 色調補正。1.0 のときは式ごと恒等 (lerp(l, x, 1) = x、0.18 + (x - 0.18) * 1 = x)
    float luma = dot(mapped, float3(0.2126f, 0.7152f, 0.0722f));
    mapped = lerp(luma.xxx, mapped, Saturation);
    mapped = saturate(0.18f + (mapped - 0.18f) * Contrast);

    mapped = pow(mapped, 2.2f / max(Gamma, 1e-4f));
    return float4(linearToSrgb(mapped), hdr.a);
}
)HLSL";

/// @brief Tonemap CB (b0) レイアウト。HLSL 側と一致させる
struct alignas(16) TonemapCB
{
    float exposure      = 1.0f;
    float gamma         = 2.2f;
    float aoOn          = 0.0f;
    float bloomOn       = 0.0f;
    float bloomStrength = 0.3f;
    float saturation    = 1.0f;
    float contrast      = 1.0f;
    float _pad1         = 0.0f;
};

static_assert(sizeof(TonemapCB) == 32,
    "TonemapCB byte size mismatch — HLSL CB layout will break");

} // namespace mitiru::render
