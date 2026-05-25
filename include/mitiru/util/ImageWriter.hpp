#pragma once

/// @file ImageWriter.hpp
/// @brief RGBA8ピクセルデータの画像ファイル保存ユーティリティ（外部依存なし）

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace mitiru::util {

/// @brief RGBA8ピクセルデータをBMPファイルとして保存する（外部依存なし）
/// @param path ファイルパス
/// @param pixels RGBA8ピクセルデータ
/// @param w 画像幅
/// @param h 画像高さ
inline void saveBmp(const std::string& path, const std::vector<std::uint8_t>& pixels,
	int w, int h)
{
	const int rowBytes = w * 3;
	const int paddedRowBytes = (rowBytes + 3) & ~3;
	const int imageSize = paddedRowBytes * h;
	const int fileSize = 54 + imageSize;

	std::ofstream ofs(path, std::ios::binary);
	if (!ofs)
	{
		return;
	}

	// BMPファイルヘッダー (14 bytes)
	auto write16 = [&](std::uint16_t v) {
		ofs.put(static_cast<char>(v & 0xFF));
		ofs.put(static_cast<char>((v >> 8) & 0xFF));
	};
	auto write32 = [&](std::uint32_t v) {
		ofs.put(static_cast<char>(v & 0xFF));
		ofs.put(static_cast<char>((v >> 8) & 0xFF));
		ofs.put(static_cast<char>((v >> 16) & 0xFF));
		ofs.put(static_cast<char>((v >> 24) & 0xFF));
	};

	ofs.put('B'); ofs.put('M');
	write32(static_cast<std::uint32_t>(fileSize));
	write16(0); write16(0);
	write32(54); // ピクセルデータへのオフセット

	// DIBヘッダー (40 bytes - BITMAPINFOHEADER)
	write32(40);
	write32(static_cast<std::uint32_t>(w));
	write32(static_cast<std::uint32_t>(h));
	write16(1);  // プレーン数
	write16(24); // 1ピクセルあたりのビット数
	write32(0);  // 圧縮（なし）
	write32(static_cast<std::uint32_t>(imageSize));
	write32(2835); write32(2835); // pixels per meter（解像度）
	write32(0); write32(0);

	// ピクセルデータ（ボトムアップ・BGR）
	const std::uint8_t pad[3] = {0, 0, 0};
	const int padBytes = paddedRowBytes - rowBytes;
	for (int y = h - 1; y >= 0; --y)
	{
		for (int x = 0; x < w; ++x)
		{
			const int idx = (y * w + x) * 4;
			ofs.put(static_cast<char>(pixels[static_cast<std::size_t>(idx + 2)])); // 青
			ofs.put(static_cast<char>(pixels[static_cast<std::size_t>(idx + 1)])); // 緑
			ofs.put(static_cast<char>(pixels[static_cast<std::size_t>(idx + 0)])); // 赤
		}
		if (padBytes > 0)
		{
			ofs.write(reinterpret_cast<const char*>(pad), padBytes);
		}
	}
}

/// @brief RGBA8ピクセルデータをPNGファイルとして保存する（外部依存なし・非圧縮PNG）
/// @param path ファイルパス
/// @param pixels RGBA8ピクセルデータ
/// @param w 画像幅
/// @param h 画像高さ
inline void savePng(const std::string& path, const std::vector<std::uint8_t>& pixels,
	int w, int h)
{
	if (pixels.empty() || w <= 0 || h <= 0) { return; }

	// 各行: フィルタバイト(0) + RGBA * width
	const auto rowBytes = static_cast<std::size_t>(w) * 4 + 1;
	const auto rawSize = rowBytes * static_cast<std::size_t>(h);

	// フィルタバイト付きの生ピクセルデータ
	std::vector<std::uint8_t> rawData;
	rawData.reserve(rawSize);
	for (int y = 0; y < h; ++y)
	{
		rawData.push_back(0); // フィルタ: None
		const auto* row = pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4;
		rawData.insert(rawData.end(), row, row + static_cast<std::size_t>(w) * 4);
	}

	// deflate（stored block・無圧縮）
	std::vector<std::uint8_t> deflateData;
	deflateData.reserve(rawSize + 64);
	deflateData.push_back(0x78); // CMF
	deflateData.push_back(0x01); // FLG

	std::size_t pos = 0;
	while (pos < rawData.size())
	{
		const auto remain = rawData.size() - pos;
		const auto blockSize = (remain > 65535) ? std::size_t{65535} : remain;
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
	for (const auto b : rawData) { s1 = (s1 + b) % 65521; s2 = (s2 + s1) % 65521; }
	const std::uint32_t adler = (s2 << 16) | s1;
	deflateData.push_back(static_cast<std::uint8_t>((adler >> 24) & 0xFF));
	deflateData.push_back(static_cast<std::uint8_t>((adler >> 16) & 0xFF));
	deflateData.push_back(static_cast<std::uint8_t>((adler >> 8) & 0xFF));
	deflateData.push_back(static_cast<std::uint8_t>(adler & 0xFF));

	// CRC-32
	auto crc32 = [](const std::uint8_t* data, std::size_t len) -> std::uint32_t {
		static std::uint32_t table[256] = {};
		static bool built = false;
		if (!built)
		{
			for (std::uint32_t i = 0; i < 256; ++i)
			{
				std::uint32_t c = i;
				for (int k = 0; k < 8; ++k) { c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1); }
				table[i] = c;
			}
			built = true;
		}
		std::uint32_t crc = 0xFFFFFFFF;
		for (std::size_t i = 0; i < len; ++i) { crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8); }
		return crc ^ 0xFFFFFFFF;
	};

	auto writeChunk = [&](std::vector<std::uint8_t>& out, const char* type,
		const std::uint8_t* data, std::size_t len) {
		auto push32be = [&](std::uint32_t v) {
			out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
			out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
			out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
			out.push_back(static_cast<std::uint8_t>(v & 0xFF));
		};
		push32be(static_cast<std::uint32_t>(len));
		const auto typeStart = out.size();
		out.insert(out.end(), type, type + 4);
		if (data && len > 0) { out.insert(out.end(), data, data + len); }
		const auto crc = crc32(&out[typeStart], 4 + len);
		push32be(crc);
	};

	// PNGを構築する
	std::vector<std::uint8_t> png;
	png.reserve(deflateData.size() + 128);

	const std::uint8_t sig[] = {137, 80, 78, 71, 13, 10, 26, 10};
	png.insert(png.end(), sig, sig + 8);

	// IHDR
	std::uint8_t ihdr[13] = {};
	ihdr[0] = static_cast<std::uint8_t>((w >> 24) & 0xFF);
	ihdr[1] = static_cast<std::uint8_t>((w >> 16) & 0xFF);
	ihdr[2] = static_cast<std::uint8_t>((w >> 8) & 0xFF);
	ihdr[3] = static_cast<std::uint8_t>(w & 0xFF);
	ihdr[4] = static_cast<std::uint8_t>((h >> 24) & 0xFF);
	ihdr[5] = static_cast<std::uint8_t>((h >> 16) & 0xFF);
	ihdr[6] = static_cast<std::uint8_t>((h >> 8) & 0xFF);
	ihdr[7] = static_cast<std::uint8_t>(h & 0xFF);
	ihdr[8] = 8;  // ビット深度
	ihdr[9] = 6;  // カラータイプ: RGBA
	writeChunk(png, "IHDR", ihdr, 13);

	// IDAT
	writeChunk(png, "IDAT", deflateData.data(), deflateData.size());

	// IEND
	writeChunk(png, "IEND", nullptr, 0);

	// ファイルへ書き出す
	std::ofstream ofs(path, std::ios::binary);
	if (ofs)
	{
		ofs.write(reinterpret_cast<const char*>(png.data()),
			static_cast<std::streamsize>(png.size()));
	}
}

} // namespace mitiru::util
