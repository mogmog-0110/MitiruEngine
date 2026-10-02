#pragma once

/// @file VfxTextures.hpp
/// @brief デカールと粒が共有するテクスチャの層 (一辺 kVfxLayerSize の配列の 1 枚) を CPU で作る
/// @details 層はどれも同じ大きさなので、登録した画像は双線形で kVfxLayerSize 角へ引き伸ばしてから
///          mip を作る。色の層は sRGB として縮め、データの層 (法線) は向きとして縮める。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <mitiru/render/TextureMips.hpp>

namespace mitiru::render
{

inline constexpr int kVfxLayerSize = 256;
inline constexpr int kVfxMaxLayers = 32;

enum class VfxTextureKind : std::uint8_t
{
	Color = 0,   ///< 基本色と不透明度 (sRGB)。デカールの albedoLayer と粒の textureLayer
	Normal = 1,  ///< 接空間の法線 (RG、[0,1] に詰めた [-1,1])。デカールの normalLayer
};

/// @brief rgba (w x h) を双線形で size x size へ引き伸ばす。端は端の画素を伸ばす
[[nodiscard]] inline std::vector<std::uint8_t> resampleRgba(const std::uint8_t* rgba, int w, int h, int size)
{
	std::vector<std::uint8_t> out(static_cast<std::size_t>(size) * size * 4);
	const auto at = [&](int x, int y, int c) {
		x = std::clamp(x, 0, w - 1);
		y = std::clamp(y, 0, h - 1);
		return static_cast<float>(rgba[(static_cast<std::size_t>(y) * w + x) * 4 + c]);
	};
	for (int y = 0; y < size; ++y)
	{
		const float sy = (static_cast<float>(y) + 0.5f) * static_cast<float>(h) / static_cast<float>(size) - 0.5f;
		const int y0 = static_cast<int>(std::floor(sy));
		const float fy = sy - static_cast<float>(y0);
		for (int x = 0; x < size; ++x)
		{
			const float sx = (static_cast<float>(x) + 0.5f) * static_cast<float>(w) / static_cast<float>(size) - 0.5f;
			const int x0 = static_cast<int>(std::floor(sx));
			const float fx = sx - static_cast<float>(x0);
			for (int c = 0; c < 4; ++c)
			{
				const float top = at(x0, y0, c) * (1.0f - fx) + at(x0 + 1, y0, c) * fx;
				const float bot = at(x0, y0 + 1, c) * (1.0f - fx) + at(x0 + 1, y0 + 1, c) * fx;
				const float v = top * (1.0f - fy) + bot * fy;
				out[(static_cast<std::size_t>(y) * size + x) * 4 + c] = static_cast<std::uint8_t>(std::clamp(v + 0.5f, 0.0f, 255.0f));
			}
		}
	}
	return out;
}

/// @brief 1 層ぶんの mip 連鎖 (kVfxLayerSize から 1x1 まで)。画像が空なら空を返す
[[nodiscard]] inline std::vector<std::vector<std::uint8_t>> buildVfxLayer(const std::uint8_t* rgba, int w, int h,
                                                                        VfxTextureKind kind)
{
	if (rgba == nullptr || w <= 0 || h <= 0) { return {}; }
	const std::vector<std::uint8_t> base = (w == kVfxLayerSize && h == kVfxLayerSize)
		? std::vector<std::uint8_t>(rgba, rgba + static_cast<std::size_t>(w) * h * 4)
		: resampleRgba(rgba, w, h, kVfxLayerSize);
	return buildMipChain(base.data(), kVfxLayerSize, kVfxLayerSize,
	                     kind == VfxTextureKind::Normal ? MipFilter::NormalMap : MipFilter::Srgb);
}

} // namespace mitiru::render
