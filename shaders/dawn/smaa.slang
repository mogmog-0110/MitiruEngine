// SMAA 1x の 3 パス。上流の SMAA.hlsl (external/SMAA) を Slang がそのまま読む。DX12 は同じ本文を
// DX12SmaaShaders.hpp の前置きで読み、資源の並びだけが違う。トーンマップと輪郭線の後の最終色 (LDR) の写しを読む。
// offset の計算は上流では頂点側の関数だが、フルスクリーン三角形の頂点シェーダーを増やさないよう fragment の中で呼ぶ。

struct SmaaCB
{
    float4 metrics;   // xy = 1 / 画面の大きさ zw = 画面の大きさ
};

[[vk::binding(0, 0)]] ConstantBuffer<SmaaCB> smaaCb;
[[vk::binding(1, 0)]] Texture2D<float4> colorTex;
[[vk::binding(2, 0)]] Texture2D<float4> edgesTex;
[[vk::binding(3, 0)]] Texture2D<float4> blendTex;
[[vk::binding(4, 0)]] Texture2D<float4> areaTex;
[[vk::binding(5, 0)]] Texture2D<float4> searchTex;
[[vk::binding(6, 0)]] SamplerState linearSampler;
[[vk::binding(7, 0)]] SamplerState pointSampler;

#define SMAA_CUSTOM_SL
#define SMAA_PRESET_HIGH
#define SMAA_RT_METRICS smaaCb.metrics
#define SMAATexture2D(tex) Texture2D<float4> tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) tex.SampleLevel(linearSampler, coord, 0)
#define SMAASampleLevelZeroPoint(tex, coord) tex.SampleLevel(pointSampler, coord, 0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.SampleLevel(linearSampler, coord, 0, offset)
#define SMAASample(tex, coord) tex.SampleLevel(linearSampler, coord, 0)
#define SMAASamplePoint(tex, coord) tex.SampleLevel(pointSampler, coord, 0)
#define SMAASampleOffset(tex, coord, offset) tex.SampleLevel(linearSampler, coord, 0, offset)
#define SMAA_FLATTEN [flatten]
#define SMAA_BRANCH [branch]

#include "../../external/SMAA/SMAA.hlsl"

struct SmaaOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

[shader("vertex")]
SmaaOut vsSmaa(uint vid : SV_VertexID)
{
    SmaaOut o;
    float2 t = float2(float((vid << 1u) & 2u), float(vid & 2u));
    o.pos = float4(t * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.uv = t;
    return o;
}

[shader("fragment")]
float4 fsSmaaEdge(SmaaOut input) : SV_Target
{
    float4 offset[3];
    SMAAEdgeDetectionVS(input.uv, offset);
    return float4(SMAALumaEdgeDetectionPS(input.uv, offset, colorTex), 0.0, 0.0);
}

[shader("fragment")]
float4 fsSmaaWeight(SmaaOut input) : SV_Target
{
    float4 offset[3];
    float2 pixcoord;
    SMAABlendingWeightCalculationVS(input.uv, pixcoord, offset);
    return SMAABlendingWeightCalculationPS(input.uv, pixcoord, offset, edgesTex, areaTex, searchTex, float4(0.0, 0.0, 0.0, 0.0));
}

[shader("fragment")]
float4 fsSmaaBlend(SmaaOut input) : SV_Target
{
    float4 offset;
    SMAANeighborhoodBlendingVS(input.uv, offset);
    return SMAANeighborhoodBlendingPS(input.uv, offset, colorTex, blendTex);
}
