#pragma once

/// @file ImageWriter.hpp
/// @brief RGBA8 ピクセルを PNG / BMP にする (PNG は fpng、BMP は stb_image_write)
/// @details PNG は /api/screenshot や --capture-dir がゲームスレッドで 1 フレームの中に符号化するので、
///          圧縮率より速さを取る fpng を使う (出力はどの PNG デコーダでも読める普通の PNG)。

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <fpng/fpng.h>
#include <stb_image_write.h>

namespace mitiru::util {

namespace detail {

[[nodiscard]] inline bool isRgbaImage(const std::uint8_t* rgba, std::size_t size, int w, int h) noexcept
{
	return rgba != nullptr && w > 0 && h > 0
		&& size >= static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4;
}

} // namespace detail

/// @brief RGBA8 (w*h*4 バイト、行の詰め物なし) を PNG のバイト列にする。入力が不正なら空
[[nodiscard]] inline std::vector<std::uint8_t> encodePng(const std::uint8_t* rgba, int w, int h)
{
	if (rgba == nullptr || w <= 0 || h <= 0) { return {}; }
	// fpng_init は SSE4.1 / PCLMUL の有無を調べるだけで、プロセスで 1 回呼べばよい。
	static const bool initialized = (fpng::fpng_init(), true);
	(void)initialized;
	std::vector<std::uint8_t> png;
	if (!fpng::fpng_encode_image_to_memory(rgba, static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), 4, png))
	{
		return {};
	}
	return png;
}

/// @brief RGBA8 (w*h*4 バイト) を PNG ファイルに保存する
inline bool savePng(const std::string& path, const std::uint8_t* rgba, int w, int h)
{
	const auto png = encodePng(rgba, w, h);
	if (png.empty()) { return false; }
	std::ofstream out(path, std::ios::binary);
	out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
	return static_cast<bool>(out);
}

/// @brief RGBA8 を PNG ファイルに保存する。pixels が w*h*4 バイトに足りなければ書かない
inline bool savePng(const std::string& path, const std::vector<std::uint8_t>& pixels, int w, int h)
{
	if (!detail::isRgbaImage(pixels.data(), pixels.size(), w, h)) { return false; }
	return savePng(path, pixels.data(), w, h);
}

/// @brief RGBA8 を BMP ファイル (32bit、アルファ付き) に保存する
inline bool saveBmp(const std::string& path, const std::vector<std::uint8_t>& pixels, int w, int h)
{
	if (!detail::isRgbaImage(pixels.data(), pixels.size(), w, h)) { return false; }
	return stbi_write_bmp(path.c_str(), w, h, 4, pixels.data()) != 0;
}

/// @brief 連番 PNG を `<dir>/<prefix>_0001.png` の形で書き出す (動画化は ffmpeg 等の外部ツールで行う)
/// @param dir 出力ディレクトリ (末尾スラッシュは任意。存在している前提)
/// @return 書き出したファイルパス
inline std::string saveFrameSequence(const std::string& dir, const std::string& prefix,
	int frameIndex, const std::vector<std::uint8_t>& pixels, int w, int h)
{
	char num[8];
	std::snprintf(num, sizeof(num), "%04d", frameIndex < 0 ? 0 : frameIndex);
	std::string base = dir;
	if (!base.empty() && base.back() != '/' && base.back() != '\\') { base += '/'; }
	const std::string path = base + prefix + "_" + num + ".png";
	savePng(path, pixels, w, h);
	return path;
}

} // namespace mitiru::util
