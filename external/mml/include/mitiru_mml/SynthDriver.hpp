#pragma once
/// @file SynthDriver.hpp
/// @brief 減算シンセ (SynthDevice) の Driver
/// @details チャンネル 0〜15 がそれぞれ別の音色を持つ。音色は setVoice (MML の @n) で組み込みの音色
///          (SynthPatch の synthPreset) から選ぶか、setPatch で自分の音色を渡す。
///          X パラメーター (Xcutoff、Xres、Xwave ...) は setParam を通って、チャンネルの音色を書き換える。

#include <mitiru_mml/ChipDriver.hpp>
#include <mitiru_mml/ModernDsp.hpp>
#include <mitiru_mml/SynthDevice.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

namespace mitiru_mml
{

/// @brief 減算シンセの Driver
class SynthDriver final : public ChipDriver
{
public:
	explicit SynthDriver(std::uint32_t outputRate = 44100)
		: m_device(outputRate)
	{
		reset();
	}

	/// @brief チャンネルの音色を直接渡す
	void setPatch(int channel, const SynthPatch& patch)
	{
		m_device.setPatch(channel, patch);
	}

	[[nodiscard]] ChipDevice& device() noexcept override
	{
		return m_device;
	}

	void reset() override
	{
		m_device.reset();
		for (auto& s : m_state)
		{
			s = ChannelState{};
		}
		for (int ch = 0; ch < SynthDevice::CHANNELS; ++ch)
		{
			applyVolume(ch);
		}
	}

	[[nodiscard]] int channelCount() const noexcept override
	{
		return SynthDevice::CHANNELS;
	}

	[[nodiscard]] ChannelCaps channelCaps(int channel) const noexcept override
	{
		ChannelCaps caps;
		caps.valid = channel >= 0 && channel < SynthDevice::CHANNELS;
		caps.pitchSlide = false;
		caps.hardwareEnvelope = false;
		caps.volumeEvents = true;
		caps.extendedSyntax = true;
		return caps;
	}

	void noteOn(int channel, int key, int velocity) override
	{
		static_cast<void>(velocity);
		if (valid(channel))
		{
			m_device.write(static_cast<std::uint32_t>(channel), SynthDevice::REG_NOTE_ON,
				static_cast<std::uint32_t>(std::clamp(key, 0, 127)));
		}
	}

	void noteOff(int channel) override
	{
		if (valid(channel))
		{
			m_device.write(static_cast<std::uint32_t>(channel), SynthDevice::REG_NOTE_OFF, 0);
		}
	}

	/// @param voice 組み込みの音色の番号。負の値は何もしない (setPatch で渡した音色をそのまま使う)
	void setVoice(int channel, int voice) override
	{
		if (valid(channel) && voice >= 0)
		{
			m_device.write(static_cast<std::uint32_t>(channel), SynthDevice::REG_PRESET,
				static_cast<std::uint32_t>(std::clamp(voice, 0, SYNTH_PRESET_COUNT - 1)));
		}
	}

	void setVolume(int channel, int volume) override
	{
		if (valid(channel))
		{
			m_state[static_cast<std::size_t>(channel)].level = std::clamp(volume, 0, 15);
			applyVolume(channel);
		}
	}

	void setPan(int channel, int pan) override
	{
		setParam(channel, modern_param::PAN, (pan <= 0) ? 0 : (pan == 1 ? 64 : 127), 0);
	}

	void setPitch(int channel, int key) override
	{
		static_cast<void>(channel);
		static_cast<void>(key);
	}

	void setParam(int channel, int id, int value, int value2) override
	{
		static_cast<void>(value2);
		if (!valid(channel) || id < 1000)
		{
			return;
		}
		if (id == modern_param::VOL)
		{
			m_state[static_cast<std::size_t>(channel)].fine = std::clamp(value, 0, 127);
			applyVolume(channel);
			return;
		}
		m_device.write(static_cast<std::uint32_t>(channel), static_cast<std::uint32_t>(id),
			static_cast<std::uint32_t>(value));
	}

private:
	struct ChannelState
	{
		int level = 12;
		int fine = 127;
	};

	[[nodiscard]] static bool valid(int channel) noexcept
	{
		return channel >= 0 && channel < SynthDevice::CHANNELS;
	}

	void applyVolume(int channel)
	{
		const ChannelState& s = m_state[static_cast<std::size_t>(channel)];
		const int v = modern_dsp::volume15To127(s.level) * s.fine / 127;
		m_device.write(static_cast<std::uint32_t>(channel), static_cast<std::uint32_t>(modern_param::VOL),
			static_cast<std::uint32_t>(v));
	}

	SynthDevice m_device;
	std::array<ChannelState, SynthDevice::CHANNELS> m_state{};
};

} // namespace mitiru_mml
