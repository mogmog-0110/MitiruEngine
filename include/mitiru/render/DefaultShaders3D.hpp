#pragma once

/// @file DefaultShaders3D.hpp
/// @brief 3D描画用デフォルトシェーダー（HLSL SM5.0）
/// @details Phongシェーディングモデルによる頂点・ピクセルシェーダーを提供する。
///          ディレクショナルライト1灯によるアンビエント+ディフューズ+スペキュラー照明。

namespace mitiru::render
{

/// @brief 3D Phong照明用 頂点シェーダー（HLSL SM5.0）
/// @details ワールド・ビュー・プロジェクション行列による座標変換と、
///          ワールド空間での法線・位置をピクセルシェーダーに渡す。
constexpr const char* DEFAULT_VS_3D = R"hlsl(
cbuffer CbTransform : register(b0)
{
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
};

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

struct VSOutput
{
    float4 Position  : SV_POSITION;
    float3 WorldPos  : TEXCOORD0;
    float3 WorldNorm : TEXCOORD1;
    float2 TexCoord  : TEXCOORD2;
    float4 Color     : COLOR0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;

    float4 worldPos = mul(float4(input.Position, 1.0), World);
    output.WorldPos = worldPos.xyz;
    output.WorldNorm = normalize(mul(input.Normal, (float3x3)World));

    float4 viewPos = mul(worldPos, View);
    output.Position = mul(viewPos, Projection);

    output.TexCoord = input.TexCoord;
    output.Color = input.Color;

    return output;
}
)hlsl";

/// @brief 3D Phong照明用 ピクセルシェーダー（HLSL SM5.0）
/// @details ディレクショナルライト1灯によるPhongシェーディング。
///          アンビエント + ディフューズ + スペキュラー成分を計算する。
constexpr const char* DEFAULT_PS_3D = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;
    float  _pad0;
    float3 LightColor;
    float  _pad1;
    float3 AmbientColor;
    float  _pad2;
    float3 CameraPos;
    float  _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float  MaterialShininess;
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
    float3 R = reflect(-L, N);

    // アンビエント成分
    float3 ambient = AmbientColor * MaterialDiffuse.rgb;

    // ディフューズ成分（Lambert）
    float NdotL = max(dot(N, L), 0.0);
    float3 diffuse = LightColor * MaterialDiffuse.rgb * NdotL;

    // スペキュラー成分（Blinn-Phong）
    float3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float specFactor = pow(NdotH, MaterialShininess);
    float3 specular = LightColor * MaterialSpecular.rgb * specFactor;

    float3 finalColor = ambient + diffuse + specular;
    float alpha = MaterialDiffuse.a * input.Color.a;

    return float4(finalColor * input.Color.rgb, alpha);
}
)hlsl";

/// @brief アンライト（照明なし）頂点シェーダー
/// @details テクスチャカラーと頂点カラーのみで描画する3Dシェーダー。
constexpr const char* UNLIT_VS_3D = R"hlsl(
cbuffer CbTransform : register(b0)
{
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
};

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    float4 worldPos = mul(float4(input.Position, 1.0), World);
    float4 viewPos = mul(worldPos, View);
    output.Position = mul(viewPos, Projection);
    output.TexCoord = input.TexCoord;
    output.Color = input.Color;
    return output;
}
)hlsl";

/// @brief アンライト（照明なし）ピクセルシェーダー
/// @details 頂点カラーをそのまま出力する。
constexpr const char* UNLIT_PS_3D = R"hlsl(
struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.Color;
}
)hlsl";

} // namespace mitiru::render
