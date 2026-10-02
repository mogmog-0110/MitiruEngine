#pragma once

/// @file ResizePixels.hpp
/// @brief スクリーンショット API の縮小 (RGBA8、ニアレストネイバー)

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mitiru::server::detail
{

/// @brief ニアレストネイバーで RGBA8 ピクセルをリサイズする
[[nodiscard]] inline std::vector<std::uint8_t> resizePixels(
	const std::vector<std::uint8_t>& src, int srcW, int srcH, int dstW, int dstH)
{
	if (dstW <= 0 || dstH <= 0 || srcW <= 0 || srcH <= 0) { return src; }

	std::vector<std::uint8_t> dst(
		static_cast<std::size_t>(dstW) * static_cast<std::size_t>(dstH) * 4);

	for (int y = 0; y < dstH; ++y)
	{
		const int sy = (y * srcH) / dstH;
		for (int x = 0; x < dstW; ++x)
		{
			const int sx = (x * srcW) / dstW;
			const auto srcIdx = static_cast<std::size_t>((sy * srcW + sx) * 4);
			const auto dstIdx = static_cast<std::size_t>((y * dstW + x) * 4);

			if (srcIdx + 3 < src.size() && dstIdx + 3 < dst.size())
			{
				dst[dstIdx + 0] = src[srcIdx + 0];
				dst[dstIdx + 1] = src[srcIdx + 1];
				dst[dstIdx + 2] = src[srcIdx + 2];
				dst[dstIdx + 3] = src[srcIdx + 3];
			}
		}
	}

	return dst;
}

} // namespace mitiru::server::detail
