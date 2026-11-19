#pragma once
/// @file OplDriver.hpp
/// @brief YM3526 (OPL)、YM3812 (OPL2)、YMF262 (OPL3) の Driver (層 2)
/// @details 2 オペレーター FM。OPL と OPL2 は 9ch、OPL3 は 18ch (4 オペレーター結合とリズム音源は使わない)。
///          音程は 10bit の F-Number と 3bit のブロックで決まるので、半音の中には、F-Number の丸めのために
///          1.7 セントほどずれる音がある (keyToFnumBlock が選んだ値の誤差は pitchErrorBoundCents で分かる)。

#include <mitiru_mml/ChipDriver.hpp>
#include <mitiru_mml/OplDevice.hpp>
#include <mitiru_mml/OplPresets.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace mitiru_mml
{

/// @brief OPL 系の Driver
class OplDriver final : public ChipDriver
{
public:
	/// @brief 音量を MML の 1 段 (3 dB) ずつ変えるための、TL の段数 (TL 1 段が 0.75 dB)
	static constexpr int TL_STEPS_PER_VOLUME = 4;
	/// @brief F-Number の基準レート (クロック / 72。OPL3 は クロック / 288 で同じ値)
	static constexpr double FNUM_RATE = 3579545.0 / 72.0;

	/// @brief F-Number とブロック
	struct FnumBlock
	{
		std::uint16_t fnum; ///< 0-1023
		std::uint8_t block; ///< 0-7
	};

	explicit OplDriver(OplVariant variant = OplVariant::Opl2)
		: m_device(variant)
	{
		initState();
	}

	[[nodiscard]] OplVariant variant() const noexcept
	{
		return m_device.variant();
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
		return m_device.variant() == OplVariant::Opl3 ? 18 : 9;
	}

	[[nodiscard]] ChannelCaps channelCaps(int channel) const noexcept override
	{
		ChannelCaps caps;
		if (channel >= 0 && channel < channelCount())
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
		const std::size_t ch = idx(channel);
		if (m_keyOn[ch])
		{
			m_keyOn[ch] = false;
			writeFrequency(channel);
		}
		m_key[ch] = key;
		m_held[ch] = true;
		m_keyOn[ch] = (m_volume[ch] > 0);
		writeFrequency(channel);
	}

	void noteOff(int channel) override
	{
		if (!valid(channel))
		{
			return;
		}
		m_held[idx(channel)] = false;
		m_keyOn[idx(channel)] = false;
		writeFrequency(channel);
	}

	void setVoice(int channel, int voice) override
	{
		if (valid(channel))
		{
			setFmVoice(channel, opl_presets::getPreset(voice));
		}
	}

	void setVolume(int channel, int volume) override
	{
		if (!valid(channel))
		{
			return;
		}
		const std::size_t ch = idx(channel);
		m_volume[ch] = std::clamp(volume, 0, 15);
		// 音量 0 の間はキーオフにし、鳴らしている最中に音量を戻したらキーオンし直す
		const bool wantKeyOn = m_held[ch] && m_volume[ch] > 0;
		writeLevels(channel);
		if (wantKeyOn != m_keyOn[ch])
		{
			m_keyOn[ch] = wantKeyOn;
			writeFrequency(channel);
		}
	}

	void setPan(int channel, int pan) override
	{
		if (!valid(channel) || m_device.variant() != OplVariant::Opl3)
		{
			return;
		}
		m_outputBits[idx(channel)] = (pan == 0) ? 0x10 : (pan == 2 ? 0x20 : 0x30);
		writeConnection(channel);
	}

	void setPitch(int channel, int key) override
	{
		if (valid(channel))
		{
			m_key[idx(channel)] = key;
			writeFrequency(channel);
		}
	}

	/// @brief MIDI のノート番号から F-Number とブロックを選ぶ
	/// @details F-Number が 512 以上 1023 以下になる最小のブロックを使う (丸めの相対誤差が最小)。
	///          48.5 Hz 未満と 6.2 kHz 超は範囲の端で止まる。
	[[nodiscard]] static FnumBlock keyToFnumBlock(int key) noexcept
	{
		const double freq = 440.0 * std::pow(2.0, (static_cast<double>(key) - 69.0) / 12.0);
		for (int block = 0; block <= 7; ++block)
		{
			const double fnum = freq * static_cast<double>(1 << (20 - block)) / FNUM_RATE;
			const long rounded = std::lround(fnum);
			if (rounded <= 1023 || block == 7)
			{
				return {static_cast<std::uint16_t>(std::clamp(rounded, 1L, 1023L)), static_cast<std::uint8_t>(block)};
			}
		}
		return {1023, 7};
	}

	/// @brief F-Number とブロックが作る周波数 (Hz)
	[[nodiscard]] static double fnumBlockToHz(FnumBlock fb) noexcept
	{
		return static_cast<double>(fb.fnum) * FNUM_RATE / static_cast<double>(1 << (20 - fb.block));
	}

	/// @brief F-Number の丸めが作る、音程の誤差の上限 (セント)。1200 * log2(1 + 0.5 / fnum)
	[[nodiscard]] static double pitchErrorBoundCents(FnumBlock fb) noexcept
	{
		return 1200.0 * std::log2(1.0 + 0.5 / static_cast<double>(fb.fnum));
	}

	/// @brief FM ボイスを設定する
	void setFmVoice(int channel, const OplVoice& voice)
	{
		const std::size_t ch = idx(channel);
		m_voice[ch] = voice;
		m_haveVoice[ch] = true;
		writeConnection(channel);
		writeOperator(channel, 0, voice.modulator);
		writeOperator(channel, 3, voice.carrier);
		writeLevels(channel);
	}

private:
	/// @brief バンク内のチャンネル 0〜8 の、変調器のオペレーターのオフセット (搬送波は +3)
	static constexpr std::array<std::uint32_t, 9> OPERATOR_OFFSET = {0, 1, 2, 8, 9, 10, 16, 17, 18};

	[[nodiscard]] bool valid(int channel) const noexcept
	{
		return channel >= 0 && channel < channelCount();
	}

	[[nodiscard]] static std::size_t idx(int channel) noexcept
	{
		return static_cast<std::size_t>(channel);
	}

	/// @brief OPL3 の後半 (チャンネル 9 以降) は port 1 に書く
	void write(int channel, std::uint32_t reg, std::uint32_t value)
	{
		m_device.write(channel >= 9 ? 1u : 0u, reg, value);
	}

	[[nodiscard]] static std::uint32_t operatorOffset(int channel, int carrierOffset) noexcept
	{
		return OPERATOR_OFFSET[static_cast<std::size_t>(channel % 9)] + static_cast<std::uint32_t>(carrierOffset);
	}

	void initState()
	{
		m_volume.fill(DEFAULT_VOLUME);
		m_outputBits.fill(0x30);
		m_keyOn.fill(false);
		m_held.fill(false);
		m_key.fill(60);
		m_haveVoice.fill(false);
		m_voice.fill(OplVoice{});
		switch (m_device.variant())
		{
		case OplVariant::Opl:
			break;
		case OplVariant::Opl2:
			m_device.write(0, 0x01, 0x20); // 波形選択を有効にする
			break;
		case OplVariant::Opl3:
			m_device.write(1, 0x05, 0x01); // OPL3 のモード (ステレオ出力と波形 8 種)
			m_device.write(1, 0x04, 0x00); // 4 オペレーター結合は使わない
			break;
		}
		m_device.write(0, 0xBD, 0x00);
	}

	void writeOperator(int channel, int carrierOffset, const OplOpParams& p)
	{
		const std::uint32_t off = operatorOffset(channel, carrierOffset);
		write(channel, 0x20 + off, (p.tremolo ? 0x80u : 0u) | (p.vibrato ? 0x40u : 0u)
			| (p.sustain ? 0x20u : 0u) | (p.keyScaleRate ? 0x10u : 0u) | (p.multiple & 15u));
		write(channel, 0x60 + off, (static_cast<std::uint32_t>(p.attackRate & 15) << 4) | (p.decayRate & 15u));
		write(channel, 0x80 + off, (static_cast<std::uint32_t>(p.sustainLevel & 15) << 4) | (p.releaseRate & 15u));
		if (m_device.variant() != OplVariant::Opl)
		{
			write(channel, 0xE0 + off, p.waveform & 7u);
		}
	}

	/// @brief フィードバックと接続 (と OPL3 の左右)
	void writeConnection(int channel)
	{
		const std::size_t ch = idx(channel);
		const OplVoice& v = m_voice[ch];
		std::uint32_t value = (static_cast<std::uint32_t>(v.feedback & 7) << 1) | (v.additive ? 1u : 0u);
		if (m_device.variant() == OplVariant::Opl3)
		{
			value |= m_outputBits[ch];
		}
		write(channel, 0xC0 + static_cast<std::uint32_t>(channel % 9), value);
	}

	/// @brief 搬送波 (additive なら変調器も) の TL に、現在の音量の減衰を足して書く
	void writeLevels(int channel)
	{
		const std::size_t ch = idx(channel);
		if (!m_haveVoice[ch])
		{
			return;
		}
		const OplVoice& v = m_voice[ch];
		const int attenuation = (15 - m_volume[ch]) * TL_STEPS_PER_VOLUME;
		const auto level = [&](const OplOpParams& p, bool attenuate)
		{
			const int tl = std::min(63, p.totalLevel + (attenuate ? attenuation : 0));
			return (static_cast<std::uint32_t>(p.keyScaleLevel & 3) << 6) | static_cast<std::uint32_t>(tl);
		};
		write(channel, 0x40 + operatorOffset(channel, 0), level(v.modulator, v.additive));
		write(channel, 0x40 + operatorOffset(channel, 3), level(v.carrier, true));
	}

	void writeFrequency(int channel)
	{
		const std::size_t ch = idx(channel);
		const FnumBlock fb = keyToFnumBlock(m_key[ch]);
		const auto c = static_cast<std::uint32_t>(channel % 9);
		write(channel, 0xA0 + c, fb.fnum & 0xFFu);
		write(channel, 0xB0 + c, (m_keyOn[ch] ? 0x20u : 0u)
			| (static_cast<std::uint32_t>(fb.block) << 2) | (fb.fnum >> 8));
	}

	static constexpr int DEFAULT_VOLUME = 12; ///< MML の V の既定値
	static constexpr int MAX_CHANNELS = 18;

	OplDevice m_device;
	std::array<int, MAX_CHANNELS> m_volume{};
	std::array<std::uint32_t, MAX_CHANNELS> m_outputBits{};
	std::array<bool, MAX_CHANNELS> m_keyOn{};
	std::array<bool, MAX_CHANNELS> m_held{}; ///< noteOn されてから noteOff されるまで true。音量 0 で m_keyOn が false でも保つ
	std::array<int, MAX_CHANNELS> m_key{};
	std::array<bool, MAX_CHANNELS> m_haveVoice{};
	std::array<OplVoice, MAX_CHANNELS> m_voice{};
};

} // namespace mitiru_mml
