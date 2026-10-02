#pragma once

/// @file TextureMips.hpp
/// @brief RGBA8 画像の mip 連鎖 (2x2 の箱フィルタ)
/// @details DX12 へ直接上げる経路とオフライン圧縮 (TextureCompress) が同じ規則で縮めるよう、
///          ここ 1 箇所に置く。mip が 1 枚だけだと、縮小して映る面が texel を 1 つおきに拾って
///          点々に見える。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <mitiru/render/ColorSpace.hpp>

namespace mitiru::render
{

/// @brief 縮小時の平均の取り方
enum class MipFilter : std::uint8_t
{
	Srgb,       ///< RGB は線形へ戻して平均 (sRGB のまま平均すると暗部に寄る)。A はそのまま
	Linear,     ///< 4 成分ともそのまま平均 (マスク・粗さ等のデータ)
	NormalMap,  ///< RGB を [-1,1] の向きとして平均し長さ 1 に戻す (平均で短くなると陰影が浅くなる)
};

/// @brief 1x1 に行き着くまでの段数 (非 2 冪でも切り捨てで 1 まで降りる)
[[nodiscard]] inline int mipLevelCount(int w, int h) noexcept
{
	int levels = 1;
	while (w > 1 || h > 1)
	{
		w = (w > 1) ? w / 2 : 1;
		h = (h > 1) ? h / 2 : 1;
		++levels;
	}
	return levels;
}

namespace detail
{

[[nodiscard]] inline const float* srgbToLinearTable()
{
	static const std::array<float, 256> table = [] {
		std::array<float, 256> t{};
		for (int i = 0; i < 256; ++i)
		{
			t[static_cast<std::size_t>(i)] = srgbToLinear(static_cast<float>(i) / 255.0f);
		}
		return t;
	}();
	return table.data();
}

[[nodiscard]] inline std::uint8_t toByte(float v01) noexcept
{
	return static_cast<std::uint8_t>(std::clamp(v01 * 255.0f + 0.5f, 0.0f, 255.0f));
}

[[nodiscard]] inline std::uint8_t linearToSrgb8(float v) noexcept
{
	return toByte(linearToSrgb(v));
}

/// 4 画素 (tap) の RGB を filter に従って 1 画素にまとめる。A は常に単純平均
inline void foldRgb(const std::uint8_t* src, const std::size_t (&tap)[4], MipFilter filter,
                    std::uint8_t* dst)
{
	float rgb[3] = {0.0f, 0.0f, 0.0f};
	const float* toLinear = srgbToLinearTable();
	for (const std::size_t o : tap)
	{
		for (int k = 0; k < 3; ++k)
		{
			const std::uint8_t c = src[o + static_cast<std::size_t>(k)];
			rgb[k] += (filter == MipFilter::Srgb) ? toLinear[c]
			        : (filter == MipFilter::NormalMap) ? (static_cast<float>(c) / 127.5f - 1.0f)
			                                           : static_cast<float>(c) / 255.0f;
		}
	}
	if (filter == MipFilter::NormalMap)
	{
		const float len = std::sqrt(rgb[0] * rgb[0] + rgb[1] * rgb[1] + rgb[2] * rgb[2]);
		const float inv = (len > 1e-6f) ? 1.0f / len : 0.0f;
		for (int k = 0; k < 3; ++k) { dst[k] = toByte(rgb[k] * inv * 0.5f + 0.5f); }
		return;
	}
	for (int k = 0; k < 3; ++k)
	{
		dst[k] = (filter == MipFilter::Srgb) ? linearToSrgb8(rgb[k] * 0.25f) : toByte(rgb[k] * 0.25f);
	}
}

} // namespace detail

/// @brief 親レベル (sw x sh) を 2x2 の箱で dw x dh へ縮める
[[nodiscard]] inline std::vector<std::uint8_t> downsampleHalf(
	const std::uint8_t* src, int sw, int sh, int dw, int dh, MipFilter filter)
{
	std::vector<std::uint8_t> out(static_cast<std::size_t>(dw) * dh * 4);
	for (int y = 0; y < dh; ++y)
	{
		const int y0 = std::min(y * 2, sh - 1);
		const int y1 = std::min(y * 2 + 1, sh - 1);
		for (int x = 0; x < dw; ++x)
		{
			const int x0 = std::min(x * 2, sw - 1);
			const int x1 = std::min(x * 2 + 1, sw - 1);
			const std::size_t tap[4] = {
				(static_cast<std::size_t>(y0) * sw + x0) * 4, (static_cast<std::size_t>(y0) * sw + x1) * 4,
				(static_cast<std::size_t>(y1) * sw + x0) * 4, (static_cast<std::size_t>(y1) * sw + x1) * 4};
			std::uint8_t* d = &out[(static_cast<std::size_t>(y) * dw + x) * 4];
			detail::foldRgb(src, tap, filter, d);
			const int a = src[tap[0] + 3] + src[tap[1] + 3] + src[tap[2] + 3] + src[tap[3] + 3];
			d[3] = static_cast<std::uint8_t>((a + 2) / 4);
		}
	}
	return out;
}

/// @brief level 0 (rgba のコピー) から 1x1 までの全段を返す
[[nodiscard]] inline std::vector<std::vector<std::uint8_t>> buildMipChain(
	const std::uint8_t* rgba, int w, int h, MipFilter filter)
{
	const int levels = mipLevelCount(w, h);
	std::vector<std::vector<std::uint8_t>> mips;
	mips.reserve(static_cast<std::size_t>(levels));
	mips.emplace_back(rgba, rgba + static_cast<std::size_t>(w) * h * 4);
	for (int lv = 1; lv < levels; ++lv)
	{
		const int nw = (w > 1) ? w / 2 : 1;
		const int nh = (h > 1) ? h / 2 : 1;
		mips.push_back(downsampleHalf(mips.back().data(), w, h, nw, nh, filter));
		w = nw;
		h = nh;
	}
	return mips;
}

} // namespace mitiru::render
