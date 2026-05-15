#pragma once

/// @file PngEncoder.hpp
/// @brief 依存なしの無圧縮PNGエンコーダ

#include <cstdint>
#include <cstddef>
#include <vector>

namespace mitiru::server::detail
{

/// @brief RGBA8ピクセルデータを無圧縮PNGに変換する
/// @details 完全な依存なしのPNGエンコーダ。deflateの代わりにstored blockを使用する。
///          画像サイズが小さい場合に適する（スクリーンショット用途では十分）。
[[nodiscard]] inline std::vector<std::uint8_t> encodePng(
	const std::uint8_t* pixels, int width, int height)
{
	if (!pixels || width <= 0 || height <= 0) { return {}; }

	// 各行: フィルタバイト(0) + RGBA * width
	const std::size_t rowBytes = static_cast<std::size_t>(width) * 4 + 1;
	const std::size_t rawSize = rowBytes * static_cast<std::size_t>(height);

	// ── IDATデータを構築する（非圧縮deflateストリーム） ──
	std::vector<std::uint8_t> deflateData;
	deflateData.reserve(rawSize + 64);

	// zlib header
	deflateData.push_back(0x78); // CMF
	deflateData.push_back(0x01); // FLG (FCHECK=1)

	// stored blocks（最大65535バイトずつ）
	std::vector<std::uint8_t> rawData;
	rawData.reserve(rawSize);
	for (int y = 0; y < height; ++y)
	{
		rawData.push_back(0); // フィルタ: None
		const auto* row = pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4;
		rawData.insert(rawData.end(), row, row + static_cast<std::size_t>(width) * 4);
	}

	std::size_t pos = 0;
	while (pos < rawData.size())
	{
		const std::size_t remain = rawData.size() - pos;
		const std::size_t blockSize = (remain > 65535) ? 65535 : remain;
		const bool lastBlock = (pos + blockSize >= rawData.size());

		deflateData.push_back(lastBlock ? 0x01 : 0x00);
		const auto len = static_cast<std::uint16_t>(blockSize);
		const auto nlen = static_cast<std::uint16_t>(~len);
		deflateData.push_back(static_cast<std::uint8_t>(len & 0xFF));
		deflateData.push_back(static_cast<std::uint8_t>((len >> 8) & 0xFF));
		deflateData.push_back(static_cast<std::uint8_t>(nlen & 0xFF));
		deflateData.push_back(static_cast<std::uint8_t>((nlen >> 8) & 0xFF));

		deflateData.insert(deflateData.end(),
			rawData.begin() + static_cast<std::ptrdiff_t>(pos),
			rawData.begin() + static_cast<std::ptrdiff_t>(pos + blockSize));

		pos += blockSize;
	}

	// Adler-32
	std::uint32_t s1 = 1, s2 = 0;
	for (const auto b : rawData)
	{
		s1 = (s1 + b) % 65521;
		s2 = (s2 + s1) % 65521;
	}
	const std::uint32_t adler = (s2 << 16) | s1;
	deflateData.push_back(static_cast<std::uint8_t>((adler >> 24) & 0xFF));
	deflateData.push_back(static_cast<std::uint8_t>((adler >> 16) & 0xFF));
	deflateData.push_back(static_cast<std::uint8_t>((adler >> 8) & 0xFF));
	deflateData.push_back(static_cast<std::uint8_t>(adler & 0xFF));

	// ── PNGファイル構築 ──
	std::vector<std::uint8_t> png;
	png.reserve(deflateData.size() + 128);

	// PNG signature
	const std::uint8_t sig[] = {137, 80, 78, 71, 13, 10, 26, 10};
	png.insert(png.end(), sig, sig + 8);

	// CRC-32テーブル
	auto crc32 = [](const std::uint8_t* data, std::size_t len) -> std::uint32_t {
		static std::uint32_t table[256] = {};
		static bool tableBuilt = false;
		if (!tableBuilt)
		{
			for (std::uint32_t i = 0; i < 256; ++i)
			{
				std::uint32_t c = i;
				for (int k = 0; k < 8; ++k)
				{
					c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
				}
				table[i] = c;
			}
			tableBuilt = true;
		}
		std::uint32_t crc = 0xFFFFFFFFu;
		for (std::size_t i = 0; i < len; ++i)
		{
			crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
		}
		return crc ^ 0xFFFFFFFFu;
	};

	// チャンク書き出しラムダ
	auto writeChunk = [&](const char* type, const std::uint8_t* data, std::size_t len) {
		const auto l = static_cast<std::uint32_t>(len);
		png.push_back(static_cast<std::uint8_t>((l >> 24) & 0xFF));
		png.push_back(static_cast<std::uint8_t>((l >> 16) & 0xFF));
		png.push_back(static_cast<std::uint8_t>((l >> 8) & 0xFF));
		png.push_back(static_cast<std::uint8_t>(l & 0xFF));
		png.push_back(static_cast<std::uint8_t>(type[0]));
		png.push_back(static_cast<std::uint8_t>(type[1]));
		png.push_back(static_cast<std::uint8_t>(type[2]));
		png.push_back(static_cast<std::uint8_t>(type[3]));
		if (data && len > 0)
		{
			png.insert(png.end(), data, data + len);
		}
		std::vector<std::uint8_t> crcBuf;
		crcBuf.reserve(4 + len);
		crcBuf.push_back(static_cast<std::uint8_t>(type[0]));
		crcBuf.push_back(static_cast<std::uint8_t>(type[1]));
		crcBuf.push_back(static_cast<std::uint8_t>(type[2]));
		crcBuf.push_back(static_cast<std::uint8_t>(type[3]));
		if (data && len > 0)
		{
			crcBuf.insert(crcBuf.end(), data, data + len);
		}
		const auto c = crc32(crcBuf.data(), crcBuf.size());
		png.push_back(static_cast<std::uint8_t>((c >> 24) & 0xFF));
		png.push_back(static_cast<std::uint8_t>((c >> 16) & 0xFF));
		png.push_back(static_cast<std::uint8_t>((c >> 8) & 0xFF));
		png.push_back(static_cast<std::uint8_t>(c & 0xFF));
	};

	// IHDR
	std::uint8_t ihdr[13];
	const auto w = static_cast<std::uint32_t>(width);
	const auto h = static_cast<std::uint32_t>(height);
	ihdr[0] = static_cast<std::uint8_t>((w >> 24) & 0xFF);
	ihdr[1] = static_cast<std::uint8_t>((w >> 16) & 0xFF);
	ihdr[2] = static_cast<std::uint8_t>((w >> 8) & 0xFF);
	ihdr[3] = static_cast<std::uint8_t>(w & 0xFF);
	ihdr[4] = static_cast<std::uint8_t>((h >> 24) & 0xFF);
	ihdr[5] = static_cast<std::uint8_t>((h >> 16) & 0xFF);
	ihdr[6] = static_cast<std::uint8_t>((h >> 8) & 0xFF);
	ihdr[7] = static_cast<std::uint8_t>(h & 0xFF);
	ihdr[8] = 8;  // bit depth
	ihdr[9] = 6;  // color type: RGBA
	ihdr[10] = 0; // compression
	ihdr[11] = 0; // filter
	ihdr[12] = 0; // interlace
	writeChunk("IHDR", ihdr, 13);

	// IDAT
	writeChunk("IDAT", deflateData.data(), deflateData.size());

	// IEND
	writeChunk("IEND", nullptr, 0);

	return png;
}

/// @brief ニアレストネイバーでRGBA8ピクセルをリサイズする
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
