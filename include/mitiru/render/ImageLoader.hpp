#pragma once

/// @file ImageLoader.hpp
/// @brief stb_imageベースの画像ローダー
/// @details メモリ上のPNGデータまたはファイルパスからTextureを生成する。
///          stb_imageの実装はsrc/stb_impl.cppに分離されている。

#include <mitiru/render/Texture.hpp>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include <stb_image.h>

namespace mitiru::render
{

/// @brief stb_imageベース画像ローダー
/// @details PNG画像をデコードしてTextureオブジェクトに変換する。
///          メモリバッファからの読み込みとファイルパスからの読み込みを提供する。
class ImageLoader
{
public:
	/// @brief メモリ上のPNGデータからTextureを生成する
	/// @param data 画像データのポインタ
	/// @param dataSize データサイズ（バイト）
	/// @return デコードされたTexture（失敗時は空テクスチャ）
	[[nodiscard]] static Texture fromMemory(const std::uint8_t* data, int dataSize)
	{
		int w = 0;
		int h = 0;
		int channels = 0;
		auto* pixels = stbi_load_from_memory(data, dataSize, &w, &h, &channels, 4);
		if (!pixels)
		{
			return {};
		}

		std::vector<std::uint8_t> px(pixels, pixels + static_cast<std::size_t>(w) * h * 4);
		stbi_image_free(pixels);
		return Texture(w, h, px);
	}

	/// @brief ファイルパスからTextureを読み込む
	/// @param path ファイルパス
	/// @return デコードされたTexture（失敗時は空テクスチャ）
	[[nodiscard]] static Texture fromFile(const std::string& path)
	{
		std::ifstream ifs(path, std::ios::binary | std::ios::ate);
		if (!ifs)
		{
			return {};
		}
		const auto size = ifs.tellg();
		ifs.seekg(0);
		std::vector<std::uint8_t> buf(static_cast<std::size_t>(size));
		ifs.read(reinterpret_cast<char*>(buf.data()), size);
		return fromMemory(buf.data(), static_cast<int>(buf.size()));
	}

	/// @brief stb_imageのエラーメッセージを取得する
	/// @return 直近のエラーメッセージ（エラーなしの場合は空文字列）
	[[nodiscard]] static std::string lastError()
	{
		const char* err = stbi_failure_reason();
		return err ? std::string(err) : std::string{};
	}
};

} // namespace mitiru::render
