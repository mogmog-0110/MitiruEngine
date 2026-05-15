#pragma once

/// @file DdsLoader.hpp
/// @brief DDSテクスチャローダー
/// @details DirectDraw Surface (DDS) 形式のテクスチャを読み込む。
///          DXT1/DXT3/DXT5（BC1/BC2/BC3）圧縮形式と非圧縮RGBAに対応する。
///          GPUアップロード用の生データとフォーマット情報を返す。

#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru::resource
{

/// @brief DDS圧縮フォーマット
enum class DdsFormat : std::uint8_t
{
	Unknown,        ///< 不明なフォーマット
	Rgba8Unorm,     ///< 非圧縮 RGBA 8bit/channel
	Bc1Unorm,       ///< BC1 (DXT1) — 1bit alpha / opaque
	Bc2Unorm,       ///< BC2 (DXT3) — explicit alpha
	Bc3Unorm,       ///< BC3 (DXT5) — interpolated alpha
};

/// @brief DDSのミップレベルデータ
struct DdsMipLevel
{
	int width = 0;                     ///< ミップレベルの幅
	int height = 0;                    ///< ミップレベルの高さ
	std::vector<std::uint8_t> data;    ///< ピクセル/ブロックデータ

	/// @brief 有効なミップレベルか判定する
	[[nodiscard]] bool isValid() const noexcept
	{
		return width > 0 && height > 0 && !data.empty();
	}
};

/// @brief DDSテクスチャデータ
struct DdsTextureData
{
	int width = 0;                              ///< ベースレベルの幅
	int height = 0;                             ///< ベースレベルの高さ
	int mipCount = 0;                           ///< ミップレベル数
	DdsFormat format = DdsFormat::Unknown;       ///< テクスチャフォーマット
	std::vector<DdsMipLevel> mipLevels;         ///< 各ミップレベルのデータ

	/// @brief 有効なテクスチャデータか判定する
	[[nodiscard]] bool isValid() const noexcept
	{
		return width > 0 && height > 0 &&
		       format != DdsFormat::Unknown &&
		       !mipLevels.empty() &&
		       mipLevels[0].isValid();
	}

	/// @brief 圧縮フォーマットか判定する
	[[nodiscard]] bool isCompressed() const noexcept
	{
		return format == DdsFormat::Bc1Unorm ||
		       format == DdsFormat::Bc2Unorm ||
		       format == DdsFormat::Bc3Unorm;
	}
};

/// @brief DDSファイルローダー
/// @details DDS形式のテクスチャファイルを読み込む。
///          ミップマップチェーン全体を読み込み、GPUアップロード可能な形式で返す。
///
/// @code
/// mitiru::resource::DdsImageLoader loader;
/// auto tex = loader.loadDds("textures/diffuse.dds");
/// if (tex) {
///     // tex->format, tex->mipLevels[0].data をGPUにアップロード
/// }
/// @endcode
class DdsImageLoader
{
public:
	/// @brief DDSファイルを読み込む
	/// @param path ファイルパス
	/// @return テクスチャデータ（失敗時はnullopt）
	[[nodiscard]] std::optional<DdsTextureData> loadDds(std::string_view path) const
	{
		std::ifstream file{std::string{path}, std::ios::binary};
		if (!file.is_open())
		{
			return std::nullopt;
		}

		file.seekg(0, std::ios::end);
		const auto fileSize = static_cast<std::size_t>(file.tellg());
		file.seekg(0, std::ios::beg);

		if (fileSize < DDS_MIN_HEADER_SIZE)
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

		return parseDds(fileData);
	}

	/// @brief メモリ上のデータからDDSをパースする
	/// @param data DDSファイルのバイト列
	/// @return テクスチャデータ（失敗時はnullopt）
	[[nodiscard]] static std::optional<DdsTextureData> parseDds(
		const std::vector<std::uint8_t>& data)
	{
		if (data.size() < DDS_MIN_HEADER_SIZE)
		{
			return std::nullopt;
		}

		/// マジックナンバー "DDS " (0x20534444) の検証
		const std::uint32_t magic = readU32(&data[0]);
		if (magic != DDS_MAGIC)
		{
			return std::nullopt;
		}

		/// DDS_HEADER の読み取り
		const std::uint32_t headerSize = readU32(&data[4]);
		if (headerSize != 124)
		{
			return std::nullopt;
		}

		const std::uint32_t flags = readU32(&data[8]);
		const auto texHeight = static_cast<int>(readU32(&data[12]));
		const auto texWidth = static_cast<int>(readU32(&data[16]));
		const std::uint32_t pitchOrLinearSize = readU32(&data[20]);
		const auto mipMapCount = static_cast<int>(readU32(&data[28]));

		if (texWidth <= 0 || texHeight <= 0)
		{
			return std::nullopt;
		}

		/// DDPIXELFORMAT の読み取り（オフセット76）
		const std::uint32_t pfSize = readU32(&data[76]);
		if (pfSize != 32)
		{
			return std::nullopt;
		}

		const std::uint32_t pfFlags = readU32(&data[80]);
		const std::uint32_t fourCC = readU32(&data[84]);
		const std::uint32_t rgbBitCount = readU32(&data[88]);
		const std::uint32_t rMask = readU32(&data[92]);
		const std::uint32_t gMask = readU32(&data[96]);
		const std::uint32_t bMask = readU32(&data[100]);
		const std::uint32_t aMask = readU32(&data[104]);

		/// フォーマットの判定
		const DdsFormat format = detectFormat(pfFlags, fourCC, rgbBitCount, rMask, gMask, bMask, aMask);
		if (format == DdsFormat::Unknown)
		{
			return std::nullopt;
		}

		/// ミップマップ数の決定
		const bool hasMipmaps = (flags & DDSD_MIPMAPCOUNT) != 0;
		const int actualMipCount = (hasMipmaps && mipMapCount > 0) ? mipMapCount : 1;

		/// ピクセルデータの開始位置
		const std::size_t dataOffset = DDS_MIN_HEADER_SIZE;

		/// 各ミップレベルの読み取り
		DdsTextureData result;
		result.width = texWidth;
		result.height = texHeight;
		result.format = format;
		result.mipCount = actualMipCount;
		result.mipLevels.reserve(static_cast<std::size_t>(actualMipCount));

		std::size_t offset = dataOffset;
		int mipW = texWidth;
		int mipH = texHeight;

		for (int mip = 0; mip < actualMipCount; ++mip)
		{
			const std::size_t levelSize = computeLevelSize(mipW, mipH, format);
			if (levelSize == 0)
			{
				return std::nullopt;
			}

			if (offset + levelSize > data.size())
			{
				/// ミップデータが途中で切れている場合、読めた分だけ返す
				if (mip == 0)
				{
					return std::nullopt;
				}
				result.mipCount = mip;
				break;
			}

			DdsMipLevel level;
			level.width = mipW;
			level.height = mipH;
			level.data.assign(data.begin() + static_cast<std::ptrdiff_t>(offset),
			                  data.begin() + static_cast<std::ptrdiff_t>(offset + levelSize));

			result.mipLevels.push_back(std::move(level));

			offset += levelSize;
			mipW = (mipW > 1) ? (mipW / 2) : 1;
			mipH = (mipH > 1) ? (mipH / 2) : 1;
		}

		return result;
	}

	/// @brief DDS拡張子に対応しているか判定する
	/// @param extension 拡張子
	/// @return ".dds"なら true
	[[nodiscard]] bool canLoad(std::string_view extension) const noexcept
	{
		return extension == ".dds" || extension == ".DDS";
	}

private:
	static constexpr std::uint32_t DDS_MAGIC = 0x20534444;          ///< "DDS "
	static constexpr std::size_t DDS_MIN_HEADER_SIZE = 128;         ///< magic(4) + header(124)
	static constexpr std::uint32_t DDSD_MIPMAPCOUNT = 0x00020000;   ///< flags: ミップマップ数有効

	/// DDPIXELFORMAT flags
	static constexpr std::uint32_t DDPF_FOURCC = 0x00000004;        ///< FourCC有効
	static constexpr std::uint32_t DDPF_RGB = 0x00000040;           ///< 非圧縮RGB
	static constexpr std::uint32_t DDPF_ALPHAPIXELS = 0x00000001;   ///< アルファチャンネル有効

	/// FourCC values
	static constexpr std::uint32_t FOURCC_DXT1 = 0x31545844;        ///< "DXT1"
	static constexpr std::uint32_t FOURCC_DXT3 = 0x33545844;        ///< "DXT3"
	static constexpr std::uint32_t FOURCC_DXT5 = 0x35545844;        ///< "DXT5"

	/// @brief バイト列から小エンディアン32bit値を読み取る
	[[nodiscard]] static std::uint32_t readU32(const std::uint8_t* ptr) noexcept
	{
		return static_cast<std::uint32_t>(ptr[0]) |
		       (static_cast<std::uint32_t>(ptr[1]) << 8) |
		       (static_cast<std::uint32_t>(ptr[2]) << 16) |
		       (static_cast<std::uint32_t>(ptr[3]) << 24);
	}

	/// @brief ピクセルフォーマットからDdsFormatを判定する
	[[nodiscard]] static DdsFormat detectFormat(
		std::uint32_t pfFlags,
		std::uint32_t fourCC,
		std::uint32_t rgbBitCount,
		std::uint32_t rMask,
		std::uint32_t gMask,
		std::uint32_t bMask,
		std::uint32_t aMask) noexcept
	{
		if ((pfFlags & DDPF_FOURCC) != 0)
		{
			if (fourCC == FOURCC_DXT1) return DdsFormat::Bc1Unorm;
			if (fourCC == FOURCC_DXT3) return DdsFormat::Bc2Unorm;
			if (fourCC == FOURCC_DXT5) return DdsFormat::Bc3Unorm;
			return DdsFormat::Unknown;
		}

		if ((pfFlags & DDPF_RGB) != 0)
		{
			/// 32bit RGBA（8bit/channel）
			if (rgbBitCount == 32 &&
			    rMask == 0x00FF0000 &&
			    gMask == 0x0000FF00 &&
			    bMask == 0x000000FF &&
			    aMask == 0xFF000000)
			{
				return DdsFormat::Rgba8Unorm;
			}

			/// 32bit RGBA（ABGR配置）
			if (rgbBitCount == 32 &&
			    rMask == 0x000000FF &&
			    gMask == 0x0000FF00 &&
			    bMask == 0x00FF0000 &&
			    aMask == 0xFF000000)
			{
				return DdsFormat::Rgba8Unorm;
			}
		}

		return DdsFormat::Unknown;
	}

	/// @brief 指定ミップレベルのデータサイズを計算する
	/// @param width ミップレベルの幅
	/// @param height ミップレベルの高さ
	/// @param format テクスチャフォーマット
	/// @return データサイズ（バイト）
	[[nodiscard]] static std::size_t computeLevelSize(
		int width, int height, DdsFormat format) noexcept
	{
		switch (format)
		{
		case DdsFormat::Rgba8Unorm:
			return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;

		case DdsFormat::Bc1Unorm:
		{
			/// BC1: 4x4ブロックあたり8バイト
			const auto blockW = static_cast<std::size_t>((width + 3) / 4);
			const auto blockH = static_cast<std::size_t>((height + 3) / 4);
			return blockW * blockH * 8;
		}

		case DdsFormat::Bc2Unorm:
		case DdsFormat::Bc3Unorm:
		{
			/// BC2/BC3: 4x4ブロックあたり16バイト
			const auto blockW = static_cast<std::size_t>((width + 3) / 4);
			const auto blockH = static_cast<std::size_t>((height + 3) / 4);
			return blockW * blockH * 16;
		}

		default:
			return 0;
		}
	}
};

} // namespace mitiru::resource
