#pragma once

/// @file ImageLoader.hpp
/// @brief 画像ローダーインターフェースとBMP/TGA実装、統合ファサード
/// @details 外部依存なしで画像ファイルを読み込む。
///          BMP（24/32bit非圧縮）、TGA（非圧縮）、PNG（stb_image経由）に対応する。
///          UnifiedImageLoader により拡張子/マジックバイトで自動判定する。

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <stb_image.h>

namespace mitiru::resource
{

/// @brief 画像データ
/// @details ロードされた画像のピクセルデータとメタ情報を保持する。
///          ピクセルはRGBA順（1ピクセル4バイト）で格納される。
struct ImageData
{
	int width = 0;                      ///< 画像幅（ピクセル）
	int height = 0;                     ///< 画像高さ（ピクセル）
	int channels = 4;                   ///< チャンネル数（常に4=RGBA）
	std::vector<std::uint8_t> pixels;   ///< ピクセルデータ（RGBA、左上起点）

	/// @brief 有効な画像データか判定する
	/// @return 幅・高さが正でピクセルデータが適切なサイズなら true
	[[nodiscard]] bool isValid() const noexcept
	{
		return width > 0 && height > 0 &&
		       pixels.size() == static_cast<std::size_t>(width * height * channels);
	}
};

/// @brief 画像ローダー抽象インターフェース
/// @details ファイルパスから画像データを読み込む共通インターフェース。
class IImageLoader
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IImageLoader() = default;

	/// @brief 画像を読み込む
	/// @param path ファイルパス
	/// @return 画像データ（読み込み失敗時はnullopt）
	[[nodiscard]] virtual std::optional<ImageData> load(std::string_view path) = 0;

	/// @brief 指定拡張子を読み込めるか判定する
	/// @param extension 拡張子（ドット付き、例: ".bmp"）
	/// @return 対応していれば true
	[[nodiscard]] virtual bool canLoad(std::string_view extension) const = 0;
};

/// @brief BMPファイルローダー
/// @details 非圧縮BMP（BI_RGB）の24bitおよび32bitに対応する。
///          BMPのボトムアップ行順序を正しく処理する。
///
/// @code
/// mitiru::resource::BmpImageLoader loader;
/// if (loader.canLoad(".bmp"))
/// {
///     auto img = loader.load("textures/player.bmp");
///     if (img) { /* img->width, img->height, img->pixels */ }
/// }
/// @endcode
class BmpImageLoader : public IImageLoader
{
public:
	/// @brief BMPファイルを読み込む
	/// @param path ファイルパス
	/// @return 画像データ（RGBA形式、失敗時はnullopt）
	[[nodiscard]] std::optional<ImageData> load(std::string_view path) override
	{
		/// ファイルをバイナリモードで読み込む
		std::ifstream file{std::string{path}, std::ios::binary};
		if (!file.is_open())
		{
			return std::nullopt;
		}

		/// ファイル全体をメモリに読み込む
		file.seekg(0, std::ios::end);
		const auto fileSize = static_cast<std::size_t>(file.tellg());
		file.seekg(0, std::ios::beg);

		if (fileSize < BMP_HEADER_SIZE + DIB_HEADER_MIN_SIZE)
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

		return parseBmp(fileData);
	}

	/// @brief BMP拡張子に対応しているか判定する
	/// @param extension 拡張子
	/// @return ".bmp"なら true
	[[nodiscard]] bool canLoad(std::string_view extension) const override
	{
		return extension == ".bmp" || extension == ".BMP";
	}

private:
	static constexpr std::size_t BMP_HEADER_SIZE = 14;      ///< BMPファイルヘッダーサイズ
	static constexpr std::size_t DIB_HEADER_MIN_SIZE = 40;   ///< DIBヘッダー最小サイズ（BITMAPINFOHEADER）
	static constexpr std::uint32_t BMP_BI_RGB = 0;            ///< 非圧縮フォーマット

	/// @brief バイト列から小エンディアン16bit値を読み取る
	/// @param data データポインタ
	/// @return 16bit値
	[[nodiscard]] static std::uint16_t readU16(const std::uint8_t* data) noexcept
	{
		return static_cast<std::uint16_t>(data[0]) |
		       (static_cast<std::uint16_t>(data[1]) << 8);
	}

	/// @brief バイト列から小エンディアン32bit値を読み取る
	/// @param data データポインタ
	/// @return 32bit値
	[[nodiscard]] static std::uint32_t readU32(const std::uint8_t* data) noexcept
	{
		return static_cast<std::uint32_t>(data[0]) |
		       (static_cast<std::uint32_t>(data[1]) << 8) |
		       (static_cast<std::uint32_t>(data[2]) << 16) |
		       (static_cast<std::uint32_t>(data[3]) << 24);
	}

	/// @brief BMPバイナリデータをパースする
	/// @param data ファイルの全バイト列
	/// @return 画像データ（失敗時はnullopt）
	[[nodiscard]] static std::optional<ImageData> parseBmp(
		const std::vector<std::uint8_t>& data)
	{
		/// BMPシグネチャの検証（'BM'）
		if (data[0] != 'B' || data[1] != 'M')
		{
			return std::nullopt;
		}

		/// ピクセルデータオフセットを読み取る
		const std::uint32_t pixelOffset = readU32(&data[10]);

		/// DIBヘッダーを読み取る
		const std::uint32_t dibSize = readU32(&data[14]);
		if (dibSize < DIB_HEADER_MIN_SIZE)
		{
			return std::nullopt;
		}

		const auto imgWidth = static_cast<std::int32_t>(readU32(&data[18]));
		const auto imgHeight = static_cast<std::int32_t>(readU32(&data[22]));
		const std::uint16_t bitsPerPixel = readU16(&data[28]);
		const std::uint32_t compression = readU32(&data[30]);

		/// 非圧縮のみサポート
		if (compression != BMP_BI_RGB)
		{
			return std::nullopt;
		}

		/// 24bitまたは32bitのみサポート
		if (bitsPerPixel != 24 && bitsPerPixel != 32)
		{
			return std::nullopt;
		}

		if (imgWidth <= 0)
		{
			return std::nullopt;
		}

		/// ボトムアップ（正の高さ）/ トップダウン（負の高さ）を判定する
		const bool bottomUp = (imgHeight > 0);
		const int absHeight = bottomUp ? imgHeight : -imgHeight;

		/// 行あたりのバイト数（4バイト境界にパディング）
		const int bytesPerPixel = bitsPerPixel / 8;
		const int rowSizeRaw = imgWidth * bytesPerPixel;
		const int rowPadding = (4 - (rowSizeRaw % 4)) % 4;
		const int rowStride = rowSizeRaw + rowPadding;

		/// ピクセルデータ領域の検証
		const auto requiredSize = static_cast<std::size_t>(pixelOffset) +
		                          static_cast<std::size_t>(rowStride) *
		                          static_cast<std::size_t>(absHeight);
		if (data.size() < requiredSize)
		{
			return std::nullopt;
		}

		/// RGBA出力バッファを構築する
		ImageData result;
		result.width = imgWidth;
		result.height = absHeight;
		result.channels = 4;
		result.pixels.resize(
			static_cast<std::size_t>(imgWidth) *
			static_cast<std::size_t>(absHeight) * 4);

		for (int y = 0; y < absHeight; ++y)
		{
			/// ボトムアップの場合は行順序を反転する
			const int srcRow = bottomUp ? (absHeight - 1 - y) : y;
			const std::uint8_t* rowPtr =
				&data[pixelOffset + static_cast<std::size_t>(srcRow) *
				      static_cast<std::size_t>(rowStride)];

			const auto dstOffset = static_cast<std::size_t>(y) *
			                       static_cast<std::size_t>(imgWidth) * 4;

			for (int x = 0; x < imgWidth; ++x)
			{
				const auto srcIdx = static_cast<std::size_t>(x * bytesPerPixel);
				const auto dstIdx = dstOffset + static_cast<std::size_t>(x) * 4;

				/// BMPはBGR(A)順→RGBA順に変換する
				result.pixels[dstIdx + 0] = rowPtr[srcIdx + 2]; // R
				result.pixels[dstIdx + 1] = rowPtr[srcIdx + 1]; // G
				result.pixels[dstIdx + 2] = rowPtr[srcIdx + 0]; // B
				result.pixels[dstIdx + 3] = (bitsPerPixel == 32)
					? rowPtr[srcIdx + 3]  // A（32bit BMPから取得）
					: 0xFF;               // A（24bit BMPは不透明）
			}
		}

		return result;
	}
};

/// @brief TGAファイルローダー
/// @details 非圧縮TGA（Type 2: 非圧縮RGB）に対応する。
///          24bitおよび32bitをサポートする。
///
/// @code
/// mitiru::resource::TgaImageLoader loader;
/// auto img = loader.load("textures/sprite.tga");
/// @endcode
class TgaImageLoader : public IImageLoader
{
public:
	/// @brief TGAファイルを読み込む
	/// @param path ファイルパス
	/// @return 画像データ（RGBA形式、失敗時はnullopt）
	[[nodiscard]] std::optional<ImageData> load(std::string_view path) override
	{
		/// ファイルをバイナリモードで読み込む
		std::ifstream file{std::string{path}, std::ios::binary};
		if (!file.is_open())
		{
			return std::nullopt;
		}

		file.seekg(0, std::ios::end);
		const auto fileSize = static_cast<std::size_t>(file.tellg());
		file.seekg(0, std::ios::beg);

		if (fileSize < TGA_HEADER_SIZE)
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

		return parseTga(fileData);
	}

	/// @brief TGA拡張子に対応しているか判定する
	/// @param extension 拡張子
	/// @return ".tga"なら true
	[[nodiscard]] bool canLoad(std::string_view extension) const override
	{
		return extension == ".tga" || extension == ".TGA";
	}

private:
	static constexpr std::size_t TGA_HEADER_SIZE = 18;  ///< TGAヘッダーサイズ

	/// @brief TGAバイナリデータをパースする
	/// @param data ファイルの全バイト列
	/// @return 画像データ（失敗時はnullopt）
	[[nodiscard]] static std::optional<ImageData> parseTga(
		const std::vector<std::uint8_t>& data)
	{
		/// TGAヘッダーを読み取る
		const std::uint8_t idLength = data[0];
		const std::uint8_t imageType = data[2];

		/// 非圧縮RGB（Type 2）のみサポート
		if (imageType != 2)
		{
			return std::nullopt;
		}

		const auto imgWidth = static_cast<int>(
			data[12] | (static_cast<std::uint16_t>(data[13]) << 8));
		const auto imgHeight = static_cast<int>(
			data[14] | (static_cast<std::uint16_t>(data[15]) << 8));
		const std::uint8_t bitsPerPixel = data[16];
		const std::uint8_t descriptor = data[17];

		/// 24bitまたは32bitのみサポート
		if (bitsPerPixel != 24 && bitsPerPixel != 32)
		{
			return std::nullopt;
		}

		if (imgWidth <= 0 || imgHeight <= 0)
		{
			return std::nullopt;
		}

		/// ピクセルデータの開始位置
		const auto pixelOffset = TGA_HEADER_SIZE + static_cast<std::size_t>(idLength);
		const int bytesPerPixel = bitsPerPixel / 8;

		const auto pixelDataSize = static_cast<std::size_t>(imgWidth) *
		                           static_cast<std::size_t>(imgHeight) *
		                           static_cast<std::size_t>(bytesPerPixel);

		if (data.size() < pixelOffset + pixelDataSize)
		{
			return std::nullopt;
		}

		/// TGAの原点方向を判定する（bit5: 0=下、1=上）
		const bool topOrigin = (descriptor & 0x20) != 0;

		/// RGBA出力バッファを構築する
		ImageData result;
		result.width = imgWidth;
		result.height = imgHeight;
		result.channels = 4;
		result.pixels.resize(
			static_cast<std::size_t>(imgWidth) *
			static_cast<std::size_t>(imgHeight) * 4);

		for (int y = 0; y < imgHeight; ++y)
		{
			/// TGAのデフォルトはボトムアップ（topOriginでない場合）
			const int srcRow = topOrigin ? y : (imgHeight - 1 - y);
			const auto srcOffset = pixelOffset +
			                       static_cast<std::size_t>(srcRow) *
			                       static_cast<std::size_t>(imgWidth) *
			                       static_cast<std::size_t>(bytesPerPixel);

			const auto dstOffset = static_cast<std::size_t>(y) *
			                       static_cast<std::size_t>(imgWidth) * 4;

			for (int x = 0; x < imgWidth; ++x)
			{
				const auto srcIdx = srcOffset +
				                    static_cast<std::size_t>(x) *
				                    static_cast<std::size_t>(bytesPerPixel);
				const auto dstIdx = dstOffset + static_cast<std::size_t>(x) * 4;

				/// TGAはBGR(A)順→RGBA順に変換する
				result.pixels[dstIdx + 0] = data[srcIdx + 2]; // R
				result.pixels[dstIdx + 1] = data[srcIdx + 1]; // G
				result.pixels[dstIdx + 2] = data[srcIdx + 0]; // B
				result.pixels[dstIdx + 3] = (bitsPerPixel == 32)
					? data[srcIdx + 3]  // A
					: 0xFF;             // 不透明
			}
		}

		return result;
	}
};

/// @brief 画像フォーマット種別
enum class ImageFormat : std::uint8_t
{
	Unknown,   ///< 不明なフォーマット
	Bmp,       ///< BMP
	Tga,       ///< TGA
	Png,       ///< PNG
	Jpeg,      ///< JPEG
	Dds,       ///< DDS (DirectDraw Surface)
	Ktx,       ///< KTX/KTX2 (Khronos Texture)
};

/// @brief 統合画像ローダーファサード
/// @details 拡張子またはマジックバイトでフォーマットを自動判定し、
///          適切なローダーに委譲する。BMP/TGA は内蔵ローダー、
///          PNG/JPEG は PngImageLoader (stb_image)、DDS/KTX は専用ローダーを使用する。
///
/// @code
/// mitiru::resource::UnifiedImageLoader loader;
///
/// // 拡張子で自動判定
/// auto img = loader.load("textures/player.png");
///
/// // メモリからマジックバイトで自動判定
/// auto img2 = loader.loadFromMemory(data.data(), data.size());
///
/// // フォーマット検出のみ
/// auto fmt = UnifiedImageLoader::detectFormatByPath("textures/terrain.dds");
/// @endcode
class UnifiedImageLoader : public IImageLoader
{
public:
	/// @brief ファイルパスから画像を読み込む（フォーマット自動判定）
	/// @param path ファイルパス
	/// @return 画像データ（RGBA形式、失敗時はnullopt）
	[[nodiscard]] std::optional<ImageData> load(std::string_view path) override
	{
		const auto ext = extractExtension(path);
		const auto format = detectFormatByExtension(ext);

		switch (format)
		{
		case ImageFormat::Bmp:
			return m_bmpLoader.load(path);

		case ImageFormat::Tga:
			return m_tgaLoader.load(path);

		case ImageFormat::Png:
		case ImageFormat::Jpeg:
			return loadWithStb(path);

		default:
			/// 拡張子で判定できない場合、マジックバイトで判定を試みる
			return loadWithMagicDetection(path);
		}
	}

	/// @brief 指定拡張子を読み込めるか判定する
	/// @param extension 拡張子（ドット付き）
	/// @return 対応していれば true
	[[nodiscard]] bool canLoad(std::string_view extension) const override
	{
		const auto format = detectFormatByExtension(extension);
		return format != ImageFormat::Unknown;
	}

	/// @brief メモリ上のデータから画像を読み込む（マジックバイト自動判定）
	/// @param data データポインタ
	/// @param size データサイズ（バイト）
	/// @return 画像データ（RGBA形式、失敗時はnullopt）
	[[nodiscard]] std::optional<ImageData> loadFromMemory(
		const std::uint8_t* data, std::size_t size) const
	{
		if (!data || size < 4)
		{
			return std::nullopt;
		}

		const auto format = detectFormatByMagic(data, size);

		switch (format)
		{
		case ImageFormat::Png:
		case ImageFormat::Jpeg:
			return loadFromMemoryWithStb(data, size);

		default:
			/// BMP/TGA等のメモリローダーは拡張子ベースのため、
			/// stb_image にフォールバックする
			return loadFromMemoryWithStb(data, size);
		}
	}

	/// @brief ファイルパスの拡張子からフォーマットを判定する
	/// @param path ファイルパス
	/// @return 判定されたフォーマット
	[[nodiscard]] static ImageFormat detectFormatByPath(std::string_view path) noexcept
	{
		return detectFormatByExtension(extractExtension(path));
	}

	/// @brief 拡張子からフォーマットを判定する
	/// @param ext 拡張子（ドット付き、例: ".png"）
	/// @return 判定されたフォーマット
	[[nodiscard]] static ImageFormat detectFormatByExtension(std::string_view ext) noexcept
	{
		if (ext.empty())
		{
			return ImageFormat::Unknown;
		}

		/// 小文字に変換して比較する（簡易実装：ASCII範囲のみ）
		std::array<char, 8> lower{};
		const auto len = (ext.size() < lower.size()) ? ext.size() : lower.size() - 1;
		for (std::size_t i = 0; i < len; ++i)
		{
			const char c = ext[i];
			lower[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
		}
		const std::string_view lowerExt{lower.data(), len};

		if (lowerExt == ".bmp") return ImageFormat::Bmp;
		if (lowerExt == ".tga") return ImageFormat::Tga;
		if (lowerExt == ".png") return ImageFormat::Png;
		if (lowerExt == ".jpg" || lowerExt == ".jpeg") return ImageFormat::Jpeg;
		if (lowerExt == ".dds") return ImageFormat::Dds;
		if (lowerExt == ".ktx" || lowerExt == ".ktx2") return ImageFormat::Ktx;

		return ImageFormat::Unknown;
	}

	/// @brief マジックバイトからフォーマットを判定する
	/// @param data データポインタ
	/// @param size データサイズ
	/// @return 判定されたフォーマット
	[[nodiscard]] static ImageFormat detectFormatByMagic(
		const std::uint8_t* data, std::size_t size) noexcept
	{
		if (!data || size < 4)
		{
			return ImageFormat::Unknown;
		}

		/// PNG: 0x89 'P' 'N' 'G'
		if (size >= 8 &&
		    data[0] == 0x89 && data[1] == 0x50 &&
		    data[2] == 0x4E && data[3] == 0x47)
		{
			return ImageFormat::Png;
		}

		/// JPEG: 0xFF 0xD8 0xFF
		if (size >= 3 &&
		    data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
		{
			return ImageFormat::Jpeg;
		}

		/// BMP: 'B' 'M'
		if (size >= 2 && data[0] == 'B' && data[1] == 'M')
		{
			return ImageFormat::Bmp;
		}

		/// DDS: 'D' 'D' 'S' ' '
		if (size >= 4 &&
		    data[0] == 'D' && data[1] == 'D' &&
		    data[2] == 'S' && data[3] == ' ')
		{
			return ImageFormat::Dds;
		}

		/// KTX1/KTX2: 0xAB 'K' 'T' 'X'
		if (size >= 4 &&
		    data[0] == 0xAB && data[1] == 0x4B &&
		    data[2] == 0x54 && data[3] == 0x58)
		{
			return ImageFormat::Ktx;
		}

		return ImageFormat::Unknown;
	}

private:
	BmpImageLoader m_bmpLoader;   ///< BMPローダー
	TgaImageLoader m_tgaLoader;   ///< TGAローダー

	/// @brief ファイルパスから拡張子を抽出する
	/// @param path ファイルパス
	/// @return 拡張子（ドット付き、見つからない場合は空）
	[[nodiscard]] static std::string_view extractExtension(std::string_view path) noexcept
	{
		const auto dotPos = path.rfind('.');
		if (dotPos == std::string_view::npos)
		{
			return {};
		}

		/// パス区切り文字の後にドットがあることを確認する
		const auto lastSlash = path.find_last_of("/\\");
		if (lastSlash != std::string_view::npos && dotPos < lastSlash)
		{
			return {};
		}

		return path.substr(dotPos);
	}

	/// @brief stb_image でファイルから画像を読み込む（PNG/JPEG対応）
	/// @param path ファイルパス
	/// @return 画像データ（失敗時はnullopt）
	[[nodiscard]] static std::optional<ImageData> loadWithStb(std::string_view path)
	{
		std::ifstream file{std::string{path}, std::ios::binary};
		if (!file.is_open())
		{
			return std::nullopt;
		}

		file.seekg(0, std::ios::end);
		const auto fileSize = static_cast<std::size_t>(file.tellg());
		file.seekg(0, std::ios::beg);

		if (fileSize == 0)
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

		return loadFromMemoryWithStb(fileData.data(), fileData.size());
	}

	/// @brief stb_image でメモリから画像を読み込む
	/// @param data データポインタ
	/// @param size データサイズ
	/// @return 画像データ（失敗時はnullopt）
	[[nodiscard]] static std::optional<ImageData> loadFromMemoryWithStb(
		const std::uint8_t* data, std::size_t size)
	{
		int w = 0;
		int h = 0;
		int channels = 0;
		auto* pixels = stbi_load_from_memory(
			data, static_cast<int>(size), &w, &h, &channels, 4);

		if (!pixels)
		{
			return std::nullopt;
		}

		ImageData result;
		result.width = w;
		result.height = h;
		result.channels = 4;
		result.pixels.assign(
			pixels,
			pixels + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);

		stbi_image_free(pixels);
		return result;
	}

	/// @brief マジックバイトで判定してファイルを読み込む（フォールバック）
	/// @param path ファイルパス
	/// @return 画像データ（失敗時はnullopt）
	[[nodiscard]] std::optional<ImageData> loadWithMagicDetection(std::string_view path)
	{
		std::ifstream file{std::string{path}, std::ios::binary};
		if (!file.is_open())
		{
			return std::nullopt;
		}

		file.seekg(0, std::ios::end);
		const auto fileSize = static_cast<std::size_t>(file.tellg());
		file.seekg(0, std::ios::beg);

		if (fileSize < 4)
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

		const auto format = detectFormatByMagic(fileData.data(), fileData.size());

		switch (format)
		{
		case ImageFormat::Bmp:
			return m_bmpLoader.load(path);

		case ImageFormat::Tga:
			return m_tgaLoader.load(path);

		case ImageFormat::Png:
		case ImageFormat::Jpeg:
			return loadFromMemoryWithStb(fileData.data(), fileData.size());

		default:
			/// 最終フォールバック: stb_image に任せる
			return loadFromMemoryWithStb(fileData.data(), fileData.size());
		}
	}
};

} // namespace mitiru::resource
