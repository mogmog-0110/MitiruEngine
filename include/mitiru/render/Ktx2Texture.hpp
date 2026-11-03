#pragma once

/// @file Ktx2Texture.hpp
/// @brief KTX2 (Basis Universal の ETC1S / UASTC) を、GPU がそのまま読む BC へ transcode して MipImage にする
/// @details 実体は basisu_impl (src/ktx2_texture_impl.cpp)。読み込み側はこのヘッダーだけで済み、DDS と同じ MipImage を
///          Dx12TextureUpload へ渡せる。役割 (TextureKind) で出力を決める: Color → BC7 (sRGB は KTX2 の DFD の
///          転送関数で決める)、Data → BC7 (線形)、Normal → BC5 (R と A を X, Y に。basisu の -separate_rg_to_color_alpha
///          の置き方)、Mask → BC4 (R)。幅か高さが 4 の倍数でない画像は D3D12 の BC に置けないので、Color と Data は
///          RGBA8 に戻す (Normal と Mask は断る)。読むのは 2D の 1 層 1 面だけで、cubemap、配列、HDR の KTX2 は断る。

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include <mitiru/render/DdsFile.hpp>
#include <mitiru/render/TextureCompress.hpp>

namespace mitiru::render
{

/// @brief KTX2 の識別子 (12 byte) で始まるか
[[nodiscard]] inline bool isKtx2Bytes(std::span<const std::uint8_t> bytes) noexcept
{
	constexpr std::uint8_t kId[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
	if (bytes.size() < sizeof(kId)) { return false; }
	for (std::size_t i = 0; i < sizeof(kId); ++i)
	{
		if (bytes[i] != kId[i]) { return false; }
	}
	return true;
}

/// @brief KTX2 を transcode する。対応外・壊れたファイルは nullopt と error に理由
[[nodiscard]] std::optional<MipImage> decodeKtx2(std::span<const std::uint8_t> bytes, TextureKind kind,
                                                 std::string& error);

} // namespace mitiru::render
