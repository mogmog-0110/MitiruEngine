#pragma once

/// @file DX12SmaaShaders.hpp
/// @brief SMAA 1x (iryoku/smaa、external/SMAA) の 3 パスの HLSL。上流の本文は SmaaHlsl_tables.hpp
/// @details 上流の VS 側の関数 (offset の計算) は PS の中で呼ぶ。VS は OUTLINE_POST_VS のフルスクリーン三角形を流用するため。
///          資源の並びは root sig と同じ: t0 色、t1 辺、t2 重み、t3 面積、t4 探索。b0 は画素の大きさ。s0 線形、s1 点。
///          辺と重みは毎フレーム 0 に塗ってから使う (辺のないところを discard で飛ばすため)。

#include <string>

#include <mitiru/render/dx12/SmaaHlsl_tables.hpp>

namespace mitiru::render
{

inline constexpr const char* DX12_SMAA_PRELUDE = R"hlsl(
#define SMAA_CUSTOM_SL
#define SMAA_PRESET_HIGH
cbuffer CbSmaa : register(b0) { float4 g_rtMetrics; };
#define SMAA_RT_METRICS g_rtMetrics
Texture2D g_color : register(t0);
Texture2D g_edges : register(t1);
Texture2D g_blend : register(t2);
Texture2D g_area : register(t3);
Texture2D g_search : register(t4);
SamplerState g_linear : register(s0);
SamplerState g_point : register(s1);
#define SMAATexture2D(tex) Texture2D tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) tex.SampleLevel(g_linear, coord, 0)
#define SMAASampleLevelZeroPoint(tex, coord) tex.SampleLevel(g_point, coord, 0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.SampleLevel(g_linear, coord, 0, offset)
#define SMAASample(tex, coord) tex.SampleLevel(g_linear, coord, 0)
#define SMAASamplePoint(tex, coord) tex.SampleLevel(g_point, coord, 0)
#define SMAASampleOffset(tex, coord, offset) tex.SampleLevel(g_linear, coord, 0, offset)
#define SMAA_FLATTEN [flatten]
#define SMAA_BRANCH [branch]
)hlsl";

inline constexpr const char* DX12_SMAA_INPUT = R"hlsl(
struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};
)hlsl";

inline constexpr const char* DX12_SMAA_EDGE_PS = R"hlsl(
float4 PSMain(PSInput input) : SV_TARGET
{
    float4 offset[3];
    SMAAEdgeDetectionVS(input.TexCoord, offset);
    return float4(SMAALumaEdgeDetectionPS(input.TexCoord, offset, g_color), 0.0, 0.0);
}
)hlsl";

inline constexpr const char* DX12_SMAA_WEIGHT_PS = R"hlsl(
float4 PSMain(PSInput input) : SV_TARGET
{
    float4 offset[3];
    float2 pixcoord;
    SMAABlendingWeightCalculationVS(input.TexCoord, pixcoord, offset);
    return SMAABlendingWeightCalculationPS(input.TexCoord, pixcoord, offset, g_edges, g_area, g_search, float4(0.0, 0.0, 0.0, 0.0));
}
)hlsl";

inline constexpr const char* DX12_SMAA_BLEND_PS = R"hlsl(
float4 PSMain(PSInput input) : SV_TARGET
{
    float4 offset;
    SMAANeighborhoodBlendingVS(input.TexCoord, offset);
    return SMAANeighborhoodBlendingPS(input.TexCoord, offset, g_color, g_blend);
}
)hlsl";

/// @brief 3 パスのうち 1 本の PS の全文 (前置き + 上流 + 入力の型 + 入口)
[[nodiscard]] inline std::string smaaPassSource(const char* entry)
{
	std::string src = DX12_SMAA_PRELUDE;
	for (const char* part : kSmaaHlslParts) { src += part; }
	src += DX12_SMAA_INPUT;
	src += entry;
	return src;
}

}  // namespace mitiru::render
