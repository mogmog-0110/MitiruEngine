#pragma once

/// @file SlotThumbnail.hpp
/// @brief セーブスロットに付ける小さなサムネイル (フレームを縮めた PNG)。

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

#include <mitiru/util/ImageWriter.hpp>

namespace mitiru::save
{

struct SlotThumbnail
{
	std::vector<std::uint8_t> png;
	std::uint16_t width = 0;
	std::uint16_t height = 0;
};

/// @brief RGBA8 の画像を縦横比を保って maxW x maxH に収め、面積平均で縮める。
[[nodiscard]] inline std::vector<std::uint8_t> downscaleRgba(std::span<const std::uint8_t> rgba, int w, int h,
                                                             int outW, int outH)
{
	std::vector<std::uint8_t> out(static_cast<std::size_t>(outW) * outH * 4);
	for (int y = 0; y < outH; ++y)
	{
		const int y0 = y * h / outH;
		const int y1 = std::max(y0 + 1, (y + 1) * h / outH);
		for (int x = 0; x < outW; ++x)
		{
			const int x0 = x * w / outW;
			const int x1 = std::max(x0 + 1, (x + 1) * w / outW);
			std::uint32_t sum[4] = {};
			for (int sy = y0; sy < y1; ++sy)
			{
				const std::uint8_t* row = rgba.data() + (static_cast<std::size_t>(sy) * w + x0) * 4;
				for (int sx = x0; sx < x1; ++sx, row += 4)
				{
					for (int c = 0; c < 4; ++c) { sum[c] += row[c]; }
				}
			}
			const std::uint32_t n = static_cast<std::uint32_t>((y1 - y0) * (x1 - x0));
			std::uint8_t* dst = out.data() + (static_cast<std::size_t>(y) * outW + x) * 4;
			for (int c = 0; c < 4; ++c) { dst[c] = static_cast<std::uint8_t>((sum[c] + n / 2) / n); }
		}
	}
	return out;
}

/// @brief フレーム (RGBA8) からサムネイルを作る。画像が無ければ空の SlotThumbnail。
[[nodiscard]] inline SlotThumbnail makeSlotThumbnail(std::span<const std::uint8_t> rgba, int w, int h,
                                                     int maxW = 320, int maxH = 180)
{
	SlotThumbnail t;
	if (w <= 0 || h <= 0 || rgba.size() < static_cast<std::size_t>(w) * h * 4) { return t; }
	const double scale = std::min({ 1.0, static_cast<double>(maxW) / w, static_cast<double>(maxH) / h });
	const int outW = std::max(1, static_cast<int>(w * scale));
	const int outH = std::max(1, static_cast<int>(h * scale));
	auto shrunk = downscaleRgba(rgba, w, h, outW, outH);
	// セーブ画面に並べる絵なので、透明な所を残すと下地の色が透けて見える
	for (std::size_t i = 3; i < shrunk.size(); i += 4) { shrunk[i] = 255; }
	t.png = util::encodePng(shrunk.data(), outW, outH);
	if (!t.png.empty())
	{
		t.width = static_cast<std::uint16_t>(outW);
		t.height = static_cast<std::uint16_t>(outH);
	}
	return t;
}

}  // namespace mitiru::save
