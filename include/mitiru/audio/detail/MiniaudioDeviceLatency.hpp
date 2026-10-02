#pragma once

/// @file MiniaudioDeviceLatency.hpp
/// @brief MiniaudioEngine::outputLatencySec() で使う、デバイスの実際のバッファ長と OS 側の遅延の問い合わせ

#include <mitiru/audio/detail/MiniaudioInclude.hpp>

#if defined(MA_HAS_WASAPI) && defined(_WIN32)
// outputLatencySec() から OS ミキサ側のレイテンシを IAudioClient に直接問い合わせるため。
#include <audioclient.h>
#endif

namespace mitiru::audio::detail {

/// @brief 実際に確保されたデバイスバッファ (フレーム)。
/// @details WASAPI の共有モードでは、要求した period × periods がそのまま使われるとは
///          限らない。internalPeriodSizeInFrames は要求値のままなので、実際に確保された
///          値がある場合はそちらを使う。
[[nodiscard]] inline ma_uint64 deviceBufferFrames(ma_device* dev) noexcept {
#if defined(MA_HAS_WASAPI) && defined(_WIN32)
	if (dev->pContext != nullptr && dev->pContext->backend == ma_backend_wasapi
	    && dev->wasapi.actualBufferSizeInFramesPlayback > 0) {
		return dev->wasapi.actualBufferSizeInFramesPlayback;
	}
#endif
	return static_cast<ma_uint64>(dev->playback.internalPeriodSizeInFrames) *
	       static_cast<ma_uint64>(dev->playback.internalPeriods);
}

/// @brief backend が自前のバッファの外側に持つレイテンシ (秒)。分からない backend は 0。
/// @details WASAPI の共有モードは OS 側のミキサを経由し、その 1 期ぶんが後段に加わる。
///          GetStreamLatency はドライバによっては 0 を返すので、値が得られない場合は
///          GetDevicePeriod の既定値で補う。
[[nodiscard]] inline double backendLatencySec(ma_device* dev) noexcept {
#if defined(MA_HAS_WASAPI) && defined(_WIN32)
	if (dev->pContext == nullptr || dev->pContext->backend != ma_backend_wasapi) { return 0.0; }
	auto* client = static_cast<IAudioClient*>(dev->wasapi.pAudioClientPlayback);
	if (client == nullptr) { return 0.0; }
	REFERENCE_TIME rt = 0;
	if (SUCCEEDED(client->GetStreamLatency(&rt)) && rt > 0) {
		return static_cast<double>(rt) * 1e-7;   // REFERENCE_TIME は 100ns 単位
	}
	REFERENCE_TIME defaultPeriod = 0, minPeriod = 0;
	if (SUCCEEDED(client->GetDevicePeriod(&defaultPeriod, &minPeriod)) && defaultPeriod > 0) {
		return static_cast<double>(defaultPeriod) * 1e-7;
	}
	return 0.0;
#else
	(void)dev;
	return 0.0;
#endif
}

} // namespace mitiru::audio::detail
