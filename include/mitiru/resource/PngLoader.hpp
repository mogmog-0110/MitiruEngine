#pragma once

/// @file PngLoader.hpp
/// @brief stb_imageベースのPNG画像ローダー
/// @details stb_image.hを使用してPNG画像をデコードする。
///          RAII管理されたC++ラッパーを提供する。

#include <mitiru/resource/ImageLoader.hpp>

#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <stb_image.h>

namespace mitiru::resource
{

/// @brief stb_image デコード結果の RAII ラッパー
/// @details stbi_load 系で確保されたピクセルメモリを自動解放する。
///          ムーブのみ可能。コピー不可。
class StbImageHandle
{
public:
	/// @brief デフォルトコンストラクタ（空ハンドル）
	StbImageHandle() noexcept = default;

	/// @brief デコード結果から構築する
	/// @param pixels stbi_load が返したピクセルデータ（所有権を取得）
	/// @param width 画像幅
	/// @param height 画像高さ
	/// @param channels チャンネル数
	StbImageHandle(std::uint8_t* pixels, int width, int height, int channels) noexcept
		: m_pixels(pixels)
		, m_width(width)
		, m_height(height)
		, m_channels(channels)
	{
	}

	/// @brief デストラクタ（stb メモリを解放する）
	~StbImageHandle()
	{
		if (m_pixels)
		{
			stbi_image_free(m_pixels);
		}
	}

	/// @brief コピー禁止
	StbImageHandle(const StbImageHandle&) = delete;
	/// @brief コピー代入禁止
	StbImageHandle& operator=(const StbImageHandle&) = delete;

	/// @brief ムーブコンストラクタ
	StbImageHandle(StbImageHandle&& other) noexcept
		: m_pixels(other.m_pixels)
		, m_width(other.m_width)
		, m_height(other.m_height)
		, m_channels(other.m_channels)
	{
		other.m_pixels = nullptr;
		other.m_width = 0;
		other.m_height = 0;
		other.m_channels = 0;
	}

	/// @brief ムーブ代入演算子
	StbImageHandle& operator=(StbImageHandle&& other) noexcept
	{
		if (this != &other)
		{
			if (m_pixels)
			{
				stbi_image_free(m_pixels);
			}
			m_pixels = other.m_pixels;
			m_width = other.m_width;
			m_height = other.m_height;
			m_channels = other.m_channels;
			other.m_pixels = nullptr;
			other.m_width = 0;
			other.m_height = 0;
			other.m_channels = 0;
		}
		return *this;
	}

	/// @brief 有効なデコード結果を保持しているか判定する
	[[nodiscard]] bool isValid() const noexcept { return m_pixels != nullptr; }

	/// @brief ピクセルデータへのポインタを取得する
	[[nodiscard]] const std::uint8_t* pixels() const noexcept { return m_pixels; }

	/// @brief 画像幅を取得する
	[[nodiscard]] int width() const noexcept { return m_width; }

	/// @brief 画像高さを取得する
	[[nodiscard]] int height() const noexcept { return m_height; }

	/// @brief チャンネル数を取得する
	[[nodiscard]] int channels() const noexcept { return m_channels; }

	/// @brief ピクセルデータの総バイト数を取得する
	[[nodiscard]] std::size_t sizeBytes() const noexcept
	{
		return static_cast<std::size_t>(m_width) *
		       static_cast<std::size_t>(m_height) *
		       static_cast<std::size_t>(m_channels);
	}

	/// @brief ImageData に変換する（ピクセルデータをコピーする）
	/// @return 変換された ImageData（無効な場合は空の ImageData）
	[[nodiscard]] ImageData toImageData() const
	{
		if (!isValid())
		{
			return {};
		}

		ImageData result;
		result.width = m_width;
		result.height = m_height;
		result.channels = m_channels;
		result.pixels.assign(m_pixels, m_pixels + sizeBytes());
		return result;
	}

private:
	std::uint8_t* m_pixels = nullptr;  ///< stbi が確保したピクセルデータ
	int m_width = 0;                   ///< 画像幅
	int m_height = 0;                  ///< 画像高さ
	int m_channels = 0;                ///< チャンネル数
};

/// @brief PNG画像ローダー
/// @details stb_image.hを使用してPNG画像をデコードする。
///          JPEG/GIF/HDR等stb_imageが対応する他形式もフォールバック可能。
///
/// @code
/// mitiru::resource::PngImageLoader loader;
/// auto img = loader.load("textures/player.png");
/// if (img) { /* img->width, img->height, img->pixels */ }
/// @endcode
class PngImageLoader : public IImageLoader
{
public:
	/// @brief PNGファイルを読み込む
	/// @param path ファイルパス
	/// @return 画像データ（RGBA形式、失敗時はnullopt）
	[[nodiscard]] std::optional<ImageData> load(std::string_view path) override
	{
		auto handle = decodeFile(path);
		if (!handle.isValid())
		{
			return std::nullopt;
		}
		return handle.toImageData();
	}

	/// @brief PNG拡張子に対応しているか判定する
	/// @param extension 拡張子（ドット付き）
	/// @return ".png"なら true
	[[nodiscard]] bool canLoad(std::string_view extension) const override
	{
		return extension == ".png" || extension == ".PNG";
	}

	/// @brief メモリ上のデータからPNG画像をデコードする
	/// @param data 画像データのポインタ
	/// @param dataSize データサイズ（バイト）
	/// @param desiredChannels 要求チャンネル数（0=元のまま、4=RGBA強制）
	/// @return RAII管理されたデコード結果
	[[nodiscard]] static StbImageHandle decodeFromMemory(
		const std::uint8_t* data,
		std::size_t dataSize,
		int desiredChannels = 4)
	{
		int w = 0;
		int h = 0;
		int channels = 0;
		auto* pixels = stbi_load_from_memory(
			data,
			static_cast<int>(dataSize),
			&w, &h, &channels, desiredChannels);

		if (!pixels)
		{
			return {};
		}

		const int actualChannels = (desiredChannels != 0) ? desiredChannels : channels;
		return StbImageHandle{pixels, w, h, actualChannels};
	}

	/// @brief ファイルからPNG画像をデコードする
	/// @param path ファイルパス
	/// @param desiredChannels 要求チャンネル数（0=元のまま、4=RGBA強制）
	/// @return RAII管理されたデコード結果
	[[nodiscard]] static StbImageHandle decodeFile(
		std::string_view path,
		int desiredChannels = 4)
	{
		std::ifstream file{std::string{path}, std::ios::binary};
		if (!file.is_open())
		{
			return {};
		}

		file.seekg(0, std::ios::end);
		const auto fileSize = static_cast<std::size_t>(file.tellg());
		file.seekg(0, std::ios::beg);

		if (fileSize == 0)
		{
			return {};
		}

		std::vector<std::uint8_t> fileData(fileSize);
		file.read(reinterpret_cast<char*>(fileData.data()),
		          static_cast<std::streamsize>(fileSize));

		if (!file)
		{
			return {};
		}

		return decodeFromMemory(fileData.data(), fileData.size(), desiredChannels);
	}

	/// @brief stb_imageの直近エラーメッセージを取得する
	/// @return エラーメッセージ（エラーなしの場合は空文字列）
	[[nodiscard]] static std::string lastError()
	{
		const char* err = stbi_failure_reason();
		return err ? std::string(err) : std::string{};
	}
};

} // namespace mitiru::resource
