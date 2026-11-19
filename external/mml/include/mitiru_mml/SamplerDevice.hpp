#pragma once
/// @file SamplerDevice.hpp
/// @brief SF2 (SoundFont) のサンプルを、ストリーミングで鳴らす Device
/// @details 既存の MultiSampleInstrument::renderNote は 1 音を丸ごと先に描く作りで、発音の長さが
///          先に分からないと使えない。Device は発音と消音を別々のレジスタ書き込みで受けるので、
///          ゾーンのサンプル、ループ、エンベロープを読み出して、1 ブロックずつ鳴らす。
///          ネイティブレートは出力レートと同じ (レート変換は要らない)。
///
/// レジスタ (port = チャンネル 0〜15)
///   0x00 発音: value = 鍵 | (ベロシティ 0〜127 << 8)
///   0x01 消音: パーカッションのチャンネルは無視する (サンプルを最後まで鳴らす)
///   0x02 プログラム選択: value = プログラム | (バンク << 8)
///   0x03 音量 0〜127、0x04 定位 0〜127 (64 が中央)、0x05 音程 (セント。符号つき 32bit)
///   0x06 リリースの長さ (ミリ秒。0 なら SF2 の値)

#include <mitiru_mml/ChipDevice.hpp>
#include <mitiru_mml/ModernDsp.hpp>
#include <mitiru_mml/MultiSampleInstrument.hpp>
#include <mitiru_mml/Sf2File.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace mitiru_mml
{

/// @brief SF2 サンプラーの Device
class SamplerDevice final : public ChipDevice
{
public:
	static constexpr int CHANNELS = 16;
	static constexpr int PERCUSSION_CHANNEL = 9;   ///< GM と同じ。バンク 128 を使う
	static constexpr int PERCUSSION_BANK = 128;
	static constexpr int VOICES_PER_CHANNEL = 12;
	static constexpr int MAX_ZONES_PER_NOTE = 4;

	static constexpr std::uint32_t REG_NOTE_ON = 0x00;
	static constexpr std::uint32_t REG_NOTE_OFF = 0x01;
	static constexpr std::uint32_t REG_PROGRAM = 0x02;
	static constexpr std::uint32_t REG_VOLUME = 0x03;
	static constexpr std::uint32_t REG_PAN = 0x04;
	static constexpr std::uint32_t REG_TUNE = 0x05;
	static constexpr std::uint32_t REG_RELEASE = 0x06;

	explicit SamplerDevice(std::uint32_t outputRate = 44100)
		: m_rate(outputRate)
	{
		reset();
	}

	/// @brief 使う SoundFont を渡す
	void setSoundFont(std::shared_ptr<const Sf2File> soundFont)
	{
		m_soundFont = std::move(soundFont);
		m_cache.clear();
		reset();
	}

	/// @brief チャンネルに楽器を直接渡す (SFZ や、既存の MultiSampleInstrument をこの時間軸で鳴らすのに使う)
	/// @details reset() の後も、この楽器に戻る。プログラム選択 (REG_PROGRAM) を書くと、SF2 の楽器に戻る
	void setInstrument(int channel, std::shared_ptr<const MultiSampleInstrument> instrument)
	{
		if (channel < 0 || channel >= CHANNELS)
		{
			return;
		}
		m_overrides[static_cast<std::size_t>(channel)] = std::move(instrument);
		m_channels[static_cast<std::size_t>(channel)].instrument = m_overrides[static_cast<std::size_t>(channel)];
	}

	[[nodiscard]] std::string_view name() const noexcept override
	{
		return "SF2 sampler";
	}

	[[nodiscard]] std::uint32_t nativeRate() const noexcept override
	{
		return m_rate;
	}

	void reset() override
	{
		for (int c = 0; c < CHANNELS; ++c)
		{
			Channel fresh;
			const auto& forced = m_overrides[static_cast<std::size_t>(c)];
			fresh.instrument = forced ? forced : lookup(c == PERCUSSION_CHANNEL ? PERCUSSION_BANK : 0, 0);
			m_channels[static_cast<std::size_t>(c)] = std::move(fresh);
		}
		m_age = 0;
	}

	void write(std::uint32_t port, std::uint32_t reg, std::uint32_t value) override
	{
		if (port >= static_cast<std::uint32_t>(CHANNELS))
		{
			return;
		}
		const int channel = static_cast<int>(port);
		Channel& ch = m_channels[port];
		switch (reg)
		{
		case REG_NOTE_ON:
			noteOn(channel, static_cast<int>(value & 0xFF), static_cast<int>((value >> 8) & 0x7F));
			break;
		case REG_NOTE_OFF:
			noteOff(channel);
			break;
		case REG_PROGRAM:
		{
			const int bank = (channel == PERCUSSION_CHANNEL) ? PERCUSSION_BANK : static_cast<int>((value >> 8) & 0xFF);
			m_overrides[port].reset();
			ch.instrument = lookup(bank, static_cast<int>(value & 0x7F));
			break;
		}
		case REG_VOLUME:
			ch.volume.target = static_cast<float>(std::min<std::uint32_t>(value, 127));
			break;
		case REG_PAN:
			ch.pan.target = static_cast<float>(std::min<std::uint32_t>(value, 127));
			break;
		case REG_TUNE:
			ch.tuneCents = static_cast<int>(static_cast<std::int32_t>(value));
			break;
		case REG_RELEASE:
			ch.releaseMs = static_cast<int>(value);
			break;
		default:
			break;
		}
	}

	void generate(float* out, std::uint32_t frames) override
	{
		std::fill(out, out + static_cast<std::size_t>(frames) * 2, 0.0f);
		std::uint32_t done = 0;
		while (done < frames)
		{
			const int len = static_cast<int>(std::min<std::uint32_t>(modern_dsp::CONTROL_BLOCK, frames - done));
			const float coef = modern_dsp::smoothCoef(len, 0.004, static_cast<double>(m_rate));
			for (auto& ch : m_channels)
			{
				ch.volume.step(coef);
				ch.pan.step(coef);
				float gl = 0.0f;
				float gr = 0.0f;
				modern_dsp::panGains(ch.pan.current, gl, gr);
				const float gain = modern_dsp::volumeCurve127(static_cast<int>(std::lround(ch.volume.current))) * OUTPUT_SCALE;
				for (auto& v : ch.voices)
				{
					if (v.active)
					{
						renderVoice(v, out + static_cast<std::size_t>(done) * 2, len, gain * gl, gain * gr);
					}
				}
			}
			done += static_cast<std::uint32_t>(len);
		}
	}

	/// @brief 鳴っているボイスの数 (テスト用)
	[[nodiscard]] int activeVoiceCount() const noexcept
	{
		int n = 0;
		for (const auto& ch : m_channels)
		{
			for (const auto& v : ch.voices)
			{
				n += v.active ? 1 : 0;
			}
		}
		return n;
	}

	/// @brief チャンネルの楽器が見つかっているか
	[[nodiscard]] bool hasInstrument(int channel) const noexcept
	{
		return channel >= 0 && channel < CHANNELS && m_channels[static_cast<std::size_t>(channel)].instrument != nullptr;
	}

private:
	static constexpr float OUTPUT_SCALE = 0.7f;

	struct Voice
	{
		bool active = false;
		bool releasing = false;
		const MultiSampleInstrument::Zone* zone = nullptr;
		std::shared_ptr<const MultiSampleInstrument> keep;
		double pos = 0.0;
		double step = 0.0;
		float gain = 1.0f;
		bool oneShot = false;  ///< パーカッションは消音を受けないので、ループがあっても最後で止める
		std::uint64_t age = 0;
		modern_dsp::Adsr env;
		float panL = 1.0f;
		float panR = 1.0f;
		bool filtered = false;
		modern_dsp::StateVariableFilter filter;
		std::uint32_t delayLeft = 0;   ///< 音が出るまでの残りのサンプル数
		std::uint32_t holdLeft = 0;    ///< 減衰を始めるまでの残りのサンプル数
		int exclusiveClass = 0;
	};

	struct Channel
	{
		std::shared_ptr<const MultiSampleInstrument> instrument;
		std::array<Voice, VOICES_PER_CHANNEL> voices{};
		modern_dsp::Smoothed volume{100.0f, 100.0f};
		modern_dsp::Smoothed pan{64.0f, 64.0f};
		int tuneCents = 0;
		int releaseMs = 0;
	};

	/// @brief バンクとプログラムから楽器を作る (結果は覚えておく)。無ければ近いプリセットで代用する
	std::shared_ptr<const MultiSampleInstrument> lookup(int bank, int program)
	{
		if (!m_soundFont || !m_soundFont->valid())
		{
			return nullptr;
		}
		int index = m_soundFont->findPreset(bank, program);
		if (index < 0 && bank != PERCUSSION_BANK)
		{
			for (int i = 0; i < m_soundFont->presetCount() && index < 0; ++i)
			{
				const auto info = m_soundFont->presetInfo(i);
				if (info.program == program && info.bank != PERCUSSION_BANK)
				{
					index = i;
				}
			}
			if (index < 0)
			{
				index = m_soundFont->findPreset(0, 0);
			}
		}
		if (index < 0 && bank == PERCUSSION_BANK)
		{
			index = m_soundFont->findPreset(PERCUSSION_BANK, 0);
		}
		if (index < 0)
		{
			return nullptr;
		}
		const auto it = m_cache.find(index);
		if (it != m_cache.end())
		{
			return it->second;
		}
		auto built = std::make_shared<const MultiSampleInstrument>(m_soundFont->buildInstrument(index, m_rate));
		m_cache[index] = built;
		return built;
	}

	void noteOn(int channel, int key, int velocity)
	{
		Channel& ch = m_channels[static_cast<std::size_t>(channel)];
		if (!ch.instrument)
		{
			return;
		}
		std::array<const MultiSampleInstrument::Zone*, MAX_ZONES_PER_NOTE> chosen{};
		int started = 0;
		for (const auto& zone : ch.instrument->zones())
		{
			if (started >= MAX_ZONES_PER_NOTE)
			{
				break;
			}
			if (key < zone.keyLo || key > zone.keyHi || velocity < zone.velLo || velocity > zone.velHi)
			{
				continue;
			}
			chosen[static_cast<std::size_t>(started++)] = &zone;
		}
		// 排他クラスの音を止めるのは、この発音を始める前の音だけ。ステレオのゾーンのように同じクラスが同時に選ばれても、お互いを止めない
		for (int i = 0; i < started; ++i)
		{
			if (chosen[static_cast<std::size_t>(i)]->exclusiveClass != 0)
			{
				chokeClass(ch, chosen[static_cast<std::size_t>(i)]->exclusiveClass);
			}
		}
		for (int i = 0; i < started; ++i)
		{
			startVoice(pickVoice(ch), ch, *chosen[static_cast<std::size_t>(i)], key);
		}
	}

	void noteOff(int channel)
	{
		if (channel == PERCUSSION_CHANNEL)
		{
			return;
		}
		for (auto& v : m_channels[static_cast<std::size_t>(channel)].voices)
		{
			if (v.active && !v.releasing)
			{
				v.releasing = true;
				v.env.release();
			}
		}
	}

	/// @brief 同じ排他クラスの音を、短いリリースで止める (開いたハイハットを閉じたハイハットが止める)
	void chokeClass(Channel& ch, int exclusiveClass) const
	{
		for (auto& v : ch.voices)
		{
			if (v.active && v.exclusiveClass == exclusiveClass)
			{
				v.releasing = true;
				v.env.releaseWithin(0.01, static_cast<double>(m_rate));
			}
		}
	}

	/// @brief 空いているボイス、無ければ最も古いボイスを返す (奪うかどうかは発音の通し番号だけで決める)
	static Voice& pickVoice(Channel& ch)
	{
		Voice* oldest = &ch.voices[0];
		for (auto& v : ch.voices)
		{
			if (!v.active)
			{
				return v;
			}
			if (v.age < oldest->age)
			{
				oldest = &v;
			}
		}
		return *oldest;
	}

	void startVoice(Voice& v, const Channel& ch, const MultiSampleInstrument::Zone& zone, int key)
	{
		const auto& inst = zone.voice;
		const auto& adsr = inst.adsr();
		const double scaled = (zone.rootKey >= 0 && zone.scaleTuning != 1.0f)
			? static_cast<double>(zone.rootKey) + static_cast<double>(key - zone.rootKey) * static_cast<double>(zone.scaleTuning)
			: static_cast<double>(key);
		const double freq = modern_dsp::midiToHz(scaled)
			* modern_dsp::centsToRatio(static_cast<double>(ch.tuneCents));
		v.active = true;
		v.releasing = false;
		v.zone = &zone;
		v.keep = ch.instrument;
		v.pos = 0.0;
		v.step = freq / static_cast<double>(inst.baseFreq());
		v.gain = (zone.samplerGain >= 0.0f) ? zone.samplerGain : zone.gain;
		v.oneShot = (&ch == &m_channels[PERCUSSION_CHANNEL]);
		v.age = ++m_age;
		const double release = (ch.releaseMs > 0) ? static_cast<double>(ch.releaseMs) * 0.001
			: static_cast<double>(adsr.release);
		// SF2 の時間は 100 dB 下がるまで。ADSR の時間は 60 dB 下がるまでなので 0.6 倍にする
		v.env.configure(static_cast<double>(adsr.attack), static_cast<double>(adsr.decay) * 0.6,
			adsr.sustain, release * 0.6, static_cast<double>(m_rate));
		v.env.trigger();
		v.panL = std::min(1.0f, 1.0f - zone.pan);
		v.panR = std::min(1.0f, 1.0f + zone.pan);
		v.exclusiveClass = zone.exclusiveClass;
		v.delayLeft = static_cast<std::uint32_t>(zone.delaySec * static_cast<float>(m_rate));
		v.holdLeft = static_cast<std::uint32_t>(zone.holdSec * static_cast<float>(m_rate));
		v.filtered = zone.filterHz > 0.0f;
		if (v.filtered)
		{
			// SF2 の Q (dB) を、フィルターの減衰係数 k に直す。0 dB は Butterworth (k = 1.414)
			const float k = std::clamp(1.4142f * std::pow(10.0f, -zone.filterQ / 20.0f), 0.04f, 2.0f);
			v.filter.reset();
			v.filter.setup(static_cast<double>(zone.filterHz), (2.0f - k) / 1.96f, static_cast<double>(m_rate));
		}
	}

	static void renderVoice(Voice& v, float* out, int len, float gl, float gr)
	{
		const auto& inst = v.zone->voice;
		const std::vector<float>& data = inst.samples();
		const std::size_t last = data.size() - 1;
		const std::size_t loopStart = inst.loopStart();
		const std::size_t loopEnd = std::min(inst.loopEnd(), data.size());
		const bool looped = !v.oneShot && loopEnd > loopStart + 1;
		for (int i = 0; i < len; ++i)
		{
			if (v.delayLeft > 0)
			{
				--v.delayLeft;
				continue;
			}
			if (looped && v.pos >= static_cast<double>(loopEnd))
			{
				v.pos -= static_cast<double>(loopEnd - loopStart);
			}
			if (!looped && v.pos >= static_cast<double>(last))
			{
				v.active = false;
				v.keep.reset();
				return;
			}
			const auto i0 = static_cast<std::size_t>(v.pos);
			// ループの終わりの次の点は、ループの先頭に戻る (継ぎ目で補間する)
			std::size_t i1 = i0 + 1;
			if (looped && i1 >= loopEnd)
			{
				i1 = loopStart + (i1 - loopEnd);
			}
			const float frac = static_cast<float>(v.pos - static_cast<double>(i0));
			const float s = data[i0] + frac * (data[std::min(i1, last)] - data[i0]);
			float level;
			if (v.holdLeft > 0 && v.env.stage() == modern_dsp::Adsr::Stage::Decay)
			{
				--v.holdLeft;
				level = v.env.level();
			}
			else
			{
				level = v.env.next();
			}
			if (!v.env.active())
			{
				v.active = false;
				v.keep.reset();
				return;
			}
			const float filtered = v.filtered ? v.filter.process(s, modern_dsp::StateVariableFilter::Type::LowPass) : s;
			const float x = filtered * level * v.gain;
			out[i * 2] += x * gl * v.panL;
			out[i * 2 + 1] += x * gr * v.panR;
			v.pos += v.step;
		}
	}

	std::uint32_t m_rate;
	std::shared_ptr<const Sf2File> m_soundFont;
	std::map<int, std::shared_ptr<const MultiSampleInstrument>> m_cache;
	std::array<Channel, CHANNELS> m_channels{};
	std::array<std::shared_ptr<const MultiSampleInstrument>, CHANNELS> m_overrides{};
	std::uint64_t m_age = 0;
};

} // namespace mitiru_mml
