#pragma once

/// @file TextureCompressor.hpp
/// @brief BCn テクスチャブロック圧縮 + .mitex 中間フォーマット
/// @details BC1/BC3/BC4/BC5 ブロック圧縮を実装し、.mitex 形式で保存・読込する。
///          ミップマップ生成機能も提供する。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace mitiru::asset
{

/// @brief ブロック圧縮フォーマット
enum class CompressionFormat
{
	BC1_RGB,  ///< 4:1 圧縮、アルファなし (DXT1)
	BC3_RGBA, ///< 4:1 圧縮、独立アルファ (DXT5)
	BC4_R,    ///< 単チャンネル (ハイトマップ、グレースケール)
	BC5_RG,   ///< 2チャンネル (法線マップ)
	BC7_RGBA, ///< 高品質RGBA (BC3にフォールバック)
	None      ///< 非圧縮
};

/// @brief 圧縮済みテクスチャデータ
struct CompressedTexture
{
	int width = 0;
	int height = 0;
	CompressionFormat format = CompressionFormat::None;
	int mipLevels = 1;
	std::vector<std::vector<uint8_t>> mipData; ///< ミップレベルごとの圧縮ブロック
};

/// @brief .mitex ファイルヘッダー (32バイト固定)
struct MitexHeader
{
	char magic[4] = {'M', 'T', 'E', 'X'};
	uint32_t version = 1;
	int32_t width = 0;
	int32_t height = 0;
	uint32_t format = 0;
	uint32_t mipLevels = 0;
	uint32_t reserved[2] = {0, 0};
};

/// @brief BCnテクスチャ圧縮器
/// @details RGBA8ピクセルデータからBCn圧縮ブロックを生成する。
///          .mitexフォーマットでの保存・読込も提供。
///
/// @code
/// auto compressed = mitiru::asset::TextureCompressor::compress(
///     pixels, 256, 256, CompressionFormat::BC1_RGB);
/// TextureCompressor::save(compressed, "output.mitex");
/// auto loaded = TextureCompressor::load("output.mitex");
/// @endcode
class TextureCompressor
{
public:
	/// @brief RGBA8ピクセルをブロック圧縮する
	/// @param pixels RGBA8ピクセルデータ
	/// @param w 幅
	/// @param h 高さ
	/// @param fmt 圧縮フォーマット
	/// @return 圧縮済みテクスチャ
	[[nodiscard]] static CompressedTexture compress(
		const uint8_t* pixels, int w, int h, CompressionFormat fmt)
	{
		CompressedTexture result;
		result.width = w;
		result.height = h;
		result.format = fmt;
		result.mipLevels = 1;

		if (!pixels || w <= 0 || h <= 0)
		{
			return result;
		}

		if (fmt == CompressionFormat::None)
		{
			const auto size = static_cast<size_t>(w) * h * 4;
			result.mipData.emplace_back(pixels, pixels + size);
			return result;
		}

		result.mipData.push_back(compressLevel(pixels, w, h, fmt));
		return result;
	}

	/// @brief ミップマップ付きで圧縮する
	/// @param pixels RGBA8ピクセルデータ
	/// @param w 幅
	/// @param h 高さ
	/// @param fmt 圧縮フォーマット
	/// @param maxMips 最大ミップ数 (0=全レベル)
	/// @return 圧縮済みテクスチャ (各ミップレベル含む)
	[[nodiscard]] static CompressedTexture compressWithMips(
		const uint8_t* pixels, int w, int h, CompressionFormat fmt, int maxMips = 0)
	{
		CompressedTexture result;
		result.width = w;
		result.height = h;
		result.format = fmt;

		if (!pixels || w <= 0 || h <= 0)
		{
			result.mipLevels = 0;
			return result;
		}

		const int totalMips = (maxMips > 0)
			? maxMips
			: computeMipCount(w, h);

		std::vector<uint8_t> currentPixels(pixels, pixels + static_cast<size_t>(w) * h * 4);
		int mw = w;
		int mh = h;

		for (int level = 0; level < totalMips; ++level)
		{
			if (fmt == CompressionFormat::None)
			{
				result.mipData.push_back(currentPixels);
			}
			else
			{
				result.mipData.push_back(
					compressLevel(currentPixels.data(), mw, mh, fmt));
			}

			if (mw == 1 && mh == 1)
			{
				break;
			}

			currentPixels = downsample(currentPixels, mw, mh);
			mw = std::max(1, mw / 2);
			mh = std::max(1, mh / 2);
		}

		result.mipLevels = static_cast<int>(result.mipData.size());
		return result;
	}

	/// @brief .mitex ファイルに保存する
	/// @param tex 圧縮済みテクスチャ
	/// @param path 出力ファイルパス
	/// @return 成功時 true
	static bool save(const CompressedTexture& tex, const std::string& path)
	{
		std::ofstream file(path, std::ios::binary);
		if (!file.is_open())
		{
			return false;
		}

		MitexHeader header;
		header.width = tex.width;
		header.height = tex.height;
		header.format = static_cast<uint32_t>(tex.format);
		header.mipLevels = static_cast<uint32_t>(tex.mipLevels);

		file.write(reinterpret_cast<const char*>(&header), sizeof(header));

		for (const auto& mip : tex.mipData)
		{
			const auto size = static_cast<uint32_t>(mip.size());
			file.write(reinterpret_cast<const char*>(&size), sizeof(size));
			file.write(reinterpret_cast<const char*>(mip.data()), mip.size());
		}

		return file.good();
	}

	/// @brief .mitex ファイルを読み込む
	/// @param path 入力ファイルパス
	/// @return 圧縮済みテクスチャ、失敗時は nullopt
	[[nodiscard]] static std::optional<CompressedTexture> load(const std::string& path)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file.is_open())
		{
			return std::nullopt;
		}

		MitexHeader header;
		file.read(reinterpret_cast<char*>(&header), sizeof(header));

		if (!file.good()
			|| header.magic[0] != 'M' || header.magic[1] != 'T'
			|| header.magic[2] != 'E' || header.magic[3] != 'X')
		{
			return std::nullopt;
		}

		CompressedTexture tex;
		tex.width = header.width;
		tex.height = header.height;
		tex.format = static_cast<CompressionFormat>(header.format);
		tex.mipLevels = static_cast<int>(header.mipLevels);

		for (uint32_t i = 0; i < header.mipLevels; ++i)
		{
			uint32_t size = 0;
			file.read(reinterpret_cast<char*>(&size), sizeof(size));
			if (!file.good())
			{
				return std::nullopt;
			}

			std::vector<uint8_t> data(size);
			file.read(reinterpret_cast<char*>(data.data()), size);
			if (!file.good())
			{
				return std::nullopt;
			}

			tex.mipData.push_back(std::move(data));
		}

		return tex;
	}

	/// @brief 使用目的に基づいてフォーマットを推奨する
	/// @param hasAlpha アルファチャンネルを含むか
	/// @param isNormalMap 法線マップか
	/// @return 推奨フォーマット
	[[nodiscard]] static CompressionFormat recommendFormat(
		bool hasAlpha, bool isNormalMap)
	{
		if (isNormalMap)
		{
			return CompressionFormat::BC5_RG;
		}
		return hasAlpha ? CompressionFormat::BC3_RGBA : CompressionFormat::BC1_RGB;
	}

private:
	/// @brief ミップレベル数を計算する
	[[nodiscard]] static int computeMipCount(int w, int h)
	{
		int count = 1;
		while (w > 1 || h > 1)
		{
			w = std::max(1, w / 2);
			h = std::max(1, h / 2);
			++count;
		}
		return count;
	}

	/// @brief RGBA8画像を半分にダウンサンプルする (box filter)
	[[nodiscard]] static std::vector<uint8_t> downsample(
		const std::vector<uint8_t>& src, int w, int h)
	{
		const int nw = std::max(1, w / 2);
		const int nh = std::max(1, h / 2);
		std::vector<uint8_t> dst(static_cast<size_t>(nw) * nh * 4);

		for (int y = 0; y < nh; ++y)
		{
			for (int x = 0; x < nw; ++x)
			{
				const int sx = std::min(x * 2, w - 1);
				const int sy = std::min(y * 2, h - 1);
				const int sx1 = std::min(sx + 1, w - 1);
				const int sy1 = std::min(sy + 1, h - 1);

				for (int c = 0; c < 4; ++c)
				{
					const int sum =
						src[static_cast<size_t>((sy * w + sx) * 4 + c)] +
						src[static_cast<size_t>((sy * w + sx1) * 4 + c)] +
						src[static_cast<size_t>((sy1 * w + sx) * 4 + c)] +
						src[static_cast<size_t>((sy1 * w + sx1) * 4 + c)];
					dst[static_cast<size_t>((y * nw + x) * 4 + c)] =
						static_cast<uint8_t>(sum / 4);
				}
			}
		}
		return dst;
	}

	/// @brief 1ミップレベルを圧縮する
	[[nodiscard]] static std::vector<uint8_t> compressLevel(
		const uint8_t* pixels, int w, int h, CompressionFormat fmt)
	{
		switch (fmt)
		{
		case CompressionFormat::BC1_RGB:
			return compressBC1(pixels, w, h);
		case CompressionFormat::BC3_RGBA:
		case CompressionFormat::BC7_RGBA: // BC7 は BC3 にフォールバック
			return compressBC3(pixels, w, h);
		case CompressionFormat::BC4_R:
			return compressBC4(pixels, w, h);
		case CompressionFormat::BC5_RG:
			return compressBC5(pixels, w, h);
		case CompressionFormat::None:
		{
			const auto size = static_cast<size_t>(w) * h * 4;
			return {pixels, pixels + size};
		}
		}
		return {};
	}

	/// @brief 4x4 ブロックのRGBAピクセルを抽出する (境界クランプ)
	static void extractBlock(const uint8_t* pixels, int w, int h,
		int bx, int by, uint8_t block[64])
	{
		for (int row = 0; row < 4; ++row)
		{
			for (int col = 0; col < 4; ++col)
			{
				const int px = std::min(bx + col, w - 1);
				const int py = std::min(by + row, h - 1);
				const auto srcIdx = static_cast<size_t>((py * w + px) * 4);
				const auto dstIdx = static_cast<size_t>((row * 4 + col) * 4);
				block[dstIdx + 0] = pixels[srcIdx + 0];
				block[dstIdx + 1] = pixels[srcIdx + 1];
				block[dstIdx + 2] = pixels[srcIdx + 2];
				block[dstIdx + 3] = pixels[srcIdx + 3];
			}
		}
	}

	/// @brief RGB565 にパックする
	[[nodiscard]] static uint16_t packRGB565(uint8_t r, uint8_t g, uint8_t b)
	{
		return static_cast<uint16_t>(
			((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
	}

	/// @brief RGB565 からアンパックする
	static void unpackRGB565(uint16_t packed, uint8_t& r, uint8_t& g, uint8_t& b)
	{
		r = static_cast<uint8_t>(((packed >> 11) & 0x1F) * 255 / 31);
		g = static_cast<uint8_t>(((packed >> 5) & 0x3F) * 255 / 63);
		b = static_cast<uint8_t>((packed & 0x1F) * 255 / 31);
	}

	/// @brief ブロックの最小・最大RGB色を求める (bounding box)
	static void findMinMaxColors(const uint8_t block[64],
		uint8_t minColor[3], uint8_t maxColor[3])
	{
		minColor[0] = minColor[1] = minColor[2] = 255;
		maxColor[0] = maxColor[1] = maxColor[2] = 0;

		for (int i = 0; i < 16; ++i)
		{
			const auto idx = static_cast<size_t>(i * 4);
			for (int c = 0; c < 3; ++c)
			{
				minColor[c] = std::min(minColor[c], block[idx + c]);
				maxColor[c] = std::max(maxColor[c], block[idx + c]);
			}
		}

		// 色差を微調整 (inset) して品質向上
		for (int c = 0; c < 3; ++c)
		{
			const int delta = maxColor[c] - minColor[c];
			minColor[c] = static_cast<uint8_t>(
				std::min(255, minColor[c] + delta / 16));
			maxColor[c] = static_cast<uint8_t>(
				std::max(0, maxColor[c] - delta / 16));
		}
	}

	/// @brief BC1 (DXT1) ブロック圧縮
	[[nodiscard]] static std::vector<uint8_t> compressBC1(
		const uint8_t* pixels, int w, int h)
	{
		const int bw = (w + 3) / 4;
		const int bh = (h + 3) / 4;
		std::vector<uint8_t> output(static_cast<size_t>(bw) * bh * 8);

		for (int by = 0; by < bh; ++by)
		{
			for (int bx = 0; bx < bw; ++bx)
			{
				uint8_t block[64];
				extractBlock(pixels, w, h, bx * 4, by * 4, block);

				uint8_t minC[3], maxC[3];
				findMinMaxColors(block, minC, maxC);

				const uint16_t color0 = packRGB565(maxC[0], maxC[1], maxC[2]);
				const uint16_t color1 = packRGB565(minC[0], minC[1], minC[2]);

				// パレット生成 (4色)
				uint8_t palette[4][3];
				unpackRGB565(color0, palette[0][0], palette[0][1], palette[0][2]);
				unpackRGB565(color1, palette[1][0], palette[1][1], palette[1][2]);

				if (color0 > color1)
				{
					for (int c = 0; c < 3; ++c)
					{
						palette[2][c] = static_cast<uint8_t>(
							(2 * palette[0][c] + palette[1][c]) / 3);
						palette[3][c] = static_cast<uint8_t>(
							(palette[0][c] + 2 * palette[1][c]) / 3);
					}
				}
				else
				{
					for (int c = 0; c < 3; ++c)
					{
						palette[2][c] = static_cast<uint8_t>(
							(palette[0][c] + palette[1][c]) / 2);
					}
					palette[3][0] = palette[3][1] = palette[3][2] = 0;
				}

				// 各ピクセルを最近パレット色に割り当て
				uint32_t indices = 0;
				for (int i = 0; i < 16; ++i)
				{
					const auto pi = static_cast<size_t>(i * 4);
					int bestDist = INT32_MAX;
					int bestIdx = 0;
					for (int p = 0; p < 4; ++p)
					{
						const int dr = block[pi + 0] - palette[p][0];
						const int dg = block[pi + 1] - palette[p][1];
						const int db = block[pi + 2] - palette[p][2];
						const int dist = dr * dr + dg * dg + db * db;
						if (dist < bestDist)
						{
							bestDist = dist;
							bestIdx = p;
						}
					}
					indices |= static_cast<uint32_t>(bestIdx) << (i * 2);
				}

				// 出力: color0(2) + color1(2) + indices(4) = 8バイト
				const auto outIdx = static_cast<size_t>((by * bw + bx) * 8);
				const uint16_t c0 = std::max(color0, color1);
				const uint16_t c1 = std::min(color0, color1);
				std::memcpy(&output[outIdx + 0], &c0, 2);
				std::memcpy(&output[outIdx + 2], &c1, 2);
				std::memcpy(&output[outIdx + 4], &indices, 4);
			}
		}
		return output;
	}

	/// @brief 単チャンネルアルファブロック圧縮 (BC3アルファ部 / BC4)
	static void compressAlphaBlock(const uint8_t values[16], uint8_t out[8])
	{
		uint8_t minVal = 255, maxVal = 0;
		for (int i = 0; i < 16; ++i)
		{
			minVal = std::min(minVal, values[i]);
			maxVal = std::max(maxVal, values[i]);
		}

		out[0] = maxVal;
		out[1] = minVal;

		// 8レベルパレット
		uint8_t palette[8];
		palette[0] = maxVal;
		palette[1] = minVal;
		if (maxVal > minVal)
		{
			for (int i = 1; i <= 6; ++i)
			{
				palette[i + 1] = static_cast<uint8_t>(
					((7 - i) * maxVal + i * minVal) / 7);
			}
		}
		else
		{
			for (int i = 1; i <= 4; ++i)
			{
				palette[i + 1] = static_cast<uint8_t>(
					((5 - i) * maxVal + i * minVal) / 5);
			}
			palette[6] = 0;
			palette[7] = 255;
		}

		// 48ビットインデックスをパック
		uint64_t bits = 0;
		for (int i = 0; i < 16; ++i)
		{
			int bestDist = INT32_MAX;
			int bestIdx = 0;
			for (int p = 0; p < 8; ++p)
			{
				const int d = std::abs(values[i] - palette[p]);
				if (d < bestDist)
				{
					bestDist = d;
					bestIdx = p;
				}
			}
			bits |= static_cast<uint64_t>(bestIdx) << (i * 3);
		}

		// 48ビットを6バイトに書き込み
		for (int i = 0; i < 6; ++i)
		{
			out[2 + i] = static_cast<uint8_t>((bits >> (i * 8)) & 0xFF);
		}
	}

	/// @brief BC3 (DXT5) ブロック圧縮
	[[nodiscard]] static std::vector<uint8_t> compressBC3(
		const uint8_t* pixels, int w, int h)
	{
		const int bw = (w + 3) / 4;
		const int bh = (h + 3) / 4;
		std::vector<uint8_t> output(static_cast<size_t>(bw) * bh * 16);

		const auto bc1Data = compressBC1(pixels, w, h);

		for (int by = 0; by < bh; ++by)
		{
			for (int bx = 0; bx < bw; ++bx)
			{
				uint8_t block[64];
				extractBlock(pixels, w, h, bx * 4, by * 4, block);

				// アルファ値を抽出
				uint8_t alphas[16];
				for (int i = 0; i < 16; ++i)
				{
					alphas[i] = block[i * 4 + 3];
				}

				const auto outIdx = static_cast<size_t>((by * bw + bx) * 16);
				// アルファブロック (8バイト)
				compressAlphaBlock(alphas, &output[outIdx]);
				// カラーブロック (8バイト) — BC1から流用
				const auto bc1Idx = static_cast<size_t>((by * bw + bx) * 8);
				std::memcpy(&output[outIdx + 8], &bc1Data[bc1Idx], 8);
			}
		}
		return output;
	}

	/// @brief BC4 (単チャンネル) ブロック圧縮
	[[nodiscard]] static std::vector<uint8_t> compressBC4(
		const uint8_t* pixels, int w, int h)
	{
		const int bw = (w + 3) / 4;
		const int bh = (h + 3) / 4;
		std::vector<uint8_t> output(static_cast<size_t>(bw) * bh * 8);

		for (int by = 0; by < bh; ++by)
		{
			for (int bx = 0; bx < bw; ++bx)
			{
				uint8_t block[64];
				extractBlock(pixels, w, h, bx * 4, by * 4, block);

				uint8_t reds[16];
				for (int i = 0; i < 16; ++i)
				{
					reds[i] = block[i * 4]; // R チャンネル
				}

				const auto outIdx = static_cast<size_t>((by * bw + bx) * 8);
				compressAlphaBlock(reds, &output[outIdx]);
			}
		}
		return output;
	}

	/// @brief BC5 (2チャンネル) ブロック圧縮
	[[nodiscard]] static std::vector<uint8_t> compressBC5(
		const uint8_t* pixels, int w, int h)
	{
		const int bw = (w + 3) / 4;
		const int bh = (h + 3) / 4;
		std::vector<uint8_t> output(static_cast<size_t>(bw) * bh * 16);

		for (int by = 0; by < bh; ++by)
		{
			for (int bx = 0; bx < bw; ++bx)
			{
				uint8_t block[64];
				extractBlock(pixels, w, h, bx * 4, by * 4, block);

				uint8_t reds[16], greens[16];
				for (int i = 0; i < 16; ++i)
				{
					reds[i] = block[i * 4];
					greens[i] = block[i * 4 + 1];
				}

				const auto outIdx = static_cast<size_t>((by * bw + bx) * 16);
				compressAlphaBlock(reds, &output[outIdx]);
				compressAlphaBlock(greens, &output[outIdx + 8]);
			}
		}
		return output;
	}
};

} // namespace mitiru::asset
