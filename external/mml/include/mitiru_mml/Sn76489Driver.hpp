#pragma once
/// @file Sn76489Driver.hpp
/// @brief SN76489 (PSG) の Driver (層 2): 音符を周期と減衰のレジスタ書き込みに変える
/// @details チャンネル 0〜2 が矩形波、3 がノイズ。音色は無く、ノイズだけ setVoice で
///          0 = 白色ノイズ、1 = 周期性ノイズを選ぶ。音程は 10bit の周期の整数で決まるので、
///          高い音ほど 12 平均律からずれる (keyToPeriod と periodToHz で誤差を求められる)。

#include <mitiru_mml/ChipDriver.hpp>
#include <mitiru_mml/Sn76489Device.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace mitiru_mml
{

/// @brief SN76489 ドライバー
class Sn76489Driver final : public ChipDriver
{
public:
	static constexpr int TONE_CHANNELS = 3;
	static constexpr int NOISE_CHANNEL = 3;
	static constexpr int CHANNELS = 4;
	/// @brief 周期の範囲 (10bit)。0 は使わない
	static constexpr int MAX_PERIOD = 1023;

	Sn76489Driver()
	{
		initState();
	}

	[[nodiscard]] ChipDevice& device() noexcept override
	{
		return m_device;
	}

	void reset() override
	{
		m_device.reset();
		initState();
	}

	[[nodiscard]] int channelCount() const noexcept override
	{
		return CHANNELS;
	}

	[[nodiscard]] ChannelCaps channelCaps(int channel) const noexcept override
	{
		ChannelCaps caps;
		if (channel >= 0 && channel < CHANNELS)
		{
			caps.valid = true;
			caps.pitchSlide = (channel < TONE_CHANNELS);
		}
		return caps;
	}

	void noteOn(int channel, int key, int velocity) override
	{
		static_cast<void>(velocity);
		if (!valid(channel))
		{
			return;
		}
		if (channel == NOISE_CHANNEL)
		{
			writeNoiseControl(key);
		}
		else
		{
			writePeriod(channel, keyToPeriod(key));
		}
		m_sounding[idx(channel)] = true;
		writeAttenuation(channel);
	}

	void noteOff(int channel) override
	{
		if (!valid(channel))
		{
			return;
		}
		m_sounding[idx(channel)] = false;
		writeAttenuation(channel);
	}

	void setVoice(int channel, int voice) override
	{
		if (channel == NOISE_CHANNEL)
		{
			m_periodicNoise = (voice == 1);
		}
	}

	void setVolume(int channel, int volume) override
	{
		if (!valid(channel))
		{
			return;
		}
		m_volume[idx(channel)] = std::clamp(volume, 0, 15);
		if (m_sounding[idx(channel)])
		{
			writeAttenuation(channel);
		}
	}

	/// @brief Game Gear 式のステレオ指定で、声ごとに左右へ振る
	void setPan(int channel, int pan) override
	{
		if (!valid(channel))
		{
			return;
		}
		const auto left = static_cast<std::uint8_t>(1u << (4 + channel));
		const auto right = static_cast<std::uint8_t>(1u << channel);
		m_stereo = static_cast<std::uint8_t>(m_stereo | left | right);
		if (pan == 0)
		{
			m_stereo = static_cast<std::uint8_t>(m_stereo & ~right);
		}
		else if (pan == 2)
		{
			m_stereo = static_cast<std::uint8_t>(m_stereo & ~left);
		}
		m_device.write(1, 0, m_stereo);
	}

	void setPitch(int channel, int key) override
	{
		if (channel >= 0 && channel < TONE_CHANNELS)
		{
			writePeriod(channel, keyToPeriod(key));
		}
	}

	/// @brief MIDI のノート番号から、いちばん近い周期を選ぶ (周波数 = クロック / (32 * 周期))
	[[nodiscard]] static int keyToPeriod(int key) noexcept
	{
		const double freq = 440.0 * std::pow(2.0, (static_cast<double>(key) - 69.0) / 12.0);
		const long period = std::lround(static_cast<double>(Sn76489Device::EMULATOR_CLOCK) / (32.0 * freq));
		return static_cast<int>(std::clamp(period, 1L, static_cast<long>(MAX_PERIOD)));
	}

	/// @brief 周期が作る周波数 (Hz)
	[[nodiscard]] static double periodToHz(int period) noexcept
	{
		return static_cast<double>(Sn76489Device::EMULATOR_CLOCK) / (32.0 * static_cast<double>(period));
	}

private:
	[[nodiscard]] static bool valid(int channel) noexcept
	{
		return channel >= 0 && channel < CHANNELS;
	}

	[[nodiscard]] static std::size_t idx(int channel) noexcept
	{
		return static_cast<std::size_t>(channel);
	}

	void initState()
	{
		m_volume.fill(DEFAULT_VOLUME);
		m_sounding.fill(false);
		m_periodicNoise = false;
		m_stereo = 0xFF;
	}

	void writePeriod(int channel, int period)
	{
		const auto latch = static_cast<std::uint32_t>(0x80 | (channel << 5) | (period & 0x0F));
		m_device.write(0, 0, latch);
		m_device.write(0, 0, static_cast<std::uint32_t>(period >> 4) & 0x3Fu);
	}

	/// @brief ノイズの種類と速さ (高い音は速く、低い音は遅く)
	void writeNoiseControl(int key)
	{
		const int rate = (key >= 72) ? 0 : (key >= 60 ? 1 : 2);
		m_device.write(0, 0, 0xE0u | (m_periodicNoise ? 0u : 4u) | static_cast<std::uint32_t>(rate));
	}

	/// @brief 減衰量のレジスタ。0 が最大、15 が無音で、1 段が 2 dB
	void writeAttenuation(int channel)
	{
		const int attenuation = m_sounding[idx(channel)] ? 15 - m_volume[idx(channel)] : 15;
		m_device.write(0, 0, static_cast<std::uint32_t>(0x90 | (channel << 5) | attenuation));
	}

	static constexpr int DEFAULT_VOLUME = 12; ///< MML の V の既定値

	Sn76489Device m_device;
	std::array<int, CHANNELS> m_volume{};
	std::array<bool, CHANNELS> m_sounding{};
	bool m_periodicNoise = false;
	std::uint8_t m_stereo = 0xFF;
};

} // namespace mitiru_mml
