/// @file ktx2_texture_impl.cpp
/// @brief Ktx2Texture.hpp の実装。basis_universal の transcoder はこの TU にだけ入れる

#include <mitiru/render/Ktx2Texture.hpp>

#include <mutex>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4267 4456 4457 4100 4505 4701 4702)
#endif
#include <basisu_transcoder.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace mitiru::render
{
namespace
{

/// ETC1S の表は process で 1 回だけ組む (basisu の決まり)
void initTranscoderOnce()
{
	static std::once_flag once;
	std::call_once(once, [] { basist::basisu_transcoder_init(); });
}

struct Target
{
	basist::transcoder_texture_format tf;
	TextureFormat format;
};

[[nodiscard]] Target targetOf(TextureKind kind, bool srgb, bool blockAligned, std::string& error)
{
	switch (kind)
	{
	case TextureKind::Normal:
		if (!blockAligned) { error = "辺が 4 の倍数でない法線の KTX2 は BC5 に置けない"; }
		return {basist::transcoder_texture_format::cTFBC5_RG, TextureFormat::Bc5};
	case TextureKind::Mask:
		if (!blockAligned) { error = "辺が 4 の倍数でないマスクの KTX2 は BC4 に置けない"; }
		return {basist::transcoder_texture_format::cTFBC4_R, TextureFormat::Bc4};
	case TextureKind::Data:
		if (!blockAligned) { return {basist::transcoder_texture_format::cTFRGBA32, TextureFormat::Rgba8}; }
		return {basist::transcoder_texture_format::cTFBC7_RGBA, TextureFormat::Bc7};
	case TextureKind::Color:
		break;
	}
	if (!blockAligned)
	{
		return {basist::transcoder_texture_format::cTFRGBA32, srgb ? TextureFormat::Rgba8Srgb : TextureFormat::Rgba8};
	}
	return {basist::transcoder_texture_format::cTFBC7_RGBA, srgb ? TextureFormat::Bc7Srgb : TextureFormat::Bc7};
}

} // namespace

std::optional<MipImage> decodeKtx2(std::span<const std::uint8_t> bytes, TextureKind kind, std::string& error)
{
	// targetOf が error の空かどうかで断るので、前の呼び出しの理由を持ち越さない
	error.clear();
	if (!isKtx2Bytes(bytes))
	{
		error = "KTX2 ではない";
		return std::nullopt;
	}
	if (bytes.size() > 0xFFFFFFFFu)
	{
		error = "KTX2 が大きすぎる";
		return std::nullopt;
	}
	initTranscoderOnce();
	basist::ktx2_transcoder tr;
	if (!tr.init(bytes.data(), static_cast<std::uint32_t>(bytes.size())))
	{
		error = "KTX2 として読めない (壊れているか、ETC1S / UASTC の 2D ではない)";
		return std::nullopt;
	}
	if (tr.get_faces() != 1 || tr.get_layers() > 1)
	{
		error = "cubemap と配列の KTX2 は読まない";
		return std::nullopt;
	}
	if (!tr.is_etc1s() && !tr.is_uastc())
	{
		error = "ETC1S と UASTC 以外 (UASTC HDR、ASTC) の KTX2 は読まない";
		return std::nullopt;
	}
	const std::uint32_t width = tr.get_width();
	const std::uint32_t height = tr.get_height();
	const std::uint32_t levels = tr.get_levels();
	if (width == 0 || height == 0 || width > 16384 || height > 16384 || levels == 0 || levels > 15)
	{
		error = "KTX2 の寸法か mip 段数が範囲外";
		return std::nullopt;
	}
	const bool blockAligned = (width % 4 == 0) && (height % 4 == 0);
	const Target target = targetOf(kind, tr.is_srgb(), blockAligned, error);
	if (!error.empty()) { return std::nullopt; }
	if (!tr.start_transcoding())
	{
		error = "KTX2 の transcode を始められない (ETC1S の表か zstd の展開に失敗)";
		return std::nullopt;
	}

	MipImage img;
	img.format = target.format;
	img.width = width;
	img.height = height;
	img.hasAlpha = (kind == TextureKind::Color) && tr.get_has_alpha() != 0;
	const bool bc = isBlockCompressed(target.format);
	for (std::uint32_t lv = 0; lv < levels; ++lv)
	{
		const std::uint32_t w = mipExtent(width, lv);
		const std::uint32_t h = mipExtent(height, lv);
		std::vector<std::uint8_t> level(textureLevelBytes(target.format, w, h));
		const std::uint32_t units = bc ? ((w + 3) / 4) * ((h + 3) / 4) : w * h;
		if (!tr.transcode_image_level(lv, 0, 0, level.data(), units, target.tf))
		{
			error = "KTX2 の mip " + std::to_string(lv) + " を transcode できない";
			return std::nullopt;
		}
		img.mips.push_back(std::move(level));
	}
	return img;
}

} // namespace mitiru::render
