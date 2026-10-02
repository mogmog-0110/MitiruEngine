#pragma once

/// @file PcmStemReader.hpp
/// @brief 展開済みの PCM（インターリーブされた float）を先頭から読む IStemReader
/// @details データは呼び出し側が持ち、読み手より長く生かす。同じデータを複数の読み手で共有できる。区間を繰り返して前後の響きが重なる場合も、読む位置は読み手ごとに分かれる。

#include <algorithm>
#include <cstdint>

#include <mitiru/audio/music/MusicCommand.hpp>

namespace mitiru::audio::music
{

class PcmStemReader final : public IStemReader
{
public:
	PcmStemReader() = default;
	PcmStemReader(const float* data, std::uint64_t frames, std::uint32_t channels) noexcept
		: m_data(data), m_frames(frames), m_channels(channels) {}

	void rewind(std::uint64_t startFrame = 0) noexcept { m_cursor = std::min(startFrame, m_frames); }

	std::uint64_t read(float* out, std::uint64_t frames) override
	{
		const std::uint64_t n = std::min(frames, m_frames - m_cursor);
		std::copy_n(m_data + m_cursor * m_channels, n * m_channels, out);
		m_cursor += n;
		return n;
	}

private:
	const float*  m_data = nullptr;
	std::uint64_t m_frames = 0;
	std::uint32_t m_channels = 2;
	std::uint64_t m_cursor = 0;
};

}  // namespace mitiru::audio::music
