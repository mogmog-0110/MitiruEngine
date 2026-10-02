#pragma once

/// @file TextureCompress.hpp
/// @brief 画像 → mip 連鎖つき BC7 / BC5 / BC4 の DDS (オフライン圧縮、実体は texcompress_impl)
/// @details エンコーダは bc7enc_rdo (BC7) と rgbcx (BC4 / BC5)。drawModel の import と
///          mitiru_texc から呼ばれ、実行時の読み込みは DdsFile.hpp だけで済む。

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <mitiru/render/DdsFile.hpp>

namespace mitiru::render
{

/// @brief 画像の役割。圧縮形式と mip の縮め方がこれで決まる
enum class TextureKind : std::uint8_t
{
	Color,    ///< sRGB の色 → BC7 (sRGB)。アルファも持てる
	Normal,   ///< 接空間法線 → BC5 (XY だけ。Z はシェーダが復元する)
	Mask,     ///< 1 チャンネル (R) → BC4
};

/// @brief RGBA8 画像を mip 連鎖ごと圧縮する。幅と高さが 4 の倍数でないと nullopt
///        (D3D12 は BC の最上段の寸法に 4 の倍数を要求する)
[[nodiscard]] std::optional<MipImage> compressTexture(const std::uint8_t* rgba, std::uint32_t width,
                                                      std::uint32_t height, TextureKind kind);

/// @brief 圧縮済みの 1 段を RGBA8 へ戻す (BC5 は B=0、BC4 は G=B=0、A=255)
[[nodiscard]] std::vector<std::uint8_t> decompressLevel(const MipImage& image, std::size_t level);

/// @brief imagePath の隣に `<imagePath>.dds` を作る。既に有って元画像より新しければ作らない
/// @return 作った (または既存の) dds のパス。読めない・圧縮できない画像は nullopt と error
[[nodiscard]] std::optional<std::string> ensureCompressedTexture(const std::string& imagePath,
                                                                 TextureKind kind, std::string& error);

} // namespace mitiru::render
