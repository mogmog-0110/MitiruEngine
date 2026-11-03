#pragma once

/// @file Av1Source.hpp
/// @brief WebM に入った AV1 の映像を dav1d で復号するバックエンドの宣言 (ADR 0116)
/// @details 定義は src/video/Av1Source.cpp。mitiru.toml の features に "video" を指定した build にだけ含まれる (CMake の MITIRU_WITH_VIDEO と MITIRU_VIDEO_AV1)。
/// 8 ビットだけを復号する。音は読まない (別のファイルを既存の音の経路で鳴らす)。

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <mitiru/video/IVideoSource.hpp>

namespace mitiru::video
{

/// 復号した 1 コマの YUV の面。試験が dav1d の出力を別の実装 (libaom) と画素ごとに比べるために使う
struct Av1Planes
{
	int                       width = 0;
	int                       height = 0;
	int                       chromaWidth = 0;    ///< 色の面が無い (4:0:0) ときは 0
	int                       chromaHeight = 0;
	std::vector<std::uint8_t> y;                  ///< width * height
	std::vector<std::uint8_t> u;                  ///< chromaWidth * chromaHeight
	std::vector<std::uint8_t> v;
};

class IAv1Source : public IVideoSource
{
public:
	/// 次の 1 コマを YUV のまま取り出す。終端では false
	virtual bool decodeNextPlanes(Av1Planes& out) = 0;
};

/// @brief WebM (AV1) のバイト列を開く。失敗時は nullptr を返し、error に理由を入れる
[[nodiscard]] std::unique_ptr<IAv1Source> openAv1WebM(std::vector<std::uint8_t>&& bytes, std::string* error);

}  // namespace mitiru::video
