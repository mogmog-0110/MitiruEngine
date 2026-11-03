#pragma once

/// @file Nv12.hpp
/// @brief NV12 (4:2:0 の 2 面) を RGBA8 にする整数だけの変換 (ADR 0114)
/// @details OS の色変換 (Media Foundation の Video Processor) はバージョンや CPU の命令で端数が変わりうる。撮影とリプレイの絵を
///          どの PC でも同じにするため、変換はこの 1 か所の固定小数点で行う。色の行列と範囲は動画のメタデータから決め、
///          無いときは高さ 720 以上を BT.709、未満を BT.601、範囲は放送の 16-235 とする。色差は隣の画素へ複製する (補間しない)。

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace mitiru::video
{

enum class YuvMatrix : std::uint8_t
{
	Bt601,
	Bt709,
};

struct Nv12Layout
{
	const std::uint8_t* y = nullptr;    ///< 輝度の面の左上
	const std::uint8_t* uv = nullptr;   ///< U V の交互の面の左上
	std::size_t         yStride = 0;
	std::size_t         uvStride = 0;
};

namespace detail
{

struct YuvCoefficients
{
	int yScale;    ///< (Y - yBias) に掛ける 8 bit 小数
	int yBias;
	int rV, gU, gV, bU;
};

[[nodiscard]] constexpr YuvCoefficients coefficientsOf(YuvMatrix matrix, bool fullRange) noexcept
{
	if (matrix == YuvMatrix::Bt709)
	{
		return fullRange ? YuvCoefficients{256, 0, 403, 48, 120, 475} : YuvCoefficients{298, 16, 459, 55, 136, 541};
	}
	return fullRange ? YuvCoefficients{256, 0, 359, 88, 183, 454} : YuvCoefficients{298, 16, 409, 100, 208, 517};
}

[[nodiscard]] constexpr std::uint8_t clamp8(int v) noexcept
{
	return static_cast<std::uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

}  // namespace detail

/// @brief 動画の高さから、メタデータが無いときの行列を選ぶ
[[nodiscard]] constexpr YuvMatrix defaultMatrixFor(int height) noexcept
{
	return height >= 720 ? YuvMatrix::Bt709 : YuvMatrix::Bt601;
}

/// @brief width x height (偶数) の画素を rgba (width * 4 byte の行) に書く。A は 255
inline void nv12ToRgba(const Nv12Layout& src, int width, int height, YuvMatrix matrix, bool fullRange, std::uint8_t* rgba) noexcept
{
	const detail::YuvCoefficients k = detail::coefficientsOf(matrix, fullRange);
	for (int row = 0; row < height; ++row)
	{
		const std::uint8_t* yRow = src.y + static_cast<std::size_t>(row) * src.yStride;
		const std::uint8_t* uvRow = src.uv + static_cast<std::size_t>(row / 2) * src.uvStride;
		std::uint8_t* out = rgba + static_cast<std::size_t>(row) * static_cast<std::size_t>(width) * 4;
		for (int x = 0; x < width; ++x)
		{
			const int u = uvRow[(x & ~1)] - 128;
			const int v = uvRow[(x & ~1) + 1] - 128;
			const int c = k.yScale * (yRow[x] - k.yBias) + 128;
			out[x * 4 + 0] = detail::clamp8((c + k.rV * v) >> 8);
			out[x * 4 + 1] = detail::clamp8((c - k.gU * u - k.gV * v) >> 8);
			out[x * 4 + 2] = detail::clamp8((c + k.bU * u) >> 8);
			out[x * 4 + 3] = 255;
		}
	}
}

}  // namespace mitiru::video
