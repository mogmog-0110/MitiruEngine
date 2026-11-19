#pragma once
/// @file Ay8910Driver.hpp
/// @brief AY-3-8910 / YM2149 の Driver (層 2): 音符を周期と音量のレジスタ書き込みに変える
/// @details チャンネル 0〜2 が声 A〜C。音色は setVoice で、0 = 矩形波、1 = ノイズ、2 = 矩形波とノイズの重ね。
///          ノイズの周期はチップに 1 つしかないので、ノイズの声が複数あると最後に鳴らした音の値になる。
///          音程は 12bit の周期の整数で決まるので、高い音ほど 12 平均律からずれる (keyToPeriod と periodToHz)。

#include <mitiru_mml/Ay8910Device.hpp>
#include <mitiru_mml/ChipDriver.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace mitiru_mml
{

/// @brief AY-3-8910 ドライバー
class Ay8910Driver final : public ChipDriver
{
public:
	static constexpr int CHANNELS = 3;
	static constexpr int MAX_PERIOD = 4095;
	/// @brief setParam の id: ノイズの周期 (value = 1〜31)
	static constexpr int PARAM_NOISE_PERIOD = chip_param::FIRST_CHIP_SPECIFIC;

	Ay8910Driver()
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
			caps.pitchSlide = true;
			caps.hardwareEnvelope = true;
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
		const std::size_t ch = idx(channel);
		if (m_mode[ch] == MODE_NOISE)
		{
			writeNoisePeriod(std::clamp(31 - (key - 24) / 2, 1, 31));
		}
		else
		{
			writePeriod(channel, keyToPeriod(key));
		}
		if (m_envelope[ch])
		{
			m_device.write(0, 0x0D, m_envShape); // 書き込みでエンベロープが最初から始まる
		}
		m_sounding[ch] = true;
		writeVolume(channel);
	}

	void noteOff(int channel) override
	{
		if (!valid(channel))
		{
			return;
		}
		m_sounding[idx(channel)] = false;
		writeVolume(channel);
	}

	void setVoice(int channel, int voice) override
	{
		if (!valid(channel))
		{
			return;
		}
		m_mode[idx(channel)] = (voice == 1) ? MODE_NOISE : (voice == 2 ? MODE_BOTH : MODE_TONE);
		writeMixer();
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
			writeVolume(channel);
		}
	}

	void setPan(int channel, int pan) override
	{
		m_device.setChannelPan(channel, pan);
	}

	void setPitch(int channel, int key) override
	{
		if (valid(channel) && m_mode[idx(channel)] != MODE_NOISE)
		{
			writePeriod(channel, keyToPeriod(key));
		}
	}

	void setParam(int channel, int id, int value, int value2) override
	{
		if (!valid(channel))
		{
			return;
		}
		if (id == chip_param::ENVELOPE)
		{
			m_envelope[idx(channel)] = true;
			m_envShape = static_cast<std::uint32_t>(value & 0x0F);
			m_device.write(0, 0x0B, static_cast<std::uint32_t>(value2) & 0xFFu);
			m_device.write(0, 0x0C, (static_cast<std::uint32_t>(value2) >> 8) & 0xFFu);
		}
		else if (id == PARAM_NOISE_PERIOD)
		{
			writeNoisePeriod(std::clamp(value, 1, 31));
		}
	}

	/// @brief MIDI のノート番号から、いちばん近い周期を選ぶ (周波数 = クロック / (16 * 周期))
	[[nodiscard]] static int keyToPeriod(int key) noexcept
	{
		const double freq = 440.0 * std::pow(2.0, (static_cast<double>(key) - 69.0) / 12.0);
		const long period = std::lround(static_cast<double>(Ay8910Device::CLOCK) / (16.0 * freq));
		return static_cast<int>(std::clamp(period, 1L, static_cast<long>(MAX_PERIOD)));
	}

	/// @brief 周期が作る周波数 (Hz)
	[[nodiscard]] static double periodToHz(int period) noexcept
	{
		return static_cast<double>(Ay8910Device::CLOCK) / (16.0 * static_cast<double>(period));
	}

private:
	static constexpr int MODE_TONE = 0;
	static constexpr int MODE_NOISE = 1;
	static constexpr int MODE_BOTH = 2;
	static constexpr int DEFAULT_VOLUME = 12; ///< MML の V の既定値

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
		m_mode.fill(MODE_TONE);
		m_envelope.fill(false);
		m_envShape = 0;
		for (std::uint32_t reg = 8; reg <= 10; ++reg)
		{
			m_device.write(0, reg, 0);
		}
		writeNoisePeriod(15);
		writeMixer();
	}

	void writePeriod(int channel, int period)
	{
		m_device.write(0, static_cast<std::uint32_t>(channel * 2), static_cast<std::uint32_t>(period) & 0xFFu);
		m_device.write(0, static_cast<std::uint32_t>(channel * 2 + 1), static_cast<std::uint32_t>(period >> 8) & 0x0Fu);
	}

	void writeNoisePeriod(int period)
	{
		m_device.write(0, 6, static_cast<std::uint32_t>(period));
	}

	/// @brief ミキサーのレジスタ。ビットが 1 のとき、その声のトーンかノイズが止まる
	void writeMixer()
	{
		std::uint32_t value = 0;
		for (std::size_t c = 0; c < static_cast<std::size_t>(CHANNELS); ++c)
		{
			if (m_mode[c] == MODE_NOISE)
			{
				value |= 1u << c;
			}
			if (m_mode[c] == MODE_TONE)
			{
				value |= 8u << c;
			}
		}
		m_device.write(0, 7, value);
	}

	void writeVolume(int channel)
	{
		const std::size_t ch = idx(channel);
		std::uint32_t value = 0;
		if (m_sounding[ch])
		{
			value = m_envelope[ch] ? 0x10u : static_cast<std::uint32_t>(m_volume[ch]);
		}
		m_device.write(0, static_cast<std::uint32_t>(8 + channel), value);
	}

	Ay8910Device m_device;
	std::array<int, CHANNELS> m_volume{};
	std::array<bool, CHANNELS> m_sounding{};
	std::array<int, CHANNELS> m_mode{};
	std::array<bool, CHANNELS> m_envelope{};
	std::uint32_t m_envShape = 0;
};

} // namespace mitiru_mml
