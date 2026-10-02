/// @file texture_compress_impl.cpp
/// @brief TextureCompress.hpp の実装。bc7enc_rdo / rgbcx はこの TU にだけ入れる

#include <mitiru/render/TextureCompress.hpp>
#include <mitiru/render/TextureMips.hpp>
#include <mitiru/util/ParallelFor.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>

#include <stb_image.h>

#if MITIRU_HAS_BC7ENC
#include <bc7decomp.h>
#include <bc7enc.h>
#include <rgbcx.h>
#endif

namespace mitiru::render
{
namespace
{

[[nodiscard]] TextureFormat formatOf(TextureKind kind) noexcept
{
	switch (kind)
	{
	case TextureKind::Normal: return TextureFormat::Bc5;
	case TextureKind::Mask:   return TextureFormat::Bc4;
	case TextureKind::Data:   return TextureFormat::Bc7;
	case TextureKind::Color:  break;
	}
	return TextureFormat::Bc7Srgb;
}

[[nodiscard]] MipFilter filterOf(TextureKind kind) noexcept
{
	switch (kind)
	{
	case TextureKind::Normal: return MipFilter::NormalMap;
	case TextureKind::Mask:   return MipFilter::Linear;
	case TextureKind::Data:   return MipFilter::Linear;
	case TextureKind::Color:  break;
	}
	return MipFilter::Srgb;
}

/// ClodScene が RGBA8 から抜きの要否を決める閾値と揃える
[[nodiscard]] bool anyTranslucent(const std::vector<std::uint8_t>& rgba) noexcept
{
	for (std::size_t i = 3; i < rgba.size(); i += 4)
	{
		if (rgba[i] < 250) { return true; }
	}
	return false;
}

/// 端のブロックは画像の外を最後の画素で埋める (4 未満の小さな mip もブロック 1 個になる)
void gatherBlock(const std::uint8_t* rgba, std::uint32_t w, std::uint32_t h, std::uint32_t bx,
                 std::uint32_t by, std::uint8_t (&out)[64]) noexcept
{
	for (std::uint32_t y = 0; y < 4; ++y)
	{
		const std::uint32_t sy = std::min(by * 4 + y, h - 1);
		for (std::uint32_t x = 0; x < 4; ++x)
		{
			const std::uint32_t sx = std::min(bx * 4 + x, w - 1);
			std::memcpy(&out[(y * 4 + x) * 4], rgba + (static_cast<std::size_t>(sy) * w + sx) * 4, 4);
		}
	}
}

#if MITIRU_HAS_BC7ENC

void initEncoders()
{
	static std::once_flag once;
	std::call_once(once, [] {
		bc7enc_compress_block_init();
		rgbcx::init();
	});
}

void encodeBlock(TextureFormat format, const std::uint8_t (&px)[64], std::uint8_t* dst,
                 const bc7enc_compress_block_params& bc7)
{
	switch (format)
	{
	case TextureFormat::Bc5: rgbcx::encode_bc5_hq(dst, px, 0, 1, 4); break;
	case TextureFormat::Bc4: rgbcx::encode_bc4_hq(dst, px, 4); break;
	default:                 bc7enc_compress_block(dst, px, &bc7); break;
	}
}

/// ブロック行ごとに並列で 1 段を符号化する
[[nodiscard]] std::vector<std::uint8_t> encodeLevel(const std::vector<std::uint8_t>& rgba,
                                                    std::uint32_t w, std::uint32_t h, TextureFormat format)
{
	bc7enc_compress_block_params bc7;
	bc7enc_compress_block_params_init(&bc7);
	const std::size_t rowBytes = textureRowBytes(format, w);
	const std::size_t blockBytes = (format == TextureFormat::Bc4) ? 8 : 16;
	std::vector<std::uint8_t> out(textureLevelBytes(format, w, h));
	util::parallelFor(textureRowCount(format, h), [&](std::size_t row) {
		const auto by = static_cast<std::uint32_t>(row);
		std::uint8_t px[64];
		for (std::uint32_t bx = 0; bx < (w + 3) / 4; ++bx)
		{
			gatherBlock(rgba.data(), w, h, bx, by, px);
			encodeBlock(format, px, out.data() + by * rowBytes + bx * blockBytes, bc7);
		}
	});
	return out;
}

void decodeBlock(TextureFormat format, const std::uint8_t* src, std::uint8_t (&px)[64])
{
	std::memset(px, 0, sizeof(px));
	for (int i = 0; i < 16; ++i) { px[i * 4 + 3] = 255; }
	switch (format)
	{
	case TextureFormat::Bc5: rgbcx::unpack_bc5(src, px, 0, 1, 4); break;
	case TextureFormat::Bc4: rgbcx::unpack_bc4(src, px, 4); break;
	default: bc7decomp::unpack_bc7(src, reinterpret_cast<bc7decomp::color_rgba*>(px)); break;
	}
}

#endif

[[nodiscard]] bool writeFileAtomically(const std::filesystem::path& dst, const std::vector<std::uint8_t>& bytes,
                                       std::string& error)
{
	const std::filesystem::path tmp(dst.string() + ".tmp");
	{
		std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
		if (!f) { error = "書けない: " + tmp.string(); return false; }
		f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}
	std::error_code ec;
	std::filesystem::rename(tmp, dst, ec);
	if (ec) { error = "rename に失敗: " + dst.string(); return false; }
	return true;
}

} // namespace

std::optional<MipImage> compressTexture(const std::uint8_t* rgba, std::uint32_t width,
                                        std::uint32_t height, TextureKind kind)
{
#if MITIRU_HAS_BC7ENC
	if (rgba == nullptr || width == 0 || height == 0 || width % 4 != 0 || height % 4 != 0)
	{
		return std::nullopt;
	}
	initEncoders();
	const auto chain = buildMipChain(rgba, static_cast<int>(width), static_cast<int>(height), filterOf(kind));
	MipImage img;
	img.format = formatOf(kind);
	img.width = width;
	img.height = height;
	img.hasAlpha = (kind == TextureKind::Color) && anyTranslucent(chain.front());
	for (std::size_t lv = 0; lv < chain.size(); ++lv)
	{
		img.mips.push_back(encodeLevel(chain[lv], mipExtent(width, lv), mipExtent(height, lv), img.format));
	}
	return img;
#else
	(void)rgba; (void)width; (void)height; (void)kind;
	return std::nullopt;
#endif
}

std::vector<std::uint8_t> decompressLevel(const MipImage& image, std::size_t level)
{
	const std::uint32_t w = mipExtent(image.width, level);
	const std::uint32_t h = mipExtent(image.height, level);
	if (level >= image.mips.size()) { return {}; }
	if (!isBlockCompressed(image.format)) { return image.mips[level]; }
	std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * 4);
#if MITIRU_HAS_BC7ENC
	const std::size_t rowBytes = textureRowBytes(image.format, w);
	const std::size_t blockBytes = (image.format == TextureFormat::Bc4) ? 8 : 16;
	std::uint8_t px[64];
	for (std::uint32_t by = 0; by < (h + 3) / 4; ++by)
	{
		for (std::uint32_t bx = 0; bx < (w + 3) / 4; ++bx)
		{
			decodeBlock(image.format, image.mips[level].data() + by * rowBytes + bx * blockBytes, px);
			for (std::uint32_t y = 0; y < 4 && by * 4 + y < h; ++y)
			{
				const std::uint32_t n = std::min(4u, w - bx * 4);
				std::memcpy(&out[((static_cast<std::size_t>(by) * 4 + y) * w + bx * 4) * 4], &px[y * 16], n * 4);
			}
		}
	}
#endif
	return out;
}

std::optional<std::string> ensureCompressedTexture(const std::string& imagePath, TextureKind kind,
                                                   std::string& error)
{
	namespace fs = std::filesystem;
	const fs::path src(imagePath);
	const fs::path dds(imagePath + ".dds");
	std::error_code ec;
	if (!fs::exists(src, ec))
	{
		error = "画像が無い: " + imagePath;
		return std::nullopt;
	}
	if (fs::exists(dds, ec) && fs::last_write_time(dds, ec) >= fs::last_write_time(src, ec))
	{
		return dds.string();
	}
	int w = 0, h = 0, comp = 0;
	std::uint8_t* pixels = stbi_load(imagePath.c_str(), &w, &h, &comp, 4);
	if (pixels == nullptr)
	{
		error = "画像を decode できない: " + imagePath;
		return std::nullopt;
	}
	const auto img = compressTexture(pixels, static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), kind);
	stbi_image_free(pixels);
	if (!img)
	{
		error = "BC 圧縮できない (辺が 4 の倍数でないか、エンコーダ無しのビルド): " + imagePath;
		return std::nullopt;
	}
	if (!writeFileAtomically(dds, encodeDds(*img), error)) { return std::nullopt; }
	return dds.string();
}

} // namespace mitiru::render
