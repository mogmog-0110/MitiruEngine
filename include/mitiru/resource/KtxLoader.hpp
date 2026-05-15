#pragma once

/// @file KtxLoader.hpp
/// @brief KTX/KTX2テクスチャコンテナローダー
/// @details KTX (Khronos Texture) 形式のテクスチャを読み込む。
///          KTX1とKTX2の両フォーマットに対応する。
///          ETC2、ASTC、BCフォーマットの検出と生ミップレベルデータを返す。

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru::resource
{

/// @brief KTXテクスチャのGPUフォーマット
enum class KtxFormat : std::uint32_t
{
	Unknown = 0,

	/// BC (S3TC/BPTC) formats — Desktop GPU
	Bc1Rgba = 0x8C4D,             ///< GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
	Bc2Rgba = 0x8C4E,             ///< GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
	Bc3Rgba = 0x8C4F,             ///< GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
	Bc7Rgba = 0x8E8C,             ///< GL_COMPRESSED_RGBA_BPTC_UNORM

	/// ETC2 formats — Mobile GPU (OpenGL ES 3.0+)
	Etc2Rgb8 = 0x9274,            ///< GL_COMPRESSED_RGB8_ETC2
	Etc2Rgba8 = 0x9278,           ///< GL_COMPRESSED_RGBA8_ETC2_EAC
	Etc2Rgb8A1 = 0x9276,          ///< GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2

	/// ASTC formats — Mobile GPU (OpenGL ES 3.1+ / Vulkan)
	Astc4x4 = 0x93B0,             ///< GL_COMPRESSED_RGBA_ASTC_4x4_KHR
	Astc5x5 = 0x93B2,             ///< GL_COMPRESSED_RGBA_ASTC_5x5_KHR
	Astc6x6 = 0x93B4,             ///< GL_COMPRESSED_RGBA_ASTC_6x6_KHR
	Astc8x8 = 0x93B7,             ///< GL_COMPRESSED_RGBA_ASTC_8x8_KHR

	/// Uncompressed
	Rgba8 = 0x8058,                ///< GL_RGBA8
	Rgb8 = 0x8051,                 ///< GL_RGB8
};

/// @brief KTX2 の VkFormat 値（主要なもの）
enum class KtxVkFormat : std::uint32_t
{
	Undefined = 0,
	Bc1RgbaUnorm = 131,            ///< VK_FORMAT_BC1_RGBA_UNORM_BLOCK
	Bc2UnormBlock = 135,           ///< VK_FORMAT_BC2_UNORM_BLOCK
	Bc3UnormBlock = 137,           ///< VK_FORMAT_BC3_UNORM_BLOCK
	Bc7UnormBlock = 145,           ///< VK_FORMAT_BC7_UNORM_BLOCK
	Etc2Rgb8Unorm = 147,          ///< VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK
	Etc2Rgba8Unorm = 151,         ///< VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK
	Astc4x4Unorm = 157,           ///< VK_FORMAT_ASTC_4x4_UNORM_BLOCK
	Astc5x5Unorm = 159,           ///< VK_FORMAT_ASTC_5x5_UNORM_BLOCK
	Astc6x6Unorm = 161,           ///< VK_FORMAT_ASTC_6x6_UNORM_BLOCK
	Astc8x8Unorm = 165,           ///< VK_FORMAT_ASTC_8x8_UNORM_BLOCK
	R8G8B8A8Unorm = 37,           ///< VK_FORMAT_R8G8B8A8_UNORM
	R8G8B8Unorm = 23,             ///< VK_FORMAT_R8G8B8_UNORM
};

/// @brief KTXミップレベルデータ
struct KtxMipLevel
{
	int width = 0;                     ///< ミップレベルの幅
	int height = 0;                    ///< ミップレベルの高さ
	std::vector<std::uint8_t> data;    ///< 生データ（圧縮/非圧縮）

	/// @brief 有効なミップレベルか判定する
	[[nodiscard]] bool isValid() const noexcept
	{
		return width > 0 && height > 0 && !data.empty();
	}
};

/// @brief KTXテクスチャデータ
struct KtxTextureData
{
	int width = 0;                              ///< ベースレベルの幅
	int height = 0;                             ///< ベースレベルの高さ
	int mipCount = 0;                           ///< ミップレベル数
	KtxFormat glFormat = KtxFormat::Unknown;     ///< OpenGL内部フォーマット
	KtxVkFormat vkFormat = KtxVkFormat::Undefined; ///< Vulkanフォーマット（KTX2のみ）
	bool isKtx2 = false;                        ///< KTX2形式か
	std::vector<KtxMipLevel> mipLevels;         ///< 各ミップレベルのデータ

	/// @brief 有効なテクスチャデータか判定する
	[[nodiscard]] bool isValid() const noexcept
	{
		return width > 0 && height > 0 &&
		       (glFormat != KtxFormat::Unknown || vkFormat != KtxVkFormat::Undefined) &&
		       !mipLevels.empty() &&
		       mipLevels[0].isValid();
	}

	/// @brief 圧縮フォーマットか判定する
	[[nodiscard]] bool isCompressed() const noexcept
	{
		switch (glFormat)
		{
		case KtxFormat::Rgba8:
		case KtxFormat::Rgb8:
		case KtxFormat::Unknown:
			return false;
		default:
			return true;
		}
	}

	/// @brief ASTCフォーマットか判定する
	[[nodiscard]] bool isAstc() const noexcept
	{
		return glFormat == KtxFormat::Astc4x4 ||
		       glFormat == KtxFormat::Astc5x5 ||
		       glFormat == KtxFormat::Astc6x6 ||
		       glFormat == KtxFormat::Astc8x8;
	}

	/// @brief ETC2フォーマットか判定する
	[[nodiscard]] bool isEtc2() const noexcept
	{
		return glFormat == KtxFormat::Etc2Rgb8 ||
		       glFormat == KtxFormat::Etc2Rgba8 ||
		       glFormat == KtxFormat::Etc2Rgb8A1;
	}

	/// @brief BCフォーマットか判定する
	[[nodiscard]] bool isBc() const noexcept
	{
		return glFormat == KtxFormat::Bc1Rgba ||
		       glFormat == KtxFormat::Bc2Rgba ||
		       glFormat == KtxFormat::Bc3Rgba ||
		       glFormat == KtxFormat::Bc7Rgba;
	}
};

/// @brief KTX/KTX2ファイルローダー
/// @details Khronos Texture形式のファイルを読み込む。
///          KTX1（OpenGL向け）とKTX2（Vulkan向け）の両方に対応する。
///
/// @code
/// mitiru::resource::KtxImageLoader loader;
/// auto tex = loader.loadKtx("textures/compressed.ktx");
/// if (tex) {
///     if (tex->isAstc()) { /* ASTC用パス */ }
///     for (const auto& mip : tex->mipLevels) { /* GPUアップロード */ }
/// }
/// @endcode
class KtxImageLoader
{
public:
	/// @brief KTX/KTX2ファイルを読み込む
	/// @param path ファイルパス
	/// @return テクスチャデータ（失敗時はnullopt）
	[[nodiscard]] std::optional<KtxTextureData> loadKtx(std::string_view path) const
	{
		std::ifstream file{std::string{path}, std::ios::binary};
		if (!file.is_open())
		{
			return std::nullopt;
		}

		file.seekg(0, std::ios::end);
		const auto fileSize = static_cast<std::size_t>(file.tellg());
		file.seekg(0, std::ios::beg);

		if (fileSize < 12)
		{
			return std::nullopt;
		}

		std::vector<std::uint8_t> fileData(fileSize);
		file.read(reinterpret_cast<char*>(fileData.data()),
		          static_cast<std::streamsize>(fileSize));

		if (!file)
		{
			return std::nullopt;
		}

		/// KTX1 vs KTX2 をマジックバイトで判定する
		if (isKtx1Magic(fileData))
		{
			return parseKtx1(fileData);
		}

		if (isKtx2Magic(fileData))
		{
			return parseKtx2(fileData);
		}

		return std::nullopt;
	}

	/// @brief KTX拡張子に対応しているか判定する
	/// @param extension 拡張子
	/// @return ".ktx" or ".ktx2" なら true
	[[nodiscard]] bool canLoad(std::string_view extension) const noexcept
	{
		return extension == ".ktx" || extension == ".KTX" ||
		       extension == ".ktx2" || extension == ".KTX2";
	}

private:
	/// KTX1 マジックバイト: {0xAB, 'K', 'T', 'X', ' ', '1', '1', 0xBB, '\r', '\n', 0x1A, '\n'}
	static constexpr std::array<std::uint8_t, 12> KTX1_MAGIC = {
		0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
	};

	/// KTX2 マジックバイト: {0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, '\r', '\n', 0x1A, '\n'}
	static constexpr std::array<std::uint8_t, 12> KTX2_MAGIC = {
		0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
	};

	/// @brief KTX1マジックバイトか判定する
	[[nodiscard]] static bool isKtx1Magic(const std::vector<std::uint8_t>& data) noexcept
	{
		if (data.size() < KTX1_MAGIC.size()) return false;
		return std::memcmp(data.data(), KTX1_MAGIC.data(), KTX1_MAGIC.size()) == 0;
	}

	/// @brief KTX2マジックバイトか判定する
	[[nodiscard]] static bool isKtx2Magic(const std::vector<std::uint8_t>& data) noexcept
	{
		if (data.size() < KTX2_MAGIC.size()) return false;
		return std::memcmp(data.data(), KTX2_MAGIC.data(), KTX2_MAGIC.size()) == 0;
	}

	/// @brief 小エンディアン32bit値を読み取る
	[[nodiscard]] static std::uint32_t readU32LE(const std::uint8_t* ptr) noexcept
	{
		return static_cast<std::uint32_t>(ptr[0]) |
		       (static_cast<std::uint32_t>(ptr[1]) << 8) |
		       (static_cast<std::uint32_t>(ptr[2]) << 16) |
		       (static_cast<std::uint32_t>(ptr[3]) << 24);
	}

	/// @brief 大エンディアン32bit値を読み取る
	[[nodiscard]] static std::uint32_t readU32BE(const std::uint8_t* ptr) noexcept
	{
		return (static_cast<std::uint32_t>(ptr[0]) << 24) |
		       (static_cast<std::uint32_t>(ptr[1]) << 16) |
		       (static_cast<std::uint32_t>(ptr[2]) << 8) |
		       static_cast<std::uint32_t>(ptr[3]);
	}

	/// @brief エンディアンに応じて32bit値を読み取る
	[[nodiscard]] static std::uint32_t readU32(const std::uint8_t* ptr, bool bigEndian) noexcept
	{
		return bigEndian ? readU32BE(ptr) : readU32LE(ptr);
	}

	/// @brief KTX1形式をパースする
	/// @param data ファイルの全バイト列
	/// @return テクスチャデータ（失敗時はnullopt）
	[[nodiscard]] static std::optional<KtxTextureData> parseKtx1(
		const std::vector<std::uint8_t>& data)
	{
		static constexpr std::size_t KTX1_HEADER_SIZE = 64;
		if (data.size() < KTX1_HEADER_SIZE)
		{
			return std::nullopt;
		}

		/// エンディアン判定（offset 12）
		const std::uint32_t endianness = readU32LE(&data[12]);
		const bool bigEndian = (endianness == 0x01020304);
		if (endianness != 0x04030201 && endianness != 0x01020304)
		{
			return std::nullopt;
		}

		/// ヘッダーフィールドの読み取り
		const auto glInternalFormat = static_cast<KtxFormat>(readU32(&data[28], bigEndian));
		const auto pixelWidth = static_cast<int>(readU32(&data[36], bigEndian));
		const auto pixelHeight = static_cast<int>(readU32(&data[40], bigEndian));
		const auto pixelDepth = static_cast<int>(readU32(&data[44], bigEndian));
		const auto numberOfMipmapLevels = static_cast<int>(readU32(&data[56], bigEndian));
		const auto bytesOfKeyValueData = readU32(&data[60], bigEndian);

		if (pixelWidth <= 0 || pixelHeight <= 0)
		{
			return std::nullopt;
		}

		/// 3Dテクスチャは非対応
		if (pixelDepth > 1)
		{
			return std::nullopt;
		}

		const int mipCount = (numberOfMipmapLevels > 0) ? numberOfMipmapLevels : 1;

		/// ミップデータの開始位置
		std::size_t offset = KTX1_HEADER_SIZE + static_cast<std::size_t>(bytesOfKeyValueData);

		KtxTextureData result;
		result.width = pixelWidth;
		result.height = pixelHeight;
		result.mipCount = mipCount;
		result.glFormat = glInternalFormat;
		result.isKtx2 = false;
		result.mipLevels.reserve(static_cast<std::size_t>(mipCount));

		int mipW = pixelWidth;
		int mipH = pixelHeight;

		for (int mip = 0; mip < mipCount; ++mip)
		{
			/// 各ミップレベルは imageSize (uint32) + データ + パディング
			if (offset + 4 > data.size())
			{
				if (mip == 0) return std::nullopt;
				result.mipCount = mip;
				break;
			}

			const auto imageSize = static_cast<std::size_t>(readU32(&data[offset], bigEndian));
			offset += 4;

			if (offset + imageSize > data.size())
			{
				if (mip == 0) return std::nullopt;
				result.mipCount = mip;
				break;
			}

			KtxMipLevel level;
			level.width = mipW;
			level.height = mipH;
			level.data.assign(
				data.begin() + static_cast<std::ptrdiff_t>(offset),
				data.begin() + static_cast<std::ptrdiff_t>(offset + imageSize));

			result.mipLevels.push_back(std::move(level));

			/// 4バイト境界にパディングする
			offset += imageSize;
			const auto padding = (4 - (imageSize % 4)) % 4;
			offset += padding;

			mipW = (mipW > 1) ? (mipW / 2) : 1;
			mipH = (mipH > 1) ? (mipH / 2) : 1;
		}

		return result;
	}

	/// @brief KTX2形式をパースする
	/// @param data ファイルの全バイト列
	/// @return テクスチャデータ（失敗時はnullopt）
	[[nodiscard]] static std::optional<KtxTextureData> parseKtx2(
		const std::vector<std::uint8_t>& data)
	{
		static constexpr std::size_t KTX2_HEADER_SIZE = 80;
		if (data.size() < KTX2_HEADER_SIZE)
		{
			return std::nullopt;
		}

		/// KTX2ヘッダーは常にリトルエンディアン
		const auto vkFormat = static_cast<KtxVkFormat>(readU32LE(&data[12]));
		const auto pixelWidth = static_cast<int>(readU32LE(&data[20]));
		const auto pixelHeight = static_cast<int>(readU32LE(&data[24]));
		const auto pixelDepth = static_cast<int>(readU32LE(&data[28]));
		const auto levelCount = static_cast<int>(readU32LE(&data[36]));
		const auto supercompressionScheme = readU32LE(&data[40]);

		if (pixelWidth <= 0 || pixelHeight <= 0)
		{
			return std::nullopt;
		}

		if (pixelDepth > 1)
		{
			return std::nullopt;
		}

		/// 超圧縮（BasisLZ等）は非対応
		if (supercompressionScheme != 0)
		{
			return std::nullopt;
		}

		const int mipCount = (levelCount > 0) ? levelCount : 1;

		/// Level Index の読み取り（offset 80 から levelCount * 24 バイト）
		/// 各レベル: byteOffset(8) + byteLength(8) + uncompressedByteLength(8)
		const std::size_t levelIndexOffset = KTX2_HEADER_SIZE;
		const std::size_t levelIndexSize = static_cast<std::size_t>(mipCount) * 24;

		if (data.size() < levelIndexOffset + levelIndexSize)
		{
			return std::nullopt;
		}

		/// VkFormat → GL フォーマットへの変換
		const KtxFormat glFormat = vkFormatToGlFormat(vkFormat);

		KtxTextureData result;
		result.width = pixelWidth;
		result.height = pixelHeight;
		result.mipCount = mipCount;
		result.glFormat = glFormat;
		result.vkFormat = vkFormat;
		result.isKtx2 = true;
		result.mipLevels.reserve(static_cast<std::size_t>(mipCount));

		int mipW = pixelWidth;
		int mipH = pixelHeight;

		for (int mip = 0; mip < mipCount; ++mip)
		{
			const std::size_t entryOffset = levelIndexOffset + static_cast<std::size_t>(mip) * 24;

			/// byteOffset (uint64, lower 32 bits)
			const auto levelByteOffset = static_cast<std::size_t>(readU32LE(&data[entryOffset]));
			/// byteLength (uint64, lower 32 bits)
			const auto levelByteLength = static_cast<std::size_t>(readU32LE(&data[entryOffset + 8]));

			if (levelByteOffset + levelByteLength > data.size())
			{
				if (mip == 0) return std::nullopt;
				result.mipCount = mip;
				break;
			}

			KtxMipLevel level;
			level.width = mipW;
			level.height = mipH;
			level.data.assign(
				data.begin() + static_cast<std::ptrdiff_t>(levelByteOffset),
				data.begin() + static_cast<std::ptrdiff_t>(levelByteOffset + levelByteLength));

			result.mipLevels.push_back(std::move(level));

			mipW = (mipW > 1) ? (mipW / 2) : 1;
			mipH = (mipH > 1) ? (mipH / 2) : 1;
		}

		return result;
	}

	/// @brief VkFormat を GL 内部フォーマットに変換する
	[[nodiscard]] static KtxFormat vkFormatToGlFormat(KtxVkFormat vk) noexcept
	{
		switch (vk)
		{
		case KtxVkFormat::Bc1RgbaUnorm:   return KtxFormat::Bc1Rgba;
		case KtxVkFormat::Bc2UnormBlock:   return KtxFormat::Bc2Rgba;
		case KtxVkFormat::Bc3UnormBlock:   return KtxFormat::Bc3Rgba;
		case KtxVkFormat::Bc7UnormBlock:   return KtxFormat::Bc7Rgba;
		case KtxVkFormat::Etc2Rgb8Unorm:   return KtxFormat::Etc2Rgb8;
		case KtxVkFormat::Etc2Rgba8Unorm:  return KtxFormat::Etc2Rgba8;
		case KtxVkFormat::Astc4x4Unorm:    return KtxFormat::Astc4x4;
		case KtxVkFormat::Astc5x5Unorm:    return KtxFormat::Astc5x5;
		case KtxVkFormat::Astc6x6Unorm:    return KtxFormat::Astc6x6;
		case KtxVkFormat::Astc8x8Unorm:    return KtxFormat::Astc8x8;
		case KtxVkFormat::R8G8B8A8Unorm:   return KtxFormat::Rgba8;
		case KtxVkFormat::R8G8B8Unorm:     return KtxFormat::Rgb8;
		default:                            return KtxFormat::Unknown;
		}
	}
};

} // namespace mitiru::resource
