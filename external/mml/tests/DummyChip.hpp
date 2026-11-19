#pragma once
/// @file DummyChip.hpp
/// @brief チップの口 (ChipDevice と ChipDriver) が使えることを示す、正弦波 1 声のダミーチップ
/// @details テストの中だけで使う。新しいチップを足す手順の最小の例でもある。
///          レジスタ: 0 = 周波数の増分 (下位 8bit)、1 = 同 (上位 8bit)、2 = 振幅 (0〜255)、3 = ゲート (0 か 1)。

#include <mitiru_mml/ChipDriver.hpp>

#include <cmath>
#include <cstdint>
#include <memory>

namespace mitiru_mml_test
{

/// @brief 正弦波 1 声の Device
class DummySineDevice final : public mitiru_mml::ChipDevice
{
public:
	explicit DummySineDevice(std::uint32_t rate) : m_rate(rate) {}

	[[nodiscard]] std::string_view name() const noexcept override { return "DummySine"; }
	[[nodiscard]] std::uint32_t nativeRate() const noexcept override { return m_rate; }

	void reset() override
	{
		m_increment = 0;
		m_amplitude = 0;
		m_gate = false;
		m_phase = 0;
	}

	void write(std::uint32_t port, std::uint32_t reg, std::uint32_t value) override
	{
		static_cast<void>(port);
		switch (reg)
		{
		case 0: m_increment = static_cast<std::uint16_t>((m_increment & 0xFF00u) | (value & 0xFFu)); break;
		case 1: m_increment = static_cast<std::uint16_t>((m_increment & 0x00FFu) | ((value & 0xFFu) << 8)); break;
		case 2: m_amplitude = static_cast<std::uint8_t>(value); break;
		case 3: m_gate = (value != 0); break;
		default: break;
		}
	}

	void generate(float* out, std::uint32_t frames) override
	{
		for (std::uint32_t i = 0; i < frames; ++i)
		{
			float s = 0.0f;
			if (m_gate)
			{
				const double angle = 2.0 * 3.14159265358979323846 * static_cast<double>(m_phase) / 65536.0;
				s = static_cast<float>(std::sin(angle)) * (static_cast<float>(m_amplitude) / 255.0f) * 0.5f;
				m_phase = static_cast<std::uint16_t>(m_phase + m_increment);
			}
			out[i * 2] = s;
			out[i * 2 + 1] = s;
		}
	}

	[[nodiscard]] std::vector<std::uint8_t> saveState() override
	{
		return {static_cast<std::uint8_t>(m_increment & 0xFF), static_cast<std::uint8_t>(m_increment >> 8),
			m_amplitude, static_cast<std::uint8_t>(m_gate ? 1 : 0),
			static_cast<std::uint8_t>(m_phase & 0xFF), static_cast<std::uint8_t>(m_phase >> 8)};
	}

	bool loadState(const std::vector<std::uint8_t>& state) override
	{
		if (state.size() != 6)
		{
			return false;
		}
		m_increment = static_cast<std::uint16_t>(state[0] | (state[1] << 8));
		m_amplitude = state[2];
		m_gate = (state[3] != 0);
		m_phase = static_cast<std::uint16_t>(state[4] | (state[5] << 8));
		return true;
	}

private:
	std::uint32_t m_rate;
	std::uint16_t m_increment = 0;
	std::uint8_t m_amplitude = 0;
	bool m_gate = false;
	std::uint16_t m_phase = 0;
};

/// @brief ダミーチップの Driver。チャンネル 0 だけがある
class DummySineDriver final : public mitiru_mml::ChipDriver
{
public:
	explicit DummySineDriver(std::uint32_t rate) : m_device(rate) {}

	[[nodiscard]] mitiru_mml::ChipDevice& device() noexcept override { return m_device; }
	void reset() override { m_device.reset(); }
	[[nodiscard]] int channelCount() const noexcept override { return 1; }

	[[nodiscard]] mitiru_mml::ChannelCaps channelCaps(int channel) const noexcept override
	{
		mitiru_mml::ChannelCaps caps;
		caps.valid = (channel == 0);
		caps.pitchSlide = true;
		return caps;
	}

	void noteOn(int channel, int key, int velocity) override
	{
		if (channel != 0) return;
		setPitch(channel, key);
		m_device.write(0, 2, static_cast<std::uint32_t>(velocity * 255 / 15));
		m_device.write(0, 3, 1);
	}

	void noteOff(int channel) override
	{
		if (channel == 0) m_device.write(0, 3, 0);
	}

	void setVoice(int channel, int voice) override
	{
		static_cast<void>(channel);
		static_cast<void>(voice);
	}

	void setVolume(int channel, int volume) override
	{
		if (channel == 0) m_device.write(0, 2, static_cast<std::uint32_t>(volume * 255 / 15));
	}

	void setPan(int channel, int pan) override
	{
		static_cast<void>(channel);
		static_cast<void>(pan);
	}

	void setPitch(int channel, int key) override
	{
		if (channel != 0) return;
		const double freq = 440.0 * std::pow(2.0, (static_cast<double>(key) - 69.0) / 12.0);
		const auto inc = static_cast<std::uint32_t>(std::lround(freq * 65536.0 / m_device.nativeRate()));
		m_device.write(0, 0, inc & 0xFFu);
		m_device.write(0, 1, (inc >> 8) & 0xFFu);
	}

private:
	DummySineDevice m_device;
};

} // namespace mitiru_mml_test
