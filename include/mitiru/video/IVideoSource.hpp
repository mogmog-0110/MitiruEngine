#pragma once

/// @file IVideoSource.hpp
/// @brief 動画ソースの宣言 (ADR 0104)。実装は src/video/PlMpegSource.cpp。
/// @details 映像だけを順に復号する。音声は別のファイルで、既存の音声経路 (hud.music と BGM の transport) から再生する。
/// 復号は VideoStream の専用 thread から呼ぶ。1 つの source を複数の thread から同時に呼ばない。

#include <cstdint>

#include <mitiru/video/VideoTypes.hpp>

namespace mitiru::video
{

class IVideoSource
{
public:
	virtual ~IVideoSource() = default;

	[[nodiscard]] virtual const VideoInfo& info() const noexcept = 0;

	/// @brief 次の 1 frame を復号する。rgba が nullptr のときは、先へ飛ぶ際の不要な変換を省くため、画像を書き込まない。
	/// rgba は width * height * 4 byte。終端では false を返す。
	virtual bool decodeNext(std::uint8_t* rgba) = 0;

	virtual void rewind() = 0;
};

}  // namespace mitiru::video
