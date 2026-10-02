#pragma once

/// @file MiniaudioOutput.hpp
/// @brief miniaudio の ma_device へ PCM を出力する IAudioOutput
/// @details OS ごとの出力 (WASAPI / DirectSound / WinMM / PulseAudio / ALSA / CoreAudio...) の
///          選択は miniaudio に任せる。write() は SPSC リングへの書き込みだけを行い、デバイスのスレッドが
///          そこから読み出す。不足分は無音にするため、書き手が遅れても出力は停止しない。

#include <mitiru/audio/detail/MiniaudioInclude.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>

#include <mitiru/audio/IAudioOutput.hpp>
#include <mitiru/audio/RingBuffer.hpp>

namespace mitiru::audio
{

class MiniaudioOutput : public IAudioOutput
{
public:
	/// @brief どのデバイスへ出すか
	enum class Target
	{
		SystemDefault, ///< OS の既定の出力
		Silent,        ///< miniaudio の null backend。実時間で消費するが鳴らない (テスト・ヘッドレス)
	};

	explicit MiniaudioOutput(Target target = Target::SystemDefault) noexcept
		: m_target(target)
	{
	}

	~MiniaudioOutput() override { shutdown(); }

	// デバイススレッドが this を pUserData として保持するため、移動できない。
	MiniaudioOutput(const MiniaudioOutput&) = delete;
	MiniaudioOutput& operator=(const MiniaudioOutput&) = delete;
	MiniaudioOutput(MiniaudioOutput&&) = delete;
	MiniaudioOutput& operator=(MiniaudioOutput&&) = delete;

	/// @param bufferSize リングに保持できるサンプル数 (全チャンネル合計)
	bool initialize(int sampleRate, int channels, int bufferSize) override
	{
		if (m_device || sampleRate <= 0 || channels <= 0 || bufferSize <= 0) { return false; }
		m_ring = std::make_unique<RingBuffer<float>>(static_cast<std::size_t>(bufferSize));
		auto device = std::make_unique<ma_device>();
		ma_device_config config = ma_device_config_init(ma_device_type_playback);
		config.playback.format = ma_format_f32;
		config.playback.channels = static_cast<ma_uint32>(channels);
		config.sampleRate = static_cast<ma_uint32>(sampleRate);
		config.dataCallback = &MiniaudioOutput::dataCallback;
		config.pUserData = this;
		const ma_backend silent[] = {ma_backend_null};
		const bool useSilent = (m_target == Target::Silent);
		if (ma_device_init_ex(useSilent ? silent : nullptr, useSilent ? 1 : 0, nullptr, &config,
		                      device.get()) != MA_SUCCESS)
		{
			m_ring.reset();
			return false;
		}
		if (ma_device_start(device.get()) != MA_SUCCESS)
		{
			ma_device_uninit(device.get());
			m_ring.reset();
			return false;
		}
		m_device = std::move(device);
		m_sampleRate = sampleRate;
		m_channels = channels;
		m_bufferSize = bufferSize;
		return true;
	}

	/// @return count の全てをリングに書き込めたら true。書き込めない分は破棄する (writableSamples() を確認して書き込む)
	bool write(const float* samples, std::size_t count) override
	{
		if (!m_device || samples == nullptr || count == 0) { return false; }
		return m_ring->write(samples, count) == count;
	}

	[[nodiscard]] std::size_t writableSamples() const override
	{
		return m_ring ? m_ring->availableWrite() : 0;
	}

	[[nodiscard]] bool isPlaying() const override
	{
		return m_device && ma_device_is_started(m_device.get()) == MA_TRUE;
	}

	void shutdown() override
	{
		if (m_device)
		{
			ma_device_uninit(m_device.get());
			m_device.reset();
		}
		m_ring.reset();
		m_sampleRate = 0;
		m_channels = 0;
		m_bufferSize = 0;
	}

	[[nodiscard]] bool isInitialized() const override { return m_device != nullptr; }
	[[nodiscard]] int sampleRate() const override { return m_sampleRate; }
	[[nodiscard]] int channels() const override { return m_channels; }
	[[nodiscard]] int bufferSize() const override { return m_bufferSize; }

	[[nodiscard]] const char* backendName() const override
	{
		if (!m_device || m_device->pContext == nullptr) { return "miniaudio"; }
		return ma_get_backend_name(m_device->pContext->backend);
	}

private:
	static void dataCallback(ma_device* device, void* out, const void*, ma_uint32 frameCount)
	{
		auto* self = static_cast<MiniaudioOutput*>(device->pUserData);
		auto* dst = static_cast<float*>(out);
		const std::size_t want = static_cast<std::size_t>(frameCount) * device->playback.channels;
		const std::size_t got = self->m_ring->read(dst, want);
		std::fill(dst + got, dst + want, 0.0f);
	}

	Target m_target;
	std::unique_ptr<ma_device> m_device;
	std::unique_ptr<RingBuffer<float>> m_ring;
	int m_sampleRate = 0;
	int m_channels = 0;
	int m_bufferSize = 0;
};

} // namespace mitiru::audio
