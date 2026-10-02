#pragma once

/// @file DX12PBRShaders.hpp
/// @brief IBL のテクスチャの大きさ。PBR の陰影そのものは DX12LitShaders.hpp の PBR PS にある

namespace mitiru::render
{

/// @brief プリフィルター鏡面キューブマップの mip 数。mip i の roughness = i / (数 - 1) で、
///        シェーダーは `SampleLevel(R, roughness * (数 - 1))` で粗さに応じた段を引く。
inline constexpr int kPbrPrefilterMipCount = 5;
/// @brief split-sum の環境 BRDF 表 (t10) の一辺。
inline constexpr int kPbrBrdfLutSize = 64;

} // namespace mitiru::render
