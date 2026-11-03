#pragma once

/// @file DdsFile.hpp
/// @brief mip 連鎖つきテクスチャ (MipImage) と DDS の読み書き
/// @details GPU がそのまま受け取れる形 (BC1〜7 / BC6H / RGBA8 / RGBA16F) を 1 ファイルに持つ。
///          DX12 は DXGI_FORMAT をそのまま受けるので、コンテナは DXGI の値を直に書ける DDS にした。
///          書くのは DX10 拡張ヘッダの形だけ。読むのは DX10 ヘッダと、外部の道具が書くレガシーの FourCC
///          (DXT1〜5、ATI1/2、BC4U/BC5U、A16B16G16R16F) と 32 bit の RGBA のマスク。2D、キューブ (6 面)、配列を通し、
///          体積テクスチャと未知の形式は理由付きで断る。

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
	Rgba16F = 10,
	Rgba8 = 28,
	Rgba8Srgb = 29,
	Bc1 = 71,
	Bc1Srgb = 72,
	Bc2 = 74,
	Bc2Srgb = 75,
	Bc3 = 77,
	Bc3Srgb = 78,
	Bc4 = 80,
	Bc5 = 83,
	Bc6hUf16 = 95,
	Bc6hSf16 = 96,
	Bc7 = 98,
	Bc7Srgb = 99,
};

[[nodiscard]] constexpr bool isKnownTextureFormat(std::uint32_t v) noexcept
{
	switch (static_cast<TextureFormat>(v))
	{
	case TextureFormat::Rgba16F: case TextureFormat::Rgba8: case TextureFormat::Rgba8Srgb:
	case TextureFormat::Bc1: case TextureFormat::Bc1Srgb: case TextureFormat::Bc2: case TextureFormat::Bc2Srgb:
	case TextureFormat::Bc3: case TextureFormat::Bc3Srgb: case TextureFormat::Bc4: case TextureFormat::Bc5:
	case TextureFormat::Bc6hUf16: case TextureFormat::Bc6hSf16: case TextureFormat::Bc7: case TextureFormat::Bc7Srgb:
		return true;
	}
	return false;
}

[[nodiscard]] constexpr bool isBlockCompressed(TextureFormat f) noexcept
{
	return f != TextureFormat::Rgba8 && f != TextureFormat::Rgba8Srgb && f != TextureFormat::Rgba16F;
}

[[nodiscard]] constexpr bool isSrgbFormat(TextureFormat f) noexcept
{
	return f == TextureFormat::Rgba8Srgb || f == TextureFormat::Bc1Srgb || f == TextureFormat::Bc2Srgb ||
	       f == TextureFormat::Bc3Srgb || f == TextureFormat::Bc7Srgb;
}

/// @brief 浮動小数の形式 (BC6H と RGBA16F)。RGBA8 へは戻せない
[[nodiscard]] constexpr bool isHdrFormat(TextureFormat f) noexcept
{
	return f == TextureFormat::Bc6hUf16 || f == TextureFormat::Bc6hSf16 || f == TextureFormat::Rgba16F;
}

/// @brief 4x4 ブロック 1 個のバイト数 (BC のみ)
[[nodiscard]] constexpr std::size_t textureBlockBytes(TextureFormat f) noexcept
{
	return (f == TextureFormat::Bc1 || f == TextureFormat::Bc1Srgb || f == TextureFormat::Bc4) ? 8 : 16;
}

/// @brief 1 画素のバイト数 (非 BC のみ)
[[nodiscard]] constexpr std::size_t textureTexelBytes(TextureFormat f) noexcept
{
	return f == TextureFormat::Rgba16F ? 8 : 4;
}

/// @brief 全 mip を持つ画像。mips[0] がフル解像度、各段は詰めた行 (BC は 4x4 ブロック行)
/// @details キューブと配列は、DDS と同じ並び (面 0 の全段、面 1 の全段、...) で mips に続けて置く。
///          キューブの面の順は +X, -X, +Y, -Y, +Z, -Z
struct MipImage
{
	TextureFormat format = TextureFormat::Rgba8;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	bool hasAlpha = false;   ///< 抜き (アルファテスト) が要る画素を含む
	std::uint32_t arraySize = 1;   ///< 面か配列の枚数。キューブは 6
	bool isCube = false;
	std::vector<std::vector<std::uint8_t>> mips;

	[[nodiscard]] std::size_t levelCount() const noexcept { return arraySize == 0 ? 0 : mips.size() / arraySize; }
	[[nodiscard]] const std::vector<std::uint8_t>& level(std::uint32_t slice, std::size_t lv) const noexcept
	{
		return mips[static_cast<std::size_t>(slice) * levelCount() + lv];
	}
};

/// @brief 1 行 (BC は 1 ブロック行) のバイト数
[[nodiscard]] constexpr std::size_t textureRowBytes(TextureFormat f, std::uint32_t width) noexcept
{
	if (!isBlockCompressed(f)) { return static_cast<std::size_t>(width) * textureTexelBytes(f); }
	return static_cast<std::size_t>((width + 3) / 4) * textureBlockBytes(f);
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

inline constexpr std::uint32_t kDdpfAlphaPixels = 0x1;
inline constexpr std::uint32_t kDdpfFourCc = 0x4;
inline constexpr std::uint32_t kDdpfRgb = 0x40;
inline constexpr std::uint32_t kDdsCaps2Cubemap = 0x200;
inline constexpr std::uint32_t kDdsCaps2AllFaces = 0xFC00;
inline constexpr std::uint32_t kDdsCaps2Volume = 0x200000;
inline constexpr std::uint32_t kDx10MiscCube = 0x4;
inline constexpr std::uint32_t kDx10Texture2D = 3;

[[nodiscard]] constexpr std::uint32_t fourCc(char a, char b, char c, char d) noexcept
{
	return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) | (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8) |
	       (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16) | (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24);
}

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

inline constexpr std::size_t kDdsLegacyPrefixBytes = 4 + sizeof(DdsHeader);
inline constexpr std::size_t kDdsPrefixBytes = kDdsLegacyPrefixBytes + sizeof(DdsHeaderDx10);

/// レガシーの pixelFormat から形式を決める。sRGB かは分からないので線形の値を返す
[[nodiscard]] inline bool legacyFormatOf(const DdsPixelFormat& pf, TextureFormat& format, bool& hasAlpha, std::string& error)
{
	if (pf.flags & kDdpfFourCc)
	{
		const std::uint32_t fc = pf.fourCC;
		if (fc == fourCc('D', 'X', 'T', '1')) { format = TextureFormat::Bc1; hasAlpha = (pf.flags & kDdpfAlphaPixels) != 0; return true; }
		if (fc == fourCc('D', 'X', 'T', '2') || fc == fourCc('D', 'X', 'T', '3')) { format = TextureFormat::Bc2; hasAlpha = true; return true; }
		if (fc == fourCc('D', 'X', 'T', '4') || fc == fourCc('D', 'X', 'T', '5')) { format = TextureFormat::Bc3; hasAlpha = true; return true; }
		if (fc == fourCc('A', 'T', 'I', '1') || fc == fourCc('B', 'C', '4', 'U')) { format = TextureFormat::Bc4; hasAlpha = false; return true; }
		if (fc == fourCc('A', 'T', 'I', '2') || fc == fourCc('B', 'C', '5', 'U')) { format = TextureFormat::Bc5; hasAlpha = false; return true; }
		if (fc == 113) { format = TextureFormat::Rgba16F; hasAlpha = true; return true; }   // D3DFMT_A16B16G16R16F
		char name[5] = {static_cast<char>(fc & 0xFF), static_cast<char>((fc >> 8) & 0xFF), static_cast<char>((fc >> 16) & 0xFF),
		                static_cast<char>((fc >> 24) & 0xFF), '\0'};
		for (char& c : name) { if (c != '\0' && (c < 0x20 || c > 0x7E)) { c = '?'; } }
		error = std::string("レガシーの FourCC '") + name + "' (" + std::to_string(fc) + ") の DDS は読まない";
		return false;
	}
	if ((pf.flags & kDdpfRgb) && pf.rgbBitCount == 32 && pf.rMask == 0x000000FFu && pf.gMask == 0x0000FF00u && pf.bMask == 0x00FF0000u)
	{
		format = TextureFormat::Rgba8;
		hasAlpha = (pf.flags & kDdpfAlphaPixels) != 0 && pf.aMask == 0xFF000000u;
		return true;
	}
	error = "レガシーの DDS は DXT1〜5 / ATI1 / ATI2 / A16B16G16R16F / 32 bit の RGBA しか読まない";
	return false;
}

} // namespace detail

/// @brief MipImage を DDS バイト列にする (mips の段数・各段の大きさは呼び出し側が正しく持つこと)
[[nodiscard]] inline std::vector<std::uint8_t> encodeDds(const MipImage& img)
{
	const std::size_t levels = img.levelCount();
	detail::DdsHeader h = {};
	h.size = sizeof(detail::DdsHeader);
	h.flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | (isBlockCompressed(img.format) ? 0x80000 : 0x8);
	h.height = img.height;
	h.width = img.width;
	h.pitchOrLinearSize = static_cast<std::uint32_t>(
		isBlockCompressed(img.format) ? textureLevelBytes(img.format, img.width, img.height)
		                              : textureRowBytes(img.format, img.width));
	h.depth = 1;
	h.mipMapCount = static_cast<std::uint32_t>(levels);
	h.pixelFormat.size = sizeof(detail::DdsPixelFormat);
	h.pixelFormat.flags = detail::kDdpfFourCc;
	h.pixelFormat.fourCC = detail::kDdsFourCcDx10;
	h.caps = 0x1000 | (levels > 1 ? (0x400000 | 0x8) : 0) | (img.isCube ? 0x8 : 0);
	h.caps2 = img.isCube ? (detail::kDdsCaps2Cubemap | detail::kDdsCaps2AllFaces) : 0;
	detail::DdsHeaderDx10 x = {};
	x.dxgiFormat = static_cast<std::uint32_t>(img.format);
	x.resourceDimension = detail::kDx10Texture2D;
	x.miscFlag = img.isCube ? detail::kDx10MiscCube : 0;
	x.arraySize = img.isCube ? img.arraySize / 6 : img.arraySize;
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
	if (bytes.size() < detail::kDdsLegacyPrefixBytes)
	{
		error = "DDS が短すぎる";
		return std::nullopt;
	}
	std::uint32_t magic = 0;
	detail::DdsHeader h = {};
	std::memcpy(&magic, bytes.data(), 4);
	std::memcpy(&h, bytes.data() + 4, sizeof(h));
	if (magic != detail::kDdsMagic || h.size != sizeof(h) || h.pixelFormat.size != sizeof(detail::DdsPixelFormat))
	{
		error = "DDS のヘッダではない";
		return std::nullopt;
	}

	MipImage img;
	std::size_t ofs = detail::kDdsLegacyPrefixBytes;
	const bool dx10 = (h.pixelFormat.flags & detail::kDdpfFourCc) && h.pixelFormat.fourCC == detail::kDdsFourCcDx10;
	if (dx10)
	{
		if (bytes.size() < detail::kDdsPrefixBytes)
		{
			error = "DDS が短すぎる";
			return std::nullopt;
		}
		detail::DdsHeaderDx10 x = {};
		std::memcpy(&x, bytes.data() + 4 + sizeof(h), sizeof(x));
		ofs = detail::kDdsPrefixBytes;
		if (x.resourceDimension != detail::kDx10Texture2D)
		{
			error = "2D 以外 (1D・体積) の DDS は読まない (resourceDimension " + std::to_string(x.resourceDimension) + ")";
			return std::nullopt;
		}
		if (!isKnownTextureFormat(x.dxgiFormat))
		{
			error = "BC1〜7 / BC6H / RGBA8 / RGBA16F 以外の DDS は読まない (DXGI " + std::to_string(x.dxgiFormat) + ")";
			return std::nullopt;
		}
		img.format = static_cast<TextureFormat>(x.dxgiFormat);
		img.hasAlpha = (x.miscFlags2 & 0x7u) == detail::kDdsAlphaStraight;
		img.isCube = (x.miscFlag & detail::kDx10MiscCube) != 0;
		const std::uint32_t count = x.arraySize == 0 ? 1 : x.arraySize;
		img.arraySize = img.isCube ? count * 6 : count;
	}
	else
	{
		if (h.caps2 & detail::kDdsCaps2Volume)
		{
			error = "体積テクスチャの DDS は読まない";
			return std::nullopt;
		}
		if (!detail::legacyFormatOf(h.pixelFormat, img.format, img.hasAlpha, error)) { return std::nullopt; }
		img.isCube = (h.caps2 & detail::kDdsCaps2Cubemap) != 0;
		if (img.isCube && (h.caps2 & detail::kDdsCaps2AllFaces) != detail::kDdsCaps2AllFaces)
		{
			error = "キューブの DDS は 6 面そろった物しか読まない";
			return std::nullopt;
		}
		img.arraySize = img.isCube ? 6 : 1;
	}
	if (h.width == 0 || h.height == 0 || h.width > 16384 || h.height > 16384 || h.mipMapCount > 15 || img.arraySize > 64)
	{
		error = "DDS の寸法か mip 段数か枚数が範囲外";
		return std::nullopt;
	}
	img.width = h.width;
	img.height = h.height;
	const std::uint32_t levels = h.mipMapCount == 0 ? 1 : h.mipMapCount;
	for (std::uint32_t slice = 0; slice < img.arraySize; ++slice)
	{
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
	}
	return img;
}

} // namespace mitiru::render
