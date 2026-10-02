#pragma once

/// @file SpatialRendererFactory.hpp
/// @brief 利用できる中で最適な ISpatialRenderer を作る (HRTF があれば HRTF、なければパン)

#include <cstddef>
#include <memory>

#include <mitiru/audio/SpatialRenderer.hpp>
#include <mitiru/audio/SteamAudioHrtf.hpp>

namespace mitiru::audio
{

/// @param frameSize HRTF が 1 回に畳み込むフレーム数 (パンでは使わない)
/// @return HRTF を作れなければパン。どちらが返ったかは name() で分かる
[[nodiscard]] inline std::unique_ptr<ISpatialRenderer> createSpatialRenderer(
	int sampleRate, std::size_t frameSize)
{
#ifdef MITIRU_HAS_STEAMAUDIO
	auto hrtf = std::make_unique<SteamAudioHrtfRenderer>(sampleRate, frameSize);
	if (hrtf->isReady()) { return hrtf; }
#else
	static_cast<void>(sampleRate);
	static_cast<void>(frameSize);
#endif
	return std::make_unique<PanSpatialRenderer>();
}

} // namespace mitiru::audio
