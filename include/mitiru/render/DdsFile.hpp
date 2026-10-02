#pragma once

/// @file DdsFile.hpp
/// @brief mip 連鎖つきテクスチャ (MipImage) と DDS (DX10 拡張ヘッダ) の読み書き
/// @details GPU がそのまま受け取れる形 (BC7 / BC5 / BC4 / RGBA8) を 1 ファイルに持つ。
///          DX12 は DXGI_FORMAT をそのまま受けるので、コンテナは DXGI の値を直に書ける DDS にした。
///          読むのは自分が書く形 (DX10 ヘッダ・2D・配列 1・下表の形式) だけで、それ以外は理由付きで断る。

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mitiru::render
{

/// @brief 画素形式。値は DXGI_FORMAT と同じ (DX12 側はキャストだけで渡せる)
enum class TextureFormat : std::uint32_t
{
	Rgba8 = 28,
	Rgba8Srgb = 29,
	Bc4 = 80,
	Bc5 = 83,
	Bc7 = 98,
	Bc7Srgb = 99,
};

/// @brief 全 mip を持つ画像。mips[0] がフル解像度、各段は詰めた行 (BC は 4x4 ブロック行)
struct MipImage
{
	TextureFormat format = TextureFormat::Rgba8;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	bool hasAlpha = false;   ///< 抜き (アルファテスト) が要る画素を含む
	std::vector<std::vector<std::uint8_t>> mips;
};

[[nodiscard]] constexpr bool isBlockCompressed(TextureFormat f) noexcept
{
	return f == TextureFormat::Bc4 || f == TextureFormat::Bc5 || f == TextureFormat::Bc7 ||
	       f == TextureFormat::Bc7Srgb;
}

/// @brief 1 行 (BC は 1 ブロック行) のバイト数
[[nodiscard]] constexpr std::size_t textureRowBytes(TextureFormat f, std::uint32_t width) noexcept
{
	if (!isBlockCompressed(f)) { return static_cast<std::size_t>(width) * 4; }
	const std::size_t blockBytes = (f == TextureFormat::Bc4) ? 8 : 16;
	return static_cast<std::size_t>((width + 3) / 4) * blockBytes;
}

/// @brief 行数 (BC はブロック行数)
[[nodiscard]] constexpr std::uint32_t textureRowCount(TextureFormat f, std::uint32_t height) noexcept
{
	return isBlockCompressed(f) ? (height + 3) / 4 : height;
}

[[nodiscard]] constexpr std::size_t textureLevelBytes(TextureFormat f, std::uint32_t w,
                                                      std::uint32_t h) noexcept
{
	return textureRowBytes(f, w) * textureRowCount(f, h);
}

[[nodiscard]] constexpr std::uint32_t mipExtent(std::uint32_t base, std::size_t level) noexcept
{
	const std::uint32_t v = base >> level;
	return v > 0 ? v : 1;
}

namespace detail
{

inline constexpr std::uint32_t kDdsMagic = 0x20534444u;   // "DDS "
inline constexpr std::uint32_t kDdsFourCcDx10 = 0x30315844u;   // "DX10"
inline constexpr std::uint32_t kDdsAlphaStraight = 1;
inline constexpr std::uint32_t kDdsAlphaOpaque = 3;

struct DdsPixelFormat
{
	std::uint32_t size, flags, fourCC, rgbBitCount, rMask, gMask, bMask, aMask;
};

struct DdsHeader
{
	std::uint32_t size, flags, height, width, pitchOrLinearSize, depth, mipMapCount;
	std::uint32_t reserved1[11];
	DdsPixelFormat pixelFormat;
	std::uint32_t caps, caps2, caps3, caps4, reserved2;
};

struct DdsHeaderDx10
{
	std::uint32_t dxgiFormat, resourceDimension, miscFlag, arraySize, miscFlags2;
};

static_assert(sizeof(DdsHeader) == 124);
static_assert(sizeof(DdsHeaderDx10) == 20);

inline constexpr std::size_t kDdsPrefixBytes = 4 + sizeof(DdsHeader) + sizeof(DdsHeaderDx10);

[[nodiscard]] constexpr bool isKnownFormat(std::uint32_t v) noexcept
{
	return v == 28 || v == 29 || v == 80 || v == 83 || v == 98 || v == 99;
}

} // namespace detail

/// @brief MipImage を DDS バイト列にする (mips の段数・各段の大きさは呼び出し側が正しく持つこと)
[[nodiscard]] inline std::vector<std::uint8_t> encodeDds(const MipImage& img)
{
	detail::DdsHeader h = {};
	h.size = sizeof(detail::DdsHeader);
	h.flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | (isBlockCompressed(img.format) ? 0x80000 : 0x8);
	h.height = img.height;
	h.width = img.width;
	h.pitchOrLinearSize = static_cast<std::uint32_t>(
		isBlockCompressed(img.format) ? textureLevelBytes(img.format, img.width, img.height)
		                              : textureRowBytes(img.format, img.width));
	h.depth = 1;
	h.mipMapCount = static_cast<std::uint32_t>(img.mips.size());
	h.pixelFormat.size = sizeof(detail::DdsPixelFormat);
	h.pixelFormat.flags = 0x4;   // DDPF_FOURCC
	h.pixelFormat.fourCC = detail::kDdsFourCcDx10;
	h.caps = 0x1000 | (img.mips.size() > 1 ? (0x400000 | 0x8) : 0);
	detail::DdsHeaderDx10 x = {};
	x.dxgiFormat = static_cast<std::uint32_t>(img.format);
	x.resourceDimension = 3;   // TEXTURE2D
	x.arraySize = 1;
	x.miscFlags2 = img.hasAlpha ? detail::kDdsAlphaStraight : detail::kDdsAlphaOpaque;

	std::vector<std::uint8_t> out(detail::kDdsPrefixBytes);
	std::memcpy(out.data(), &detail::kDdsMagic, 4);
	std::memcpy(out.data() + 4, &h, sizeof(h));
	std::memcpy(out.data() + 4 + sizeof(h), &x, sizeof(x));
	for (const auto& level : img.mips) { out.insert(out.end(), level.begin(), level.end()); }
	return out;
}

/// @brief DDS を読む。対応外・壊れたファイルは nullopt と error に理由
[[nodiscard]] inline std::optional<MipImage> decodeDds(std::span<const std::uint8_t> bytes,
                                                       std::string& error)
{
	if (bytes.size() < detail::kDdsPrefixBytes)
	{
		error = "DDS が短すぎる";
		return std::nullopt;
	}
	std::uint32_t magic = 0;
	detail::DdsHeader h = {};
	detail::DdsHeaderDx10 x = {};
	std::memcpy(&magic, bytes.data(), 4);
	std::memcpy(&h, bytes.data() + 4, sizeof(h));
	std::memcpy(&x, bytes.data() + 4 + sizeof(h), sizeof(x));
	if (magic != detail::kDdsMagic || h.size != sizeof(h) || h.pixelFormat.fourCC != detail::kDdsFourCcDx10)
	{
		error = "DX10 拡張ヘッダ付きの DDS ではない";
		return std::nullopt;
	}
	if (x.resourceDimension != 3 || x.arraySize != 1 || !detail::isKnownFormat(x.dxgiFormat))
	{
		error = "2D・配列 1・BC4/BC5/BC7/RGBA8 以外の DDS は読まない (DXGI " +
		        std::to_string(x.dxgiFormat) + ")";
		return std::nullopt;
	}
	if (h.width == 0 || h.height == 0 || h.width > 16384 || h.height > 16384 || h.mipMapCount > 15)
	{
		error = "DDS の寸法か mip 段数が範囲外";
		return std::nullopt;
	}

	MipImage img;
	img.format = static_cast<TextureFormat>(x.dxgiFormat);
	img.width = h.width;
	img.height = h.height;
	img.hasAlpha = (x.miscFlags2 & 0x7u) == detail::kDdsAlphaStraight;
	const std::uint32_t levels = h.mipMapCount == 0 ? 1 : h.mipMapCount;
	std::size_t ofs = detail::kDdsPrefixBytes;
	for (std::uint32_t lv = 0; lv < levels; ++lv)
	{
		const std::size_t n = textureLevelBytes(img.format, mipExtent(h.width, lv), mipExtent(h.height, lv));
		if (ofs + n > bytes.size())
		{
			error = "DDS の画素データが途中で切れている";
			return std::nullopt;
		}
		img.mips.emplace_back(bytes.begin() + static_cast<std::ptrdiff_t>(ofs),
		                      bytes.begin() + static_cast<std::ptrdiff_t>(ofs + n));
		ofs += n;
	}
	return img;
}

} // namespace mitiru::render
