#pragma once

/// @file DX12ShaderModeVS.hpp
/// @brief DX12 メインパス用 VS（lightSpacePos を出力する）
/// @details `ToonShaders3D.hpp` の `TOON_VS_3D` は DX11 と共有。
///          DX12 ではシャドウサンプルのために `CbShadow (b3)` を読み、
///          `lightSpacePos` を VSOutput に追加する必要があるため、
///          このヘッダで DX12 専用 VS を提供する。
///
///          全ての DX12 PS は VSOutput に lightSpacePos が含まれることを
///          前提とした PSInput を持つこと（ShaderModePS / MultiLightShaders を参照）。

namespace mitiru::render
{

inline constexpr const char* DX12_DEFAULT_VS_3D = R"hlsl(
cbuffer CbTransform : register(b0)
{
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
};

cbuffer CbShadow : register(b3)
{
    float4x4 LightViewProj;
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
    float4 Position      : SV_POSITION;
    float3 WorldPos      : TEXCOORD0;
    float3 WorldNorm     : TEXCOORD1;
    float2 TexCoord      : TEXCOORD2;
    float4 LightSpacePos : TEXCOORD3;
    float4 Color         : COLOR0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;

    float4 worldPos = mul(World, float4(input.Position, 1.0));
    output.WorldPos = worldPos.xyz;
    output.WorldNorm = normalize(mul((float3x3)World, input.Normal));

    float4 viewPos = mul(View, worldPos);
    output.Position = mul(Projection, viewPos);

    // ライト空間位置。シャドウサンプル PS で参照
    output.LightSpacePos = mul(LightViewProj, worldPos);

    output.TexCoord = input.TexCoord;
    output.Color = input.Color;

    return output;
}
)hlsl";

} // namespace mitiru::render
