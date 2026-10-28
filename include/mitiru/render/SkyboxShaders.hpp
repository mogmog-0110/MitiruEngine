#pragma once

/// @file SkyboxShaders.hpp
/// @brief Skybox 用 HLSL シェーダーソース
/// @details Skybox.hpp から参照される。VS は方向ベクトルを補間し、
///          PS は TextureCube からサンプリングするだけの最小実装。
///          深度は最遠（z = w）に固定し、深度書き込みは無効で運用する。

namespace mitiru::render
{

/// @brief Skybox 用バーテックスシェーダー HLSL
inline constexpr const char* SKYBOX_VS_HLSL = R"HLSL(
cbuffer CbSkyTransform : register(b0)
{
    float4x4 viewNoTranslation;
    float4x4 projection;
};

struct VSIn
{
    float3 pos : POSITION;
};

struct VSOut
{
    float4 svpos : SV_POSITION;
    float3 dir   : TEXCOORD0;
};

VSOut VSMain(VSIn i)
{
    VSOut o;
    o.dir = i.pos;
    float4 wp = mul(float4(i.pos, 0.0), viewNoTranslation);
    wp = mul(wp, projection);
    // svpos.z = w -> NDC z = 1, i.e. furthest plane, so depth test
    // LESS_EQUAL with a cleared 1.0 depth buffer leaves the skybox
    // visible only where no scene geometry has drawn.
    o.svpos = wp.xyww;
    return o;
}
)HLSL";

/// @brief Skybox 用ピクセルシェーダー HLSL
inline constexpr const char* SKYBOX_PS_HLSL = R"HLSL(
TextureCube  g_sky   : register(t0);
SamplerState g_samp  : register(s0);

struct VSOut
{
    float4 svpos : SV_POSITION;
    float3 dir   : TEXCOORD0;
};

float4 PSMain(VSOut i) : SV_TARGET
{
    return g_sky.Sample(g_samp, normalize(i.dir));
}
)HLSL";

/// @brief DX12 の skybox。DX11 と同じ形に、色へ掛ける倍率 (HDR の環境の intensity) を足した物。倍率 1 は従来と同じ値を返す
inline constexpr const char* SKYBOX_DX12_VS_HLSL = R"HLSL(
cbuffer CbSkyTransform : register(b0)
{
    float4x4 viewNoTranslation;
    float4x4 projection;
    float4   intensity;
};

struct VSIn
{
    float3 pos : POSITION;
};

struct VSOut
{
    float4 svpos : SV_POSITION;
    float3 dir   : TEXCOORD0;
    nointerpolation float scale : TEXCOORD1;   // 補間すると 1.0 が 1 ulp ずれて、倍率 1 の絵が従来と 1 段ずれうる
};

VSOut VSMain(VSIn i)
{
    VSOut o;
    o.dir = i.pos;
    o.scale = intensity.x;
    float4 wp = mul(float4(i.pos, 0.0), viewNoTranslation);
    wp = mul(wp, projection);
    o.svpos = wp.xyww;
    return o;
}
)HLSL";

inline constexpr const char* SKYBOX_DX12_PS_HLSL = R"HLSL(
TextureCube  g_sky   : register(t0);
SamplerState g_samp  : register(s0);

struct VSOut
{
    float4 svpos : SV_POSITION;
    float3 dir   : TEXCOORD0;
    nointerpolation float scale : TEXCOORD1;   // 補間すると 1.0 が 1 ulp ずれて、倍率 1 の絵が従来と 1 段ずれうる
};

float4 PSMain(VSOut i) : SV_TARGET
{
    float4 c = g_sky.Sample(g_samp, normalize(i.dir));
    return float4(c.rgb * i.scale, c.a);
}
)HLSL";

} // namespace mitiru::render
