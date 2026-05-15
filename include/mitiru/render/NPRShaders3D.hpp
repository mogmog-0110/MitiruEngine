#pragma once

/// @file NPRShaders3D.hpp
/// @brief NPR（非写実的レンダリング）シェーダー集
/// @details 3Dを2Dイラスト風に見せる各種ピクセルシェーダーを提供する。
///          全て同じVS（DEFAULT_VS_3D / TOON_VS_3D）と同じCbLightingレイアウトを使用。

namespace mitiru::render
{

/// @brief フラット＋アウトライン — 完全にフラットな色（グラデーションなし）
/// @details 最も2Dに近い表現。NdotLを2値化して明暗のみ。
constexpr const char* FLAT_PS_3D = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;    float _pad0;
    float3 LightColor;  float _pad1;
    float3 AmbientColor; float _pad2;
    float3 CameraPos;   float _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float MaterialShininess;
    float3 _pad4;
};
struct PSInput
{
    float4 Position  : SV_POSITION;
    float3 WorldPos  : TEXCOORD0;
    float3 WorldNorm : TEXCOORD1;
    float2 TexCoord  : TEXCOORD2;
    float4 Color     : COLOR0;
};
float4 PSMain(PSInput input) : SV_TARGET
{
    float3 N = normalize(input.WorldNorm);
    float3 L = normalize(-LightDir);
    float NdotL = max(dot(N, L), 0.0);

    // 完全2値: 明か暗か
    float shade = (NdotL > 0.3) ? 1.0 : 0.55;
    float3 color = MaterialDiffuse.rgb * shade;

    return float4(color * input.Color.rgb, MaterialDiffuse.a * input.Color.a);
}
)hlsl";

/// @brief ポスタライズ — 色階調を制限（ポスター調）
constexpr const char* POSTERIZE_PS_3D = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;    float _pad0;
    float3 LightColor;  float _pad1;
    float3 AmbientColor; float _pad2;
    float3 CameraPos;   float _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float MaterialShininess;
    float3 _pad4;
};
struct PSInput
{
    float4 Position  : SV_POSITION;
    float3 WorldPos  : TEXCOORD0;
    float3 WorldNorm : TEXCOORD1;
    float2 TexCoord  : TEXCOORD2;
    float4 Color     : COLOR0;
};
float4 PSMain(PSInput input) : SV_TARGET
{
    float3 N = normalize(input.WorldNorm);
    float3 L = normalize(-LightDir);
    float3 V = normalize(CameraPos - input.WorldPos);

    float3 ambient = AmbientColor * MaterialDiffuse.rgb;
    float NdotL = max(dot(N, L), 0.0);
    float3 diffuse = LightColor * MaterialDiffuse.rgb * NdotL;
    float3 raw = ambient + diffuse;

    // 4段階にポスタライズ
    float levels = 4.0;
    raw = floor(raw * levels + 0.5) / levels;

    return float4(raw * input.Color.rgb, MaterialDiffuse.a * input.Color.a);
}
)hlsl";

/// @brief ハーフトーン — ドットパターン（コミック/印刷風）
constexpr const char* HALFTONE_PS_3D = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;    float _pad0;
    float3 LightColor;  float _pad1;
    float3 AmbientColor; float _pad2;
    float3 CameraPos;   float _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float MaterialShininess;
    float3 _pad4;
};
struct PSInput
{
    float4 Position  : SV_POSITION;
    float3 WorldPos  : TEXCOORD0;
    float3 WorldNorm : TEXCOORD1;
    float2 TexCoord  : TEXCOORD2;
    float4 Color     : COLOR0;
};
float4 PSMain(PSInput input) : SV_TARGET
{
    float3 N = normalize(input.WorldNorm);
    float3 L = normalize(-LightDir);

    float NdotL = max(dot(N, L), 0.0);
    float brightness = NdotL * 0.7 + 0.3;

    // スクリーン空間のドットパターン
    float2 screenUV = input.Position.xy;
    float dotSize = 6.0;
    float2 cell = fmod(screenUV, dotSize) / dotSize - 0.5;
    float dist = length(cell);
    float dotThreshold = (1.0 - brightness) * 0.5;
    float pattern = (dist < dotThreshold) ? 0.4 : 1.0;

    float3 color = MaterialDiffuse.rgb * pattern;
    return float4(color * input.Color.rgb, MaterialDiffuse.a * input.Color.a);
}
)hlsl";

/// @brief ハッチング — 斜線描画（ペン画風）
constexpr const char* HATCHING_PS_3D = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;    float _pad0;
    float3 LightColor;  float _pad1;
    float3 AmbientColor; float _pad2;
    float3 CameraPos;   float _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float MaterialShininess;
    float3 _pad4;
};
struct PSInput
{
    float4 Position  : SV_POSITION;
    float3 WorldPos  : TEXCOORD0;
    float3 WorldNorm : TEXCOORD1;
    float2 TexCoord  : TEXCOORD2;
    float4 Color     : COLOR0;
};
float4 PSMain(PSInput input) : SV_TARGET
{
    float3 N = normalize(input.WorldNorm);
    float3 L = normalize(-LightDir);

    float NdotL = max(dot(N, L), 0.0);
    float brightness = NdotL * 0.6 + 0.4;

    // スクリーン空間の斜線パターン
    float2 screenUV = input.Position.xy;

    // 明るさに応じてハッチング密度を変える
    float hatch1 = fmod(screenUV.x + screenUV.y, 4.0) < 1.0 ? 0.0 : 1.0;
    float hatch2 = fmod(screenUV.x - screenUV.y, 6.0) < 1.0 ? 0.0 : 1.0;
    float hatch3 = fmod(screenUV.x + screenUV.y * 0.5, 3.0) < 0.8 ? 0.0 : 1.0;

    float pattern = 1.0;
    if (brightness < 0.3)
        pattern = hatch1 * hatch2 * hatch3; // 3層ハッチング（暗い）
    else if (brightness < 0.55)
        pattern = hatch1 * hatch2;          // 2層（中間）
    else if (brightness < 0.8)
        pattern = hatch1;                   // 1層（やや明）

    // ベース色 + ハッチングパターン
    float3 paperColor = float3(0.95, 0.92, 0.88); // 紙の色
    float3 inkColor = MaterialDiffuse.rgb * 0.15;  // インクの色
    float3 color = lerp(inkColor, paperColor, pattern);

    return float4(color, 1.0);
}
)hlsl";

/// @brief グラデーションマップ — ライティングをカスタムカラーグラデーションにマップ
/// @details 暖色→寒色のグラデーションで絵本風の表現
constexpr const char* GRADIENT_MAP_PS_3D = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;    float _pad0;
    float3 LightColor;  float _pad1;
    float3 AmbientColor; float _pad2;
    float3 CameraPos;   float _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float MaterialShininess;
    float3 _pad4;
};
struct PSInput
{
    float4 Position  : SV_POSITION;
    float3 WorldPos  : TEXCOORD0;
    float3 WorldNorm : TEXCOORD1;
    float2 TexCoord  : TEXCOORD2;
    float4 Color     : COLOR0;
};
float4 PSMain(PSInput input) : SV_TARGET
{
    float3 N = normalize(input.WorldNorm);
    float3 L = normalize(-LightDir);

    float NdotL = max(dot(N, L), 0.0) * 0.8 + 0.2;

    // 暖色（影）→ 素材色（中間）→ 明るい暖色（ハイライト）
    float3 shadowColor = MaterialDiffuse.rgb * float3(0.4, 0.3, 0.5);  // 紫がかった影
    float3 midColor = MaterialDiffuse.rgb;
    float3 highlightColor = MaterialDiffuse.rgb * float3(1.1, 1.05, 0.9); // 暖色ハイライト

    float3 color;
    if (NdotL < 0.5)
        color = lerp(shadowColor, midColor, NdotL * 2.0);
    else
        color = lerp(midColor, highlightColor, (NdotL - 0.5) * 2.0);

    return float4(saturate(color) * input.Color.rgb, MaterialDiffuse.a * input.Color.a);
}
)hlsl";

/// @brief シルエット — エッジのみ描画（影絵風）
constexpr const char* SILHOUETTE_PS_3D = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;    float _pad0;
    float3 LightColor;  float _pad1;
    float3 AmbientColor; float _pad2;
    float3 CameraPos;   float _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float MaterialShininess;
    float3 _pad4;
};
struct PSInput
{
    float4 Position  : SV_POSITION;
    float3 WorldPos  : TEXCOORD0;
    float3 WorldNorm : TEXCOORD1;
    float2 TexCoord  : TEXCOORD2;
    float4 Color     : COLOR0;
};
float4 PSMain(PSInput input) : SV_TARGET
{
    float3 N = normalize(input.WorldNorm);
    float3 V = normalize(CameraPos - input.WorldPos);

    // エッジ検出: 視線と法線の角度が大きい→エッジ
    float edge = 1.0 - abs(dot(N, V));
    edge = smoothstep(0.3, 0.8, edge);

    // エッジ部分は素材色、中心は薄い色
    float3 edgeColor = MaterialDiffuse.rgb * 0.2;
    float3 fillColor = MaterialDiffuse.rgb * 0.85;
    float3 color = lerp(fillColor, edgeColor, edge);

    // 強いリムライト
    float rim = pow(1.0 - saturate(dot(N, V)), 3.0);
    color += rim * 0.4 * MaterialDiffuse.rgb;

    return float4(color * input.Color.rgb, MaterialDiffuse.a * input.Color.a);
}
)hlsl";

/// @brief 水彩風 — ソフトなグラデーション＋にじみ効果
constexpr const char* WATERCOLOR_PS_3D = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;    float _pad0;
    float3 LightColor;  float _pad1;
    float3 AmbientColor; float _pad2;
    float3 CameraPos;   float _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float MaterialShininess;
    float3 _pad4;
};
struct PSInput
{
    float4 Position  : SV_POSITION;
    float3 WorldPos  : TEXCOORD0;
    float3 WorldNorm : TEXCOORD1;
    float2 TexCoord  : TEXCOORD2;
    float4 Color     : COLOR0;
};
float4 PSMain(PSInput input) : SV_TARGET
{
    float3 N = normalize(input.WorldNorm);
    float3 L = normalize(-LightDir);
    float3 V = normalize(CameraPos - input.WorldPos);

    float NdotL = max(dot(N, L), 0.0);

    // ソフトなトゥーン（滑らかな遷移）
    float shade = smoothstep(0.0, 0.6, NdotL) * 0.6 + 0.4;

    // 紙のテクスチャ風ノイズ（スクリーン空間）
    float2 uv = input.Position.xy * 0.03;
    float noise = frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
    noise = noise * 0.08 - 0.04; // 微小な変動

    // 色を少し彩度上げてパステル調に
    float3 baseColor = MaterialDiffuse.rgb;
    float grey = dot(baseColor, float3(0.299, 0.587, 0.114));
    baseColor = lerp(float3(grey, grey, grey), baseColor, 1.3); // 彩度ブースト
    baseColor = lerp(baseColor, float3(1, 1, 1), 0.15);        // 白っぽく（水彩の透明感）

    float3 color = baseColor * shade + noise;

    // エッジを少し暗くする（水彩の輪郭にじみ）
    float edge = 1.0 - abs(dot(N, V));
    edge = smoothstep(0.5, 0.9, edge);
    color = lerp(color, baseColor * 0.5, edge * 0.3);

    return float4(saturate(color) * input.Color.rgb, MaterialDiffuse.a * input.Color.a);
}
)hlsl";

} // namespace mitiru::render
