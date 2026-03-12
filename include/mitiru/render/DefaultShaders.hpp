#pragma once

/// @file DefaultShaders.hpp
/// @brief デフォルト2Dシェーダー定義
/// @details 2D描画パイプラインで使用するHLSLシェーダーソースを
///          constexpr文字列として提供する。

#include <string_view>

namespace mitiru::render
{

/// @brief デフォルト2D頂点シェーダーのHLSLソース
/// @details 入力: float2 position, float2 texCoord, float4 color
///          定数バッファ: float4x4 projection（正射影行列）
///          出力: 変換後の位置とテクスチャ座標・色をパススルー
constexpr std::string_view DEFAULT_VS_2D = R"hlsl(
cbuffer Constants : register(b0)
{
	float4x4 projection;
};

struct VSInput
{
	float2 position : POSITION;
	float2 texCoord : TEXCOORD0;
	float4 color    : COLOR0;
};

struct VSOutput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
	float4 color    : COLOR0;
};

VSOutput VSMain(VSInput input)
{
	VSOutput output;
	output.position = mul(projection, float4(input.position, 0.0f, 1.0f));
	output.texCoord = input.texCoord;
	output.color = input.color;
	return output;
}
)hlsl";

/// @brief デフォルト2DピクセルシェーダーのHLSLソース
/// @details 頂点色をそのまま出力する。テクスチャサンプリングは未実装。
constexpr std::string_view DEFAULT_PS_2D = R"hlsl(
struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
	float4 color    : COLOR0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
	return input.color;
}
)hlsl";

} // namespace mitiru::render
