#pragma once

/// @file SaveScreenshotPng.hpp
/// @brief RGBA8 ピクセル → PNG ファイル保存ヘルパー
/// @details
/// `Engine::capture()` が返す RGBA8 バッファをそのまま PNG に流すための
/// 薄いヘルパー。consumer 側 (hello_game の F3 など) で
///   -./screenshots/ ディレクトリ作成
///   - YYYYMMDD_HHMMSS のタイムスタンプ生成
///   - PNG 符号化
/// の boilerplate を毎回 50 行ぐらい書いていたのを、ここで引き受ける。
/// 符号化は util::savePng (fpng、engine 同梱の static lib) に任せる。

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

#include <mitiru/util/ImageWriter.hpp>

namespace mitiru::render
{

/// @brief RGBA8 ピクセルバッファを PNG として指定パスに保存する
/// @param rgba   width * height * 4 bytes の RGBA8 データ
/// @param width  画像幅 (px)
/// @param height 画像高さ (px)
/// @param path   出力先 PNG パス (親ディレクトリは事前作成されていること)
/// @return 書き込み成功で true
[[nodiscard]] inline bool savePixelsToPng(
	const std::uint8_t* rgba,
	int width,
	int height,
	const std::string& path)
{
	return util::savePng(path, rgba, width, height);
}

/// @brief 「<dir>/<prefix>_YYYYMMDD_HHMMSS.png」のパスを組み立てて保存する
/// @param rgba   width * height * 4 bytes の RGBA8 データ
/// @param width  画像幅 (px)
/// @param height 画像高さ (px)
/// @param dir    出力ディレクトリ (存在しなければ作成される。default = "screenshots")
/// @param prefix ファイル名先頭 (default = "frame")
/// @return 実際に書いたパス (成功時)、空文字列 (失敗時)
[[nodiscard]] inline std::string saveTimestampedFrameToPng(
	const std::uint8_t* rgba,
	int width,
	int height,
	const std::string& dir    = "screenshots",
	const std::string& prefix = "frame")
{
	if (!rgba || width <= 0 || height <= 0)
	{
		return {};
	}

	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	// create_directories: dir が既存なら false を返す (エラーではない)。
	// ec がセットされた場合のみ致命的とみなす。
	if (ec)
	{
		return {};
	}

	const auto now = std::chrono::system_clock::now();
	const auto tt  = std::chrono::system_clock::to_time_t(now);
	std::tm tm{};
#ifdef _WIN32
	localtime_s(&tm, &tt);
#else
	localtime_r(&tt, &tm);
#endif

	char filename[160];
	std::snprintf(filename, sizeof(filename),
		"%s/%s_%04d%02d%02d_%02d%02d%02d.png",
		dir.c_str(),
		prefix.c_str(),
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec);

	const std::string path = filename;
	if (!savePixelsToPng(rgba, width, height, path))
	{
		return {};
	}
	return path;
}

}  // namespace mitiru::render
