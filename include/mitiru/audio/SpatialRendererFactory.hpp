#pragma once

/// @file SpatialRendererFactory.hpp
/// @brief 利用できる中で最適な ISpatialRenderer を作る (Steam Audio の HRTF があれば HRTF、なければ binauralModel が true のときヘッドモデル、それ以外はパン)

#include <cstddef>
#include <memory>

#include <mitiru/audio/BinauralModel.hpp>
#include <mitiru/audio/SpatialRenderer.hpp>
#include <mitiru/audio/SteamAudioHrtf.hpp>

namespace mitiru::audio
{

/// @param frameSize HRTF が 1 回に畳み込むフレーム数 (パンでは使わない)
/// @param binauralModel 測った HRTF が無いとき、ヘッドモデル (BinauralModelRenderer) で両耳へ置くか。mix.json の binaural
/// @return HRTF を作れなければパン。どちらが返ったかは name() で分かる
[[nodiscard]] inline std::unique_ptr<ISpatialRenderer> createSpatialRenderer(
	int sampleRate, std::size_t frameSize, bool binauralModel = false)
{
#ifdef MITIRU_HAS_STEAMAUDIO
	auto hrtf = std::make_unique<SteamAudioHrtfRenderer>(sampleRate, frameSize);
	if (hrtf->isReady()) { return hrtf; }
#else
	static_cast<void>(frameSize);
#endif
	if (binauralModel) { return std::make_unique<BinauralModelRenderer>(sampleRate); }
	return std::make_unique<PanSpatialRenderer>();
}

} // namespace mitiru::audio
