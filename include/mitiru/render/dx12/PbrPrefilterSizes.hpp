#pragma once

/// @file PbrPrefilterSizes.hpp
/// @brief 鏡面 IBL の prefilter キューブマップで使う mip ごとの一辺
/// @details D3D12 の mip 連鎖は一辺を半分ずつにし、下限を 1 とする。畳み込みの出力とテクスチャの mip が 1 段でもずれると、CopyTextureRegion が mip の外にはみ出し、command list を閉じられず device lost になる。畳み込みとテクスチャ作成の食い違いを防ぐため、この 1 か所で大きさを決める。

#include <algorithm>

#include <mitiru/render/dx12/DX12PBRShaders.hpp>

namespace mitiru::render
{

inline constexpr int kPbrPrefilterMinBase = 1 << (kPbrPrefilterMipCount - 1);

/// @brief 環境の一辺 faceSize から prefilter の mip 0 の一辺を決める。小さい環境は、鏡面の mip 連鎖を作れる大きさまで広げる
[[nodiscard]] constexpr int pbrPrefilterBase(int faceSize) noexcept
{
	return std::max(faceSize, kPbrPrefilterMinBase);
}

/// @brief mip の一辺。D3D12 のテクスチャと同じく、base を半分ずつにして下限を 1 とする
[[nodiscard]] constexpr int pbrPrefilterMipSize(int base, int mip) noexcept
{
	return std::max(base >> mip, 1);
}

} // namespace mitiru::render
