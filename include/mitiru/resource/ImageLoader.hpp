#pragma once

/// @file ImageLoader.hpp
/// @brief 画像ローダーインターフェースとBMP/TGA実装
/// @details 外部依存なしで画像ファイルを読み込む。
///          BMP（24/32bit非圧縮）とTGA（非圧縮）に対応する。

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
		std::ifstream file(std::string(path), std::ios::binary);
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
	static constexpr std::uint32_t BI_RGB = 0;               ///< 非圧縮フォーマット

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
		if (compression != BI_RGB)
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
		std::ifstream file(std::string(path), std::ios::binary);
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

} // namespace mitiru::resource
