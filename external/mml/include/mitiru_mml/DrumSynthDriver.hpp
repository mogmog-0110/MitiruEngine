#pragma once
/// @file DrumSynthDriver.hpp
/// @brief ドラムシンセ (DrumSynthDevice) の Driver。サンプルなしでドラムを鳴らす
/// @details チャンネルは 2 通りの使い方ができる。
///          - チャンネル 0〜7 は、種類ごと (0 キック、1 スネア、2 クローズドハイハット、3 タム、4 シンバル、
///            5 リムショット、6 クラップ、7 オープンハイハット)。既存のリズム MML (B S H F Y I) は
///            addRhythmTrack(chip, 0, ...) でそのまま鳴る。音符を書くと、C4 (60) を基準に音程が上下する。
///          - チャンネル 8 は GM のドラムの割り当て (O2 の C = 36 = キック、D = 38 = スネア、F+ = 42 = クローズドハイハット、
///            A+ = 46 = オープンハイハット、D# = 39 = クラップ、タムは 41〜50)。1 本の MML で曲全体のドラムを書ける。
///            このチャンネルの音量、定位、パラメーターは、8 種類すべてに掛かる。

#include <mitiru_mml/ChipDriver.hpp>
#include <mitiru_mml/DrumSynthDevice.hpp>
#include <mitiru_mml/ModernDsp.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

namespace mitiru_mml
{

/// @brief ドラムシンセの Driver
class DrumSynthDriver final : public ChipDriver
{
public:
	static constexpr int KIT_CHANNEL = DrumSynthDevice::CHANNELS;

	explicit DrumSynthDriver(std::uint32_t outputRate = 44100)
		: m_device(outputRate)
	{
		reset();
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
		for (int type = 0; type < DrumSynthDevice::CHANNELS; ++type)
		{
			applyVolume(type);
		}
	}

	[[nodiscard]] int channelCount() const noexcept override
	{
		return KIT_CHANNEL + 1;
	}

	[[nodiscard]] ChannelCaps channelCaps(int channel) const noexcept override
	{
		ChannelCaps caps;
		caps.valid = channel >= 0 && channel <= KIT_CHANNEL;
		caps.pitchSlide = false;
		caps.hardwareEnvelope = false;
		caps.volumeEvents = true;
		caps.extendedSyntax = true;
		return caps;
	}

	void noteOn(int channel, int key, int velocity) override
	{
		static_cast<void>(velocity);
		if (channel >= 0 && channel < KIT_CHANNEL)
		{
			const int semis = (key == 0) ? 0 : std::clamp(key - 60, -24, 24);
			m_device.write(static_cast<std::uint32_t>(channel), DrumSynthDevice::REG_NOTE_ON,
				static_cast<std::uint32_t>(semis + 64));
		}
		else if (channel == KIT_CHANNEL)
		{
			int type = -1;
			int semis = 0;
			if (mapGmKey(key, type, semis))
			{
				m_device.write(static_cast<std::uint32_t>(type), DrumSynthDevice::REG_NOTE_ON,
					static_cast<std::uint32_t>(semis + 64));
			}
		}
	}

	void noteOff(int channel) override
	{
		static_cast<void>(channel);
	}

	void setVoice(int channel, int voice) override
	{
		static_cast<void>(channel);
		static_cast<void>(voice);
	}

	void setVolume(int channel, int volume) override
	{
		forTargets(channel, [this, volume](int type)
		{
			m_state[static_cast<std::size_t>(type)].level = std::clamp(volume, 0, 15);
			applyVolume(type);
		});
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
		if (id < 1000)
		{
			return;
		}
		forTargets(channel, [this, id, value](int type)
		{
			if (id == modern_param::VOL)
			{
				m_state[static_cast<std::size_t>(type)].fine = std::clamp(value, 0, 127);
				applyVolume(type);
				return;
			}
			m_device.write(static_cast<std::uint32_t>(type), static_cast<std::uint32_t>(id), static_cast<std::uint32_t>(value));
		});
	}

private:
	struct ChannelState
	{
		int level = 12;
		int fine = 127;
	};

	template <class Fn>
	void forTargets(int channel, Fn&& fn)
	{
		if (channel >= 0 && channel < KIT_CHANNEL)
		{
			fn(channel);
		}
		else if (channel == KIT_CHANNEL)
		{
			for (int type = 0; type < DrumSynthDevice::CHANNELS; ++type)
			{
				fn(type);
			}
		}
	}

	/// @brief GM のパーカッションの鍵を、ドラムの種類と半音の移調に直す
	[[nodiscard]] static bool mapGmKey(int key, int& type, int& semis) noexcept
	{
		semis = 0;
		switch (key)
		{
		case 35: case 36: type = static_cast<int>(DrumType::Kick); return true;
		case 37: type = static_cast<int>(DrumType::Rim); return true;
		case 38: type = static_cast<int>(DrumType::Snare); return true;
		case 40: type = static_cast<int>(DrumType::Snare); semis = 1; return true;
		case 39: type = static_cast<int>(DrumType::Clap); return true;
		case 42: case 44: type = static_cast<int>(DrumType::ClosedHat); return true;
		case 46: type = static_cast<int>(DrumType::OpenHat); return true;
		case 49: case 52: case 55: case 57: type = static_cast<int>(DrumType::Cymbal); return true;
		case 41: case 43: case 45: case 47: case 48: case 50:
			type = static_cast<int>(DrumType::Tom);
			semis = key - 47;
			return true;
		default: return false;
		}
	}

	void applyVolume(int type)
	{
		const ChannelState& s = m_state[static_cast<std::size_t>(type)];
		const int v = modern_dsp::volume15To127(s.level) * s.fine / 127;
		m_device.write(static_cast<std::uint32_t>(type), DrumSynthDevice::REG_VOLUME, static_cast<std::uint32_t>(v));
	}

	DrumSynthDevice m_device;
	std::array<ChannelState, DrumSynthDevice::CHANNELS> m_state{};
};

} // namespace mitiru_mml
