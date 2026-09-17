#pragma once

/// @file DX12OcclusionResolveShaders.hpp
/// @brief オクルージョンカリング用 min-depth resolve ピクセルシェーダー HLSL ソース (DX12)
/// @details DX12 の深度バッファは常に 4x MSAA (`MSAA_SAMPLE_COUNT`) のため、
///          サンプル数はコンパイル時に固定できる (`OUTLINE_POST_PS` と同じ前提)。
///          VS は `OUTLINE_POST_VS` (フルスクリーン三角形) を流用する。

namespace mitiru::render
{

/// @brief オクルージョン用 min-depth resolve ピクセルシェーダー (DX12、4x MSAA 固定)
/// @details 深度 SRV (t0、`m_depthSRVHeap` スロット0を流用) の全サンプルを
///          `Load` し、最小値 (カメラに最も近い＝最も保守的な遮蔽物候補) を
///          単一サンプル R32_FLOAT へ書く。
constexpr const char* DX12_OCCLUSION_MIN_DEPTH_RESOLVE_PS = R"hlsl(
Texture2DMS<float, 4> DepthTexture : register(t0);

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float PSMain(PSInput input) : SV_TARGET
{
    const uint2 coord = uint2(input.Position.xy);
    float minDepth = 1.0;
    [unroll]
    for (uint i = 0; i < 4; ++i)
    {
        minDepth = min(minDepth, DepthTexture.Load(coord, i));
    }
    return minDepth;
}
)hlsl";

} // namespace mitiru::render
