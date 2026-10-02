#pragma once

/// @file ImageLoader.hpp
/// @brief stb_image ベースの画像ローダー
/// @details メモリ上の PNG データまたはファイルパスから Texture を生成する。
///          stb_image の実装は src/stb_impl.cpp に分離されている。

#include <mitiru/asset/AssetPack.hpp>  // vfs::readGlobal (pack 秘匿配布 / disk fallback)
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/render/Texture.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include <stb_image.h>

namespace mitiru::render
{

/// @brief stb_image ベース画像ローダー
/// @details PNG 画像をデコードして Texture オブジェクトに変換する。
///          メモリバッファからの読み込みとファイルパスからの読み込みを提供する。
class ImageLoader
{
public:
	/// @brief メモリ上の PNG データから Texture を生成する
	/// @param data 画像データのポインタ
	/// @param dataSize データサイズ（バイト）
	/// @return デコードされた Texture（失敗時は空テクスチャ）
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

	/// @brief ファイルパスから Texture を読み込む
	/// @param path ファイルパス
	/// @return デコードされた Texture（失敗時は空テクスチャ）
	[[nodiscard]] static Texture fromFile(const std::string& path)
	{
		// pack が mount 済みなら pack から、未 mount (dev) なら disk から読む。
		const auto buf = mitiru::vfs::readGlobal(path, path);
		if (!buf)
		{
			// 警告なしで空 Texture を返すと原因が分からなくなるので path 単位で初回のみ警告 (R-01 級)
			mitiru::debug::warnOnce("image:" + path, "画像を読み込めない: " + path);
			return {};
		}
		Texture tex = fromMemory(buf->data(), static_cast<int>(buf->size()));
		if (!tex.valid())
		{
			// 読めたがデコード失敗 (壊れた PNG 等) も同様に初回のみ警告
			mitiru::debug::warnOnce("image:" + path, "画像を読み込めない: " + path);
		}
		return tex;
	}

	/// @brief stb_image のエラーメッセージを取得する
	/// @return 直近のエラーメッセージ（エラーなしの場合は空文字列）
	[[nodiscard]] static std::string lastError()
	{
		const char* err = stbi_failure_reason();
		return err ? std::string(err) : std::string{};
	}
};

} // namespace mitiru::render
