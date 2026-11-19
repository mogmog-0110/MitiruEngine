#pragma once
/// @file NesApuDriver.hpp
/// @brief NES (Famicom) の APU の Driver (層 2): 音符を周期と音量のレジスタ書き込みに変える
/// @details チャンネル 0 と 1 が矩形波、2 が三角波、3 がノイズ。
///          音色 (setVoice): 矩形波は 0 = デューティ 50%、1 = 25%、2 = 12.5%、3 = 75% (25% の反転)。
///          ノイズは 0 = 長周期 (白色)、1 = 短周期 (金属的)。三角波に音色は無い。
///          音量は 0〜15 で、三角波だけは音量を変えられないので 0 で止まり、1 以上は同じ大きさで鳴る。
///          音程は 11bit の周期の整数で決まるので、高い音ほど 12 平均律からずれる (keyToTimer と timerToHz)。

#include <mitiru_mml/ChipDriver.hpp>
#include <mitiru_mml/NesApuDevice.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace mitiru_mml
{

/// @brief NES APU ドライバー
class NesApuDriver final : public ChipDriver
{
public:
	static constexpr int PULSE_1 = 0;
	static constexpr int PULSE_2 = 1;
	static constexpr int TRIANGLE = 2;
	static constexpr int NOISE = 3;
	static constexpr int CHANNELS = 4;
	/// @brief タイマー値の上限 (11bit)
	static constexpr int MAX_TIMER = 2047;

	NesApuDriver()
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
			caps.pitchSlide = (channel != NOISE);
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
		m_sounding[idx(channel)] = true;
		switch (channel)
		{
		case PULSE_1:
		case PULSE_2:
			writePulseTimer(channel, keyToTimer(key, false));
			writePulseControl(channel);
			break;
		case TRIANGLE:
			writeTriangleTimer(keyToTimer(key, true));
			writeTriangleControl();
			break;
		default:
			writeNoiseMode(key);
			writeNoiseControl();
			break;
		}
	}

	void noteOff(int channel) override
	{
		if (!valid(channel))
		{
			return;
		}
		m_sounding[idx(channel)] = false;
		writeControl(channel);
	}

	void setVoice(int channel, int voice) override
	{
		if (!valid(channel))
		{
			return;
		}
		m_voice[idx(channel)] = voice;
		if (m_sounding[idx(channel)])
		{
			writeControl(channel);
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
			writeControl(channel);
		}
	}

	/// @brief NES は左右の定位を持たないので何もしない
	void setPan(int channel, int pan) override
	{
		static_cast<void>(channel);
		static_cast<void>(pan);
	}

	void setPitch(int channel, int key) override
	{
		if (channel == PULSE_1 || channel == PULSE_2)
		{
			writePulseTimer(channel, keyToTimer(key, false));
		}
		else if (channel == TRIANGLE)
		{
			writeTriangleTimer(keyToTimer(key, true));
		}
	}

	/// @brief MIDI のノート番号から、いちばん近いタイマー値を選ぶ
	/// @details 矩形波の周波数 = クロック / (16 * (タイマー + 1))、三角波は クロック / (32 * (タイマー + 1))。
	///          矩形波はタイマーが 8 未満だとチップ自身が消音するので 8 以上にする。
	[[nodiscard]] static int keyToTimer(int key, bool triangle) noexcept
	{
		const double freq = 440.0 * std::pow(2.0, (static_cast<double>(key) - 69.0) / 12.0);
		const double divisor = triangle ? 32.0 : 16.0;
		const long timer = std::lround(static_cast<double>(NesApuDevice::CLOCK) / (divisor * freq)) - 1;
		return static_cast<int>(std::clamp(timer, triangle ? 0L : 8L, static_cast<long>(MAX_TIMER)));
	}

	/// @brief タイマー値が作る周波数 (Hz)
	[[nodiscard]] static double timerToHz(int timer, bool triangle) noexcept
	{
		const double divisor = triangle ? 32.0 : 16.0;
		return static_cast<double>(NesApuDevice::CLOCK) / (divisor * (static_cast<double>(timer) + 1.0));
	}

private:
	/// @brief 長さカウンターの読み込み値の番号。1 は 254 で、停止フラグが立っているので減らない
	static constexpr std::uint32_t LENGTH_INDEX = 1;
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
		m_voice.fill(0);
		m_sounding.fill(false);
		m_device.write(0, 0x15, 0x0F); // 長さカウンターは有効にしてから読み込む。DPCM は使わない
		m_device.write(0, 0x17, 0x40); // フレームカウンターの割り込みを使わない
		m_device.write(0, 0x01, 0x08); // 矩形波のスイープを無効にする (ネゲートで周期の消音も外れる)
		m_device.write(0, 0x05, 0x08);
		for (int channel = 0; channel < CHANNELS; ++channel)
		{
			writeControl(channel);
		}
	}

	/// @brief 矩形波のデューティ比の値 (レジスタの bit7-6)
	[[nodiscard]] static std::uint32_t dutyBits(int voice) noexcept
	{
		static constexpr std::array<std::uint32_t, 4> BITS = {2, 1, 0, 3};
		return BITS[static_cast<std::size_t>(voice & 3)];
	}

	void writeControl(int channel)
	{
		switch (channel)
		{
		case PULSE_1:
		case PULSE_2: writePulseControl(channel); break;
		case TRIANGLE: writeTriangleControl(); break;
		default: writeNoiseControl(); break;
		}
	}

	[[nodiscard]] std::uint32_t level(int channel) const noexcept
	{
		return m_sounding[idx(channel)] ? static_cast<std::uint32_t>(m_volume[idx(channel)]) : 0u;
	}

	/// @brief 停止フラグ (bit5) と固定音量 (bit4) を立て、音量をレジスタに入れる
	void writePulseControl(int channel)
	{
		m_device.write(0, static_cast<std::uint32_t>(channel * 4),
			(dutyBits(m_voice[idx(channel)]) << 6) | 0x30u | level(channel));
	}

	void writePulseTimer(int channel, int timer)
	{
		const auto base = static_cast<std::uint32_t>(channel * 4);
		m_device.write(0, base + 2, static_cast<std::uint32_t>(timer) & 0xFFu);
		m_device.write(0, base + 3, (LENGTH_INDEX << 3) | (static_cast<std::uint32_t>(timer) >> 8));
	}

	/// @brief 三角波は線形カウンターの値 (127) で鳴らし、0 にして止める。制御ビット (bit7) で長さカウンターを止める
	void writeTriangleControl()
	{
		m_device.write(0, 0x08, 0x80u | (level(TRIANGLE) > 0 ? 0x7Fu : 0u));
	}

	void writeTriangleTimer(int timer)
	{
		m_device.write(0, 0x0A, static_cast<std::uint32_t>(timer) & 0xFFu);
		m_device.write(0, 0x0B, (LENGTH_INDEX << 3) | (static_cast<std::uint32_t>(timer) >> 8));
	}

	void writeNoiseControl()
	{
		m_device.write(0, 0x0C, 0x30u | level(NOISE));
		m_device.write(0, 0x0F, LENGTH_INDEX << 3);
	}

	/// @brief ノイズの速さ (高い音は速く、低い音は遅い) と、短周期か長周期か
	void writeNoiseMode(int key)
	{
		const int period = std::clamp(15 - (key - 24) / 4, 0, 15);
		const std::uint32_t shortMode = (m_voice[idx(NOISE)] == 1) ? 0x80u : 0u;
		m_device.write(0, 0x0E, shortMode | static_cast<std::uint32_t>(period));
	}

	NesApuDevice m_device;
	std::array<int, CHANNELS> m_volume{};
	std::array<int, CHANNELS> m_voice{};
	std::array<bool, CHANNELS> m_sounding{};
};

} // namespace mitiru_mml
