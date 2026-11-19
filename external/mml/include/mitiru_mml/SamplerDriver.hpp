#pragma once
/// @file SamplerDriver.hpp
/// @brief SF2 サンプラー (SamplerDevice) の Driver。MML から GM の音色とドラムを選べる
/// @details チャンネル 0〜15 は GM のチャンネルと同じ並びで、9 番がパーカッション (バンク 128) になる。
///          音色は setVoice (MML の @n) で GM のプログラム番号 0〜127 を選ぶ。パーカッションでは、
///          音符の高さ (MIDI のノート番号) がドラムの種類になる (O2 の C = 36 = バスドラム、D = 38 = スネア、
///          F+ = 42 = クローズドハイハット)。
///          チャンネル 16〜21 は、既存のリズム MML (B S H F Y I) 用で、パーカッションの
///          バスドラム、スネア、ハイハット、タム、シンバル、リムショットを 1 つずつ受け持つ。

#include <mitiru_mml/ChipDriver.hpp>
#include <mitiru_mml/ModernDsp.hpp>
#include <mitiru_mml/MmlExtensions.hpp>
#include <mitiru_mml/SamplerDevice.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>

namespace mitiru_mml
{

/// @brief SF2 サンプラーの Driver
class SamplerDriver final : public ChipDriver
{
public:
	static constexpr int MELODIC_CHANNELS = SamplerDevice::CHANNELS;
	static constexpr int RHYTHM_CHANNEL_BASE = SamplerDevice::CHANNELS;
	static constexpr int RHYTHM_CHANNELS = 6;

	explicit SamplerDriver(std::uint32_t outputRate = 44100)
		: m_device(outputRate)
	{
		reset();
	}

	/// @brief 使う SoundFont を渡す (渡さないと無音)
	void setSoundFont(std::shared_ptr<const Sf2File> soundFont)
	{
		m_device.setSoundFont(std::move(soundFont));
		reset();
	}

	/// @brief チャンネルに楽器を直接渡す。SFZ (SfzFile) や既存の MultiSampleInstrument を、OPNA と同じ時間軸で鳴らせる
	void setInstrument(int channel, std::shared_ptr<const MultiSampleInstrument> instrument)
	{
		m_device.setInstrument(channel, std::move(instrument));
	}

	[[nodiscard]] ChipDevice& device() noexcept override
	{
		return m_device;
	}

	[[nodiscard]] SamplerDevice& samplerDevice() noexcept
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
		for (int ch = 0; ch < MELODIC_CHANNELS; ++ch)
		{
			applyVolume(ch);
			m_device.write(static_cast<std::uint32_t>(ch), SamplerDevice::REG_PAN, 64);
		}
	}

	[[nodiscard]] int channelCount() const noexcept override
	{
		return RHYTHM_CHANNEL_BASE + RHYTHM_CHANNELS;
	}

	[[nodiscard]] ChannelCaps channelCaps(int channel) const noexcept override
	{
		ChannelCaps caps;
		caps.valid = channel >= 0 && channel < channelCount();
		caps.pitchSlide = false;
		caps.hardwareEnvelope = false;
		caps.volumeEvents = true;
		caps.extendedSyntax = true;
		return caps;
	}

	void noteOn(int channel, int key, int velocity) override
	{
		if (!validChannel(channel))
		{
			return;
		}
		const int device = deviceChannel(channel);
		const int playKey = isRhythm(channel) ? RHYTHM_KEYS[static_cast<std::size_t>(channel - RHYTHM_CHANNEL_BASE)] : key;
		const int vel127 = std::clamp(modern_dsp::volume15To127(velocity), 1, 127);
		m_device.write(static_cast<std::uint32_t>(device), SamplerDevice::REG_NOTE_ON,
			static_cast<std::uint32_t>(std::clamp(playKey, 0, 127) | (vel127 << 8)));
	}

	void noteOff(int channel) override
	{
		if (validChannel(channel))
		{
			m_device.write(static_cast<std::uint32_t>(deviceChannel(channel)), SamplerDevice::REG_NOTE_OFF, 0);
		}
	}

	void setVoice(int channel, int voice) override
	{
		// 負の値は何もしない (setInstrument で渡した楽器をそのまま使う)
		if (validChannel(channel) && !isRhythm(channel) && voice >= 0)
		{
			m_device.write(static_cast<std::uint32_t>(channel), SamplerDevice::REG_PROGRAM,
				static_cast<std::uint32_t>(std::clamp(voice, 0, 127)));
		}
	}

	void setVolume(int channel, int volume) override
	{
		if (validChannel(channel))
		{
			const int ch = deviceChannel(channel);
			m_state[static_cast<std::size_t>(ch)].level = std::clamp(volume, 0, 15);
			applyVolume(ch);
		}
	}

	void setPan(int channel, int pan) override
	{
		if (validChannel(channel))
		{
			const int value = (pan <= 0) ? 0 : (pan == 1 ? 64 : 127);
			m_device.write(static_cast<std::uint32_t>(deviceChannel(channel)), SamplerDevice::REG_PAN,
				static_cast<std::uint32_t>(value));
		}
	}

	void setPitch(int channel, int key) override
	{
		static_cast<void>(channel);
		static_cast<void>(key);
	}

	void setParam(int channel, int id, int value, int value2) override
	{
		static_cast<void>(value2);
		if (!validChannel(channel))
		{
			return;
		}
		const int ch = deviceChannel(channel);
		const auto port = static_cast<std::uint32_t>(ch);
		switch (id)
		{
		case modern_param::VOL:
			m_state[static_cast<std::size_t>(ch)].fine = std::clamp(value, 0, 127);
			applyVolume(ch);
			break;
		case modern_param::PAN:
			m_device.write(port, SamplerDevice::REG_PAN, static_cast<std::uint32_t>(std::clamp(value, 0, 127)));
			break;
		case modern_param::TUNE:
			m_device.write(port, SamplerDevice::REG_TUNE, static_cast<std::uint32_t>(value));
			break;
		case modern_param::REL:
			m_device.write(port, SamplerDevice::REG_RELEASE, static_cast<std::uint32_t>(std::max(0, value)));
			break;
		default:
			break;
		}
	}

private:
	/// @brief リズム MML のドラム (B S H F Y I) が鳴らす GM のパーカッションの鍵
	static constexpr std::array<int, RHYTHM_CHANNELS> RHYTHM_KEYS = {36, 38, 42, 45, 49, 37};

	struct ChannelState
	{
		int level = 12;  ///< V の値 (0〜15)。MML の既定の音量
		int fine = 127;  ///< Xvol の値
	};

	[[nodiscard]] bool validChannel(int channel) const noexcept
	{
		return channel >= 0 && channel < channelCount();
	}

	[[nodiscard]] static bool isRhythm(int channel) noexcept
	{
		return channel >= RHYTHM_CHANNEL_BASE;
	}

	[[nodiscard]] static int deviceChannel(int channel) noexcept
	{
		return isRhythm(channel) ? SamplerDevice::PERCUSSION_CHANNEL : channel;
	}

	void applyVolume(int ch)
	{
		const ChannelState& s = m_state[static_cast<std::size_t>(ch)];
		const int v = modern_dsp::volume15To127(s.level) * s.fine / 127;
		m_device.write(static_cast<std::uint32_t>(ch), SamplerDevice::REG_VOLUME, static_cast<std::uint32_t>(v));
	}

	SamplerDevice m_device;
	std::array<ChannelState, SamplerDevice::CHANNELS> m_state{};
};

} // namespace mitiru_mml
