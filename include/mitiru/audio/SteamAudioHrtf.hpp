#pragma once

/// @file SteamAudioHrtf.hpp
/// @brief Steam Audio (Apache-2.0) のバイノーラルエフェクトで、モノラル音源を HRTF を使って両耳用に畳み込む
/// @details -DMITIRU_WITH_STEAMAUDIO=ON で SDK (tools/fetch_steamaudio.py が external/steamaudio に置く)
///          が見つかった構成にのみ中身がある。HRTF は既定のデータセット (SDK 内蔵) を使う。
///          パンとは異なり、前後と上下も耳に届く音の差 (時間差・周波数特性) として表れる。

#ifdef MITIRU_HAS_STEAMAUDIO

#include <phonon.h>

#include <cstddef>
#include <vector>

#include <mitiru/audio/SpatialRenderer.hpp>

namespace mitiru::audio
{

class SteamAudioHrtfRenderer final : public ISpatialRenderer
{
public:
	/// @param frameSize 1 回の process で渡すフレーム数。Steam Audio は固定長のブロック単位で畳み込む
	SteamAudioHrtfRenderer(int sampleRate, std::size_t frameSize)
		: m_frameSize(frameSize)
		, m_planar(frameSize * 3)
	{
		IPLContextSettings contextSettings{};
		contextSettings.version = STEAMAUDIO_VERSION;
		if (iplContextCreate(&contextSettings, &m_context) != IPL_STATUS_SUCCESS) { return; }

		IPLAudioSettings audio{sampleRate, static_cast<IPLint32>(frameSize)};
		IPLHRTFSettings hrtfSettings{};
		hrtfSettings.type = IPL_HRTFTYPE_DEFAULT;
		hrtfSettings.volume = 1.0f;
		if (iplHRTFCreate(m_context, &audio, &hrtfSettings, &m_hrtf) != IPL_STATUS_SUCCESS) { return; }

		IPLBinauralEffectSettings effectSettings{m_hrtf};
		m_ready = iplBinauralEffectCreate(m_context, &audio, &effectSettings, &m_effect) == IPL_STATUS_SUCCESS;
	}

	~SteamAudioHrtfRenderer() override
	{
		if (m_effect) { iplBinauralEffectRelease(&m_effect); }
		if (m_hrtf) { iplHRTFRelease(&m_hrtf); }
		if (m_context) { iplContextRelease(&m_context); }
	}

	SteamAudioHrtfRenderer(const SteamAudioHrtfRenderer&) = delete;
	SteamAudioHrtfRenderer& operator=(const SteamAudioHrtfRenderer&) = delete;
	SteamAudioHrtfRenderer(SteamAudioHrtfRenderer&&) = delete;
	SteamAudioHrtfRenderer& operator=(SteamAudioHrtfRenderer&&) = delete;

	[[nodiscard]] bool isReady() const noexcept { return m_ready; }

	bool process(const float* mono, float* stereo, std::size_t frames,
	             const AudioVec3& direction, float gain) override
	{
		if (!m_ready || frames != m_frameSize) { return false; }
		float* in = m_planar.data();
		float* outL = in + m_frameSize;
		float* outR = outL + m_frameSize;
		for (std::size_t i = 0; i < frames; ++i) { in[i] = mono[i] * gain; }

		float* inChannels[] = {in};
		float* outChannels[] = {outL, outR};
		IPLAudioBuffer inBuf{1, static_cast<IPLint32>(frames), inChannels};
		IPLAudioBuffer outBuf{2, static_cast<IPLint32>(frames), outChannels};
		IPLBinauralEffectParams params{};
		params.direction = IPLVector3{direction.x, direction.y, direction.z};
		params.interpolation = IPL_HRTFINTERPOLATION_BILINEAR;
		params.spatialBlend = 1.0f;
		params.hrtf = m_hrtf;
		iplBinauralEffectApply(m_effect, &params, &inBuf, &outBuf);

		for (std::size_t i = 0; i < frames; ++i)
		{
			stereo[i * 2] = outL[i];
			stereo[i * 2 + 1] = outR[i];
		}
		return true;
	}

	[[nodiscard]] std::size_t blockFrames() const noexcept override { return m_frameSize; }
	[[nodiscard]] const char* name() const noexcept override { return "steamaudio-hrtf"; }

private:
	std::size_t m_frameSize;
	std::vector<float> m_planar;  ///< 入力 1ch + 出力 2ch を planar で並べた作業域。process では確保しない
	IPLContext m_context = nullptr;
	IPLHRTF m_hrtf = nullptr;
	IPLBinauralEffect m_effect = nullptr;
	bool m_ready = false;
};

} // namespace mitiru::audio

#endif // MITIRU_HAS_STEAMAUDIO
