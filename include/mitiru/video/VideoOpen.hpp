#pragma once

/// @file VideoOpen.hpp
/// @brief バイト列の先頭を見て、組み込まれている復号器へ回す (ADR 0116)
/// @details 拡張子ではなく中身で選ぶので、`.mpg` の名前の WebM も、その逆も取り違えない。MPEG-1 (pl_mpeg) は MITIRU_HAS_VIDEO、
///          AV1 の WebM (dav1d) は MITIRU_HAS_VIDEO_AV1、H.264 の MP4 (Media Foundation、ADR 0114) は MITIRU_HAS_VIDEO_MF の build にだけある。形式を足すときは、見分け方と case を 1 つ足す。

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <mitiru/video/IVideoSource.hpp>
#ifdef MITIRU_HAS_VIDEO
#include <mitiru/video/PlMpegSource.hpp>
#endif
#ifdef MITIRU_HAS_VIDEO_AV1
#include <mitiru/video/Av1Source.hpp>
#endif
#ifdef MITIRU_HAS_VIDEO_MF
#include <mitiru/video/MfSource.hpp>
#endif

namespace mitiru::video
{

enum class Container : std::uint8_t
{
	Unknown,
	Mpeg,   ///< MPEG-PS (.mpg)
	Mp4,    ///< ISO の基本メディア形式 (.mp4 / .m4v / .mov)
	WebM,   ///< Matroska / WebM (.webm)。先頭は EBML のヘッダー
};

/// @brief 先頭の byte で入れ物を見分ける。WebM は EBML の ID (1A 45 DF A3)、MP4 は 4 byte 目からの atom の名前、MPEG-PS は 00 00 01 の開始符号
[[nodiscard]] inline Container sniffContainer(const std::vector<std::uint8_t>& bytes) noexcept
{
	if (bytes.size() >= 4 && bytes[0] == 0x1A && bytes[1] == 0x45 && bytes[2] == 0xDF && bytes[3] == 0xA3) { return Container::WebM; }
	if (bytes.size() >= 12)
	{
		const auto is = [&](const char* tag) {
			return bytes[4] == static_cast<std::uint8_t>(tag[0]) && bytes[5] == static_cast<std::uint8_t>(tag[1]) &&
			       bytes[6] == static_cast<std::uint8_t>(tag[2]) && bytes[7] == static_cast<std::uint8_t>(tag[3]);
		};
		if (is("ftyp") || is("moov") || is("mdat") || is("free") || is("skip") || is("wide")) { return Container::Mp4; }
	}
	if (bytes.size() >= 4 && bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 1) { return Container::Mpeg; }
	return Container::Unknown;
}

/// @brief VideoHost の VideoOpener に渡す。組み込まれていない入れ物は、どの feature が要るかを error に書く
[[nodiscard]] inline std::unique_ptr<IVideoSource> openVideo(std::vector<std::uint8_t>&& bytes, std::string* error)
{
	switch (sniffContainer(bytes))
	{
	case Container::WebM:
#ifdef MITIRU_HAS_VIDEO_AV1
		return openAv1WebM(std::move(bytes), error);
#else
		if (error != nullptr) { *error = "WebM (AV1) を開くには、features に \"video\" を入れ、CMake の MITIRU_VIDEO_AV1 を ON にします"; }
		return nullptr;
#endif
	case Container::Mp4:
#ifdef MITIRU_HAS_VIDEO_MF
		return openMfH264(std::move(bytes), error);
#else
		if (error != nullptr) { *error = "MP4 (H.264) を開くには mitiru.toml の features に \"video_mf\" が要ります。ほかに AV1 の WebM と MPEG-1 の .mpg も読めます"; }
		return nullptr;
#endif
	case Container::Mpeg:
	case Container::Unknown:
		break;
	}
#ifdef MITIRU_HAS_VIDEO
	return openPlMpeg(std::move(bytes), error);
#else
	if (error != nullptr) { *error = "MPEG-1 (.mpg) を開くには mitiru.toml の features に \"video\" が要ります"; }
	return nullptr;
#endif
}

}  // namespace mitiru::video
