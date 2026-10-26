#pragma once

/// @file PlMpegSource.hpp
/// @brief pl_mpeg (MIT、単一ヘッダー) を使う MPEG-1 映像復号バックエンドの宣言 (ADR 0104)
/// @details 定義は src/video/PlMpegSource.cpp にある。mitiru.toml の features に "video" を指定したビルドにだけ含まれる (CMake の MITIRU_WITH_VIDEO)。音声は読み込まない。pl_mpeg は PTS を参照せず、最後の 1 枚を返さないため、動画の書き出し手順 (docs/VIDEO.md) で最後の画像を 1 枚追加する。

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <mitiru/video/IVideoSource.hpp>

namespace mitiru::video
{

/// @brief MPEG-PS (.mpg) のバイト列を開く。失敗時は nullptr を返し、error に理由を入れる。
[[nodiscard]] std::unique_ptr<IVideoSource> openPlMpeg(std::vector<std::uint8_t>&& bytes, std::string* error);

}  // namespace mitiru::video
