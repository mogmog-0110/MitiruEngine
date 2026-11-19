#pragma once
/// @file OpmDriver.hpp
/// @brief YM2151 (OPM) の Driver (層 2): 音符や音色をレジスタ書き込みに変える
/// @details FM 8ch。チャンネル番号は 0〜7。音程はキーコード (KC) とキーフラクション (KF) で決まり、
///          OPM のオクターブは C# から始まる。レジスタの配置が OPN と違うので OpnaDriver とは別に作る。

#include <mitiru_mml/ChipDriver.hpp>
#include <mitiru_mml/OpmDevice.hpp>
#include <mitiru_mml/OpmPresets.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

namespace mitiru_mml
{

/// @brief YM2151 (OPM) ドライバー
class OpmDriver final : public ChipDriver
{
public:
	static constexpr int CHANNELS = 8;
	/// @brief 音量を MML の 1 段 (3 dB) ずつ変えるための、TL の段数 (TL 1 段が 0.75 dB)
	static constexpr int TL_STEPS_PER_VOLUME = 4;

	/// @brief キーコードとキーフラクション
	struct KeyCode
	{
		std::uint8_t kc; ///< オクターブ (bit6-4) と音名コード (bit3-0)
		std::uint8_t kf; ///< 半音を 64 分割した端数 (0-63)
	};

	OpmDriver()
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
		writeFrequency(channel, key);
		m_device.write(0, 0x08, 0x78u | static_cast<std::uint32_t>(channel));
	}

	void noteOff(int channel) override
	{
		if (valid(channel))
		{
			m_device.write(0, 0x08, static_cast<std::uint32_t>(channel));
		}
	}

	void setVoice(int channel, int voice) override
	{
		if (valid(channel))
		{
			setFmVoice(channel, opm_presets::getPreset(voice));
		}
	}

	void setVolume(int channel, int volume) override
	{
		if (!valid(channel))
		{
			return;
		}
		m_volume[idx(channel)] = std::clamp(volume, 0, 15);
		writeCarrierLevels(channel);
	}

	void setPan(int channel, int pan) override
	{
		if (!valid(channel))
		{
			return;
		}
		m_rl[idx(channel)] = (pan == 0) ? 0x40 : (pan == 2 ? 0x80 : 0xC0);
		writeConnection(channel);
	}

	void setPitch(int channel, int key) override
	{
		if (valid(channel))
		{
			writeFrequency(channel, key);
		}
	}

	/// @brief MIDI のノート番号をキーコードに変える
	/// @details OPM のオクターブは C# から B (と、その上の C) までなので、C#4 (key 61) がオクターブ 4 の先頭。
	///          音名コードは C# D D# E F F# G G# A A# B C の 12 個が 0,1,2,4,5,6,8,9,10,12,13,14 に当たる。
	[[nodiscard]] static KeyCode keyToKeyCode(int key) noexcept
	{
		static constexpr std::array<std::uint8_t, 12> NOTE_CODE = {0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
		const int clamped = std::clamp(key, 13, 108);
		const int semis = clamped - 61;
		const int octave = 4 + (semis >= 0 ? semis / 12 : -((11 - semis) / 12));
		const int note = semis - (octave - 4) * 12;
		return {static_cast<std::uint8_t>((octave << 4) | NOTE_CODE[static_cast<std::size_t>(note)]), 0};
	}

	/// @brief FM ボイス (音色) を設定する
	void setFmVoice(int channel, const OpmVoice& voice)
	{
		const std::size_t ch = idx(channel);
		m_voice[ch] = voice;
		m_haveVoice[ch] = true;
		writeConnection(channel);
		const auto c = static_cast<std::uint32_t>(channel);
		m_device.write(0, 0x38 + c, (static_cast<std::uint32_t>(voice.pmSensitivity & 7) << 4)
			| (voice.amSensitivity & 3u));

		// レジスタのスロット順 (M1、M2、C1、C2) は op1、op3、op2、op4 に当たる
		for (int op = 0; op < 4; ++op)
		{
			const OpmOpParams& p = voice.ops[op];
			const std::uint32_t slot = c + 8u * SLOT_OF_OP[static_cast<std::size_t>(op)];
			m_device.write(0, 0x40 + slot, (static_cast<std::uint32_t>(p.detune1 & 7) << 4) | (p.multiple & 15u));
			m_device.write(0, 0x80 + slot, (static_cast<std::uint32_t>(p.keyScale & 3) << 6) | (p.attackRate & 31u));
			m_device.write(0, 0xA0 + slot, (p.amEnable ? 0x80u : 0u) | (p.decay1Rate & 31u));
			m_device.write(0, 0xC0 + slot, (static_cast<std::uint32_t>(p.detune2 & 3) << 6) | (p.decay2Rate & 31u));
			m_device.write(0, 0xE0 + slot, (static_cast<std::uint32_t>(p.sustainLevel & 15) << 4) | (p.releaseRate & 15u));
		}
		writeCarrierLevels(channel);
	}

private:
	/// @brief op1〜op4 がレジスタ上で何番目のスロットか (M1=0、M2=1、C1=2、C2=3)
	static constexpr std::array<std::uint32_t, 4> SLOT_OF_OP = {0, 2, 1, 3};

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
		m_rl.fill(0xC0);
		m_haveVoice.fill(false);
		m_voice.fill(OpmVoice{});
	}

	/// @brief ある op がキャリア (出力に直接つながる) か
	[[nodiscard]] static bool isCarrier(int algorithm, int op) noexcept
	{
		switch (algorithm & 7)
		{
		case 0: case 1: case 2: case 3: return op == 3;
		case 4: return op == 1 || op == 3;
		case 5: case 6: return op >= 1;
		default: return true;
		}
	}

	/// @brief 左右 (RL)、フィードバック (FL)、アルゴリズム (CON) のレジスタ
	void writeConnection(int channel)
	{
		const std::size_t ch = idx(channel);
		const OpmVoice& v = m_voice[ch];
		m_device.write(0, 0x20 + static_cast<std::uint32_t>(channel), m_rl[ch]
			| (static_cast<std::uint32_t>(v.feedback & 7) << 3) | (v.algorithm & 7u));
	}

	/// @brief すべてのキャリアの TL に、音色の値と現在の音量の減衰を足して書く
	void writeCarrierLevels(int channel)
	{
		const std::size_t ch = idx(channel);
		if (!m_haveVoice[ch])
		{
			return;
		}
		const OpmVoice& v = m_voice[ch];
		const int volume = m_volume[ch];
		const int attenuation = (volume == 0) ? 127 : (15 - volume) * TL_STEPS_PER_VOLUME;
		for (int op = 0; op < 4; ++op)
		{
			int tl = v.ops[op].totalLevel;
			if (isCarrier(v.algorithm, op))
			{
				tl = std::min(127, tl + attenuation);
			}
			m_device.write(0, 0x60 + static_cast<std::uint32_t>(channel)
				+ 8u * SLOT_OF_OP[static_cast<std::size_t>(op)], static_cast<std::uint32_t>(tl));
		}
	}

	void writeFrequency(int channel, int key)
	{
		const KeyCode code = keyToKeyCode(key);
		const auto c = static_cast<std::uint32_t>(channel);
		m_device.write(0, 0x28 + c, code.kc);
		m_device.write(0, 0x30 + c, static_cast<std::uint32_t>(code.kf) << 2);
	}

	static constexpr int DEFAULT_VOLUME = 12; ///< MML の V の既定値

	OpmDevice m_device;
	std::array<int, CHANNELS> m_volume{};
	std::array<std::uint32_t, CHANNELS> m_rl{};
	std::array<bool, CHANNELS> m_haveVoice{};
	std::array<OpmVoice, CHANNELS> m_voice{};
};

} // namespace mitiru_mml
