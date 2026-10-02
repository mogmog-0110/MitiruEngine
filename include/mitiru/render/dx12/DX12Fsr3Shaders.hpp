#pragma once

/// @file DX12Fsr3Shaders.hpp
/// @brief FSR 3.1 に渡す入力を作る HLSL (1 標本の深度と、半透明の反応マスク)
/// @details FSR は MSAA の深度を読めないので、4 標本のうち最も近い深度を SV_Depth に書く (細い物を背景に負けさせない)。
///          反応マスクは WBOIT の被覆率 1 − Π(1 − a) で、履歴に頼ると尾を引く画素を FSR に知らせる。剣筋の分は
///          DX12_TRAIL_HLSL の PSReactive が MAX で足す。root sig は temporal のもの (SRV t0..t3 + CBV b0)。

namespace mitiru::render
{

inline constexpr const char* DX12_FSR_INPUTS_PS = R"hlsl(
Texture2DMS<float, 4> g_depth  : register(t0);   // 主パスの深度 (MSAA)
Texture2D<float>      g_reveal : register(t1);   // WBOIT の reveal = Π(1 − a) (resolve 済み)

cbuffer CbFsrInputs : register(b0)
{
    float OitDrawn;      // このフレームに半透明を描いたか
    float MaxReactive;   // FSR の勧めで 1 にしない (履歴を全く使わない画素は揺れる)
    float2 _pad;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

struct PSOutput
{
    float Reactive : SV_TARGET0;
    float Depth    : SV_DEPTH;
};

PSOutput PSMain(PSInput input)
{
    int2 p = int2(input.Position.xy);
    float z = g_depth.Load(p, 0);
    [unroll]
    for (int s = 1; s < 4; ++s) { z = min(z, g_depth.Load(p, s)); }
    PSOutput o;
    o.Depth = z;
    float coverage = (OitDrawn > 0.5) ? 1.0 - g_reveal.Load(int3(p, 0)) : 0.0;
    o.Reactive = min(saturate(coverage), MaxReactive);
    return o;
}
)hlsl";

} // namespace mitiru::render
