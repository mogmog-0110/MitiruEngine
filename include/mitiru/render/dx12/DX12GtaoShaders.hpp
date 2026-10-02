#pragma once

/// @file DX12GtaoShaders.hpp
/// @brief XeGTAO (external/XeGTAO、MIT) を呼ぶ HLSL の入口
/// @details XeGTAO.hlsli は関数だけを持ち、資源の宣言と入口は使う側が書く。ここではパスごとに
///          「XeGTAO の本文 + そのパスの資源 + 入口」を組み立てる。深度は 32 bit (XE_GTAO_FP32_DEPTHS) にする。
///          FXC の SM 5 は min16float を精度の指示として受けるだけで、16 bit にしても速さが当てにならない。
///          面の向きは主パスの法線 RT ではなく深度から組み直す (XeGTAO_ComputeViewspaceNormal)。
///          SSAO と同じく、角を丸めた大きな面で頂点法線の補間が傾くのを拾わないため。

#include <string>

#include <mitiru/render/dx12/XeGTAOHlsl_tables.hpp>

namespace mitiru::render
{

/// @brief XeGTAO.hlsli が前提にする上流の枠組みの名前 (VA_*) もここで決める
inline constexpr const char* DX12_GTAO_PRELUDE = R"hlsl(
#define VA_SATURATE saturate
#define VA_PI 3.1415926535897932384626433832795
#define XE_GTAO_USE_HALF_FLOAT_PRECISION 0
#define XE_GTAO_FP32_DEPTHS 1
#define XE_GTAO_USE_DEFAULT_CONSTANTS 0
)hlsl";

inline constexpr const char* DX12_GTAO_COMMON_DECLS = R"hlsl(
cbuffer CbGtao : register(b0)
{
    GTAOConstants g_consts;
};
SamplerState g_pointClamp : register(s0);

lpfloat2 SpatioTemporalNoise(uint2 pixCoord, uint temporalIndex)
{
    uint index = HilbertIndex(pixCoord.x, pixCoord.y);
    index += 288 * (temporalIndex % 64);
    return lpfloat2(frac(0.5 + index * float2(0.75487766624669276005, 0.5698402909980532659114)));
}
)hlsl";

/// @brief 主パスの MSAA 深度の sample 0 を single-sample の R32_FLOAT へ写す (全画面 PS)
inline constexpr const char* DX12_GTAO_DEPTH_COPY_PS = R"hlsl(
Texture2DMS<float, 4> g_depth : register(t0);
struct PSInput { float4 Position : SV_POSITION; float2 TexCoord : TEXCOORD0; };
float PSMain(PSInput input) : SV_TARGET
{
    return g_depth.Load(int2(input.Position.xy), 0);
}
)hlsl";

inline constexpr const char* DX12_GTAO_PREFILTER_CS = R"hlsl(
Texture2D<float>     g_srcNDCDepth : register(t0);
RWTexture2D<lpfloat> g_outDepth0   : register(u0);
RWTexture2D<lpfloat> g_outDepth1   : register(u1);
RWTexture2D<lpfloat> g_outDepth2   : register(u2);
RWTexture2D<lpfloat> g_outDepth3   : register(u3);
RWTexture2D<lpfloat> g_outDepth4   : register(u4);

[numthreads(8, 8, 1)]
void CSMain(uint2 dispatchThreadID : SV_DispatchThreadID, uint2 groupThreadID : SV_GroupThreadID)
{
    XeGTAO_PrefilterDepths16x16(dispatchThreadID, groupThreadID, g_consts, g_srcNDCDepth, g_pointClamp,
                                g_outDepth0, g_outDepth1, g_outDepth2, g_outDepth3, g_outDepth4);
}
)hlsl";

/// @brief 主パス。スライス数と 1 スライスの歩数は定数で渡す (FXC はループを展開できる数でないと通さない)。
///        XeGTAO の High は 3 と 3
inline constexpr const char* DX12_GTAO_MAIN_CS = R"hlsl(
#define GTAO_SLICES 3
#define GTAO_STEPS 3
Texture2D<lpfloat>       g_workingDepth : register(t0);
Texture2D<float>         g_srcNDCDepth  : register(t1);
RWTexture2D<uint>        g_outAOTerm    : register(u0);
RWTexture2D<unorm float> g_outEdges     : register(u1);

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSMain(const uint2 pixCoord : SV_DispatchThreadID)
{
    lpfloat3 normal = (lpfloat3)XeGTAO_ComputeViewspaceNormal(pixCoord, g_consts, g_srcNDCDepth, g_pointClamp);
    XeGTAO_MainPass(pixCoord, GTAO_SLICES, GTAO_STEPS, SpatioTemporalNoise(pixCoord, g_consts.NoiseIndex),
                    normal, g_consts, g_workingDepth, g_pointClamp, g_outAOTerm, g_outEdges);
}
)hlsl";

/// @brief 縁を保つぼかし。1 スレッドが横 2 画素を受け持つ。GTAO_FINAL を 1 にした版が最後の 1 回
inline constexpr const char* DX12_GTAO_DENOISE_CS = R"hlsl(
Texture2D<uint>    g_srcAOTerm : register(t0);
Texture2D<lpfloat> g_srcEdges  : register(t1);
RWTexture2D<uint>  g_outAOTerm : register(u0);

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSMain(const uint2 dispatchThreadID : SV_DispatchThreadID)
{
    const uint2 pixCoordBase = dispatchThreadID * uint2(2, 1);
    XeGTAO_Denoise(pixCoordBase, g_consts, g_srcAOTerm, g_srcEdges, g_pointClamp, g_outAOTerm, GTAO_FINAL != 0);
}
)hlsl";

/// @brief XeGTAO の本文に、パスの資源と入口をつないだ HLSL を作る
[[nodiscard]] inline std::string gtaoPassSource(const char* passBody, const char* extraDefines = "")
{
	std::string src = DX12_GTAO_PRELUDE;
	src += extraDefines;
	for (const char* part : kXeGTAOHlslParts) { src += part; }
	src += DX12_GTAO_COMMON_DECLS;
	src += passBody;
	return src;
}

} // namespace mitiru::render
