#pragma once

/// @file MaterialTextures.hpp
/// @brief glTF の材質のテクスチャを GPU へ上げる形 (mip 連鎖つき、辺が 4 の倍数なら BC 圧縮) にする
/// @details 圧縮は大きな画像で数百 ms かかるので、結果を `<モデル>.m<材質番号>.<役割>.dds` に置いて次から読む
///          (モデルか外部の画像より古い DDS は作り直す)。pack を mount 中は pack の中の DDS だけを読み、何も書かない。
///          圧縮できない画像 (辺が 4 の倍数でない・エンコーダの無いビルド) は RGBA8 の mip 連鎖にする。

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/render/CpuTexture.hpp>
#include <mitiru/render/DdsFile.hpp>
#include <mitiru/render/TextureCompress.hpp>
#include <mitiru/render/TextureMips.hpp>

namespace mitiru::render
{

/// @brief 圧縮済みの DDS を置く場所。logicalPath は vfs で読む名前、diskPath は開発中に書く先
struct MaterialTextureSidecar
{
	std::string logicalPath;
	std::filesystem::path diskPath;        ///< 空なら書かない
	std::filesystem::path modelDiskPath;   ///< これより古い DDS は使わない (空なら比べない)
	std::filesystem::path imageDiskPath;   ///< 外部の画像。これより古い DDS も使わない (埋め込みなら空)
};

/// @brief その役割の画像を圧縮したときの形式
[[nodiscard]] constexpr TextureFormat compressedFormatOf(TextureKind kind) noexcept
{
	switch (kind)
	{
	case TextureKind::Normal: return TextureFormat::Bc5;
	case TextureKind::Mask:   return TextureFormat::Bc4;
	case TextureKind::Data:   return TextureFormat::Bc7;
	case TextureKind::Color:  break;
	}
	return TextureFormat::Bc7Srgb;
}

namespace detail
{

/// @brief vfs の論理パスを開発中のディスクの場所にする (vfs::readGlobal と同じ順: MITIRU_ASSET_ROOT、次に cwd)
[[nodiscard]] inline std::filesystem::path diskPathOfLogical(const std::string& logical)
{
	const std::filesystem::path rel{logical};
	const auto& root = vfs::detail::globalDiskRoot();
	std::error_code ec;
	if (!root.empty() && rel.is_relative() && std::filesystem::exists(root / rel, ec)) { return root / rel; }
	return rel;
}

/// @brief file が source より古いか (source が空・無いなら比べない)
[[nodiscard]] inline bool isOlderThan(const std::filesystem::path& file, const std::filesystem::path& source)
{
	std::error_code ec;
	if (source.empty() || !std::filesystem::exists(source, ec)) { return false; }
	return std::filesystem::last_write_time(file, ec) < std::filesystem::last_write_time(source, ec);
}

[[nodiscard]] inline std::optional<MipImage> readSidecar(const MaterialTextureSidecar& s, TextureFormat expected)
{
	std::optional<std::vector<std::uint8_t>> bytes;
	if (vfs::hasGlobalMount())
	{
		bytes = vfs::readGlobal(s.logicalPath);
	}
	else
	{
		std::error_code ec;
		if (s.diskPath.empty() || !std::filesystem::exists(s.diskPath, ec)) { return std::nullopt; }
		if (isOlderThan(s.diskPath, s.modelDiskPath) || isOlderThan(s.diskPath, s.imageDiskPath)) { return std::nullopt; }
		bytes = vfs::detail::readDiskFile(s.diskPath);
	}
	if (!bytes) { return std::nullopt; }
	std::string error;
	auto img = decodeDds(*bytes, error);
	if (!img || img->format != expected) { return std::nullopt; }
	return img;
}

/// 書けなくても (読み取り専用の場所など) その回は圧縮した結果をそのまま使う
inline void writeSidecar(const MaterialTextureSidecar& s, const MipImage& img)
{
	if (vfs::hasGlobalMount() || s.diskPath.empty()) { return; }
	const std::filesystem::path tmp(s.diskPath.string() + ".tmp");
	{
		std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
		if (!f) { return; }
		const auto bytes = encodeDds(img);
		f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if (!f) { return; }
	}
	std::error_code ec;
	std::filesystem::rename(tmp, s.diskPath, ec);
	if (ec) { std::filesystem::remove(tmp, ec); }
}

[[nodiscard]] inline MipImage uncompressedMips(const CpuTexture& src, TextureKind kind)
{
	MipImage img;
	img.format = (kind == TextureKind::Color) ? TextureFormat::Rgba8Srgb : TextureFormat::Rgba8;
	img.width = static_cast<std::uint32_t>(src.width);
	img.height = static_cast<std::uint32_t>(src.height);
	const MipFilter filter = (kind == TextureKind::Color)    ? MipFilter::Srgb
	                       : (kind == TextureKind::Normal)   ? MipFilter::NormalMap
	                                                         : MipFilter::Linear;
	img.mips = buildMipChain(src.rgba.data(), src.width, src.height, filter);
	return img;
}

} // namespace detail

/// @brief モデル modelPath の material 番目の役割 role ("base" / "normal" / "mr" / "emissive") の DDS の置き場
/// @param imagePath 外部の画像の論理パス (埋め込みなら空)。画像が更新されたら DDS を作り直す
[[nodiscard]] inline MaterialTextureSidecar materialTextureSidecar(const std::string& modelPath, std::size_t material,
                                                                   const char* role, const std::string& imagePath = {})
{
	MaterialTextureSidecar s;
	s.logicalPath = modelPath + ".m" + std::to_string(material) + "." + role + ".dds";
	if (!vfs::hasGlobalMount())
	{
		s.modelDiskPath = detail::diskPathOfLogical(modelPath);
		s.diskPath = s.modelDiskPath.string() + ".m" + std::to_string(material) + "." + role + ".dds";
		if (!imagePath.empty()) { s.imageDiskPath = detail::diskPathOfLogical(imagePath); }
	}
	return s;
}

/// @brief 1 枚を GPU へ上げる形にする。sidecar があれば圧縮の結果をそこから読み、無ければ作って置く
[[nodiscard]] inline MipImage prepareMaterialTexture(const CpuTexture& src, TextureKind kind,
                                                     const MaterialTextureSidecar* sidecar)
{
	const TextureFormat bc = compressedFormatOf(kind);
	if (sidecar != nullptr)
	{
		if (auto cached = detail::readSidecar(*sidecar, bc)) { return std::move(*cached); }
	}
	if (!src.valid()) { return {}; }
	if (auto img = compressTexture(src.rgba.data(), static_cast<std::uint32_t>(src.width),
	                               static_cast<std::uint32_t>(src.height), kind))
	{
		if (sidecar != nullptr) { detail::writeSidecar(*sidecar, *img); }
		return std::move(*img);
	}
	return detail::uncompressedMips(src, kind);
}

} // namespace mitiru::render
