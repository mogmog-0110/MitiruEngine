#pragma once

/// @file MfSource.hpp
/// @brief Media Foundation で H.264 の MP4 を復号する動画バックエンドの宣言 (ADR 0114)
/// @details 定義は src/video/MfSource.cpp。mitiru.toml の features に "video_mf" を書いたビルド (CMake の MITIRU_WITH_VIDEO_MF、Windows のみ)
///          にだけ含まれる。N エディションの Windows は Media Feature Pack を入れるまで Media Foundation の部品が無く、開けずに理由を返す。
///          映像だけを読み、音は別のファイルを既存の経路で鳴らす (pl_mpeg の経路と同じ)。色変換は video/Nv12.hpp の整数で行うので、
///          OS の色変換のバージョンの違いは絵に出ない。復号は OS のソフトウェアの H.264 デコーダ (DXVA は使わない)。

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <mitiru/video/IVideoSource.hpp>

namespace mitiru::video
{

/// @brief MP4 (H.264) のバイト列を開く。失敗時は nullptr を返し、error に理由を入れる。
[[nodiscard]] std::unique_ptr<IVideoSource> openMfH264(std::vector<std::uint8_t>&& bytes, std::string* error);

/// @brief 試験用。true の間、Media Foundation の開始が失敗した環境 (N エディション) を再現する
void mfForceStartupFailure(bool on) noexcept;

}  // namespace mitiru::video
