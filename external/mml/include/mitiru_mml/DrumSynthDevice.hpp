#pragma once
/// @file DrumSynthDevice.hpp
/// @brief サンプルを使わずに、キック、スネア、ハイハット、タム、シンバル、リムショット、クラップを合成する Device
/// @details 正弦波のピッチエンベロープ (キックとタム)、フィルターを通したノイズ (スネア、ハイハット、クラップ)、
///          6 本の矩形波の金属音 (ハイハット、シンバル) を、種類ごとの配合 (DrumRecipe) で混ぜる。
///          ドラムは 1 発もので、消音は無視する。ノイズは (種類、その種類の発音の通し番号) から作ったシードだけを使う。
///
/// ポート (チャンネル) は種類。0 キック、1 スネア、2 クローズドハイハット、3 タム、4 シンバル、5 リムショット、
/// 6 クラップ、7 オープンハイハット。
/// レジスタ: 0x00 発音 (value の下位 7 ビット = 半音の移調 + 64)、0x03 音量 0〜127、0x04 定位 0〜127、
///           1000 以上は modern_param (DEC、TUNE、TONE、SNAP)

#include <mitiru_mml/ChipDevice.hpp>
#include <mitiru_mml/MmlExtensions.hpp>
#include <mitiru_mml/ModernDsp.hpp>
#include <mitiru_mml/ModernRng.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace mitiru_mml
{

/// @brief ドラムの種類 (DrumSynthDevice のポート番号)
enum class DrumType : int
{
	Kick = 0,
	Snare = 1,
	ClosedHat = 2,
	Tom = 3,
	Cymbal = 4,
	Rim = 5,
	Clap = 6,
	OpenHat = 7,
};

/// @brief 種類ごとの音の配合
struct DrumRecipe
{
	float toneStartHz = 0.0f;   ///< 正弦波の最初の周波数 (0 なら正弦波なし)
	float toneEndHz = 0.0f;     ///< 正弦波の最後の周波数
	float tonePitchTau = 0.03f; ///< 周波数が下がる時定数 (秒)
	float toneAmp = 0.0f;
	float toneDecay = 0.3f;     ///< 60 dB 下がるまでの時間 (秒)
	float tone2Ratio = 0.0f;    ///< 2 つ目の正弦波の周波数比 (0 なら無し)
	float tone2Amp = 0.0f;
	float noiseAmp = 0.0f;
	float noiseDecay = 0.1f;
	int noiseFilter = 0;        ///< 0 = LP、1 = HP、2 = BP
	float noiseCutoff = 5000.0f;
	float noiseRes = 0.0f;
	float metalAmp = 0.0f;      ///< 6 本の矩形波の量
	float metalDecay = 0.1f;
	float metalHp = 6000.0f;    ///< 金属音に通す高域フィルターのカットオフ
	float clickAmp = 0.0f;      ///< 発音の最初の 2 ミリ秒のノイズ
	float drive = 1.0f;         ///< 飽和 (1 で無し)
	int clapBursts = 0;         ///< クラップのように、最初に短い間隔で鳴らす回数 (0 なら無し)
	float level = 1.0f;
};

/// @brief ドラムシンセの Device
class DrumSynthDevice final : public ChipDevice
{
public:
	static constexpr int CHANNELS = 8;
	static constexpr int VOICES_PER_TYPE = 6;

	static constexpr std::uint32_t REG_NOTE_ON = 0x00;
	static constexpr std::uint32_t REG_NOTE_OFF = 0x01;
	static constexpr std::uint32_t REG_VOLUME = 0x03;
	static constexpr std::uint32_t REG_PAN = 0x04;

	explicit DrumSynthDevice(std::uint32_t outputRate = 44100)
		: m_rate(outputRate)
	{
		reset();
	}

	[[nodiscard]] std::string_view name() const noexcept override
	{
		return "drum synth";
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
			fresh.recipe = recipeFor(static_cast<DrumType>(c));
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
		Channel& ch = m_channels[port];
		const auto signedValue = static_cast<int>(static_cast<std::int32_t>(value));
		switch (reg)
		{
		case REG_NOTE_ON:
			trigger(static_cast<int>(port), ch, static_cast<int>(value & 0x7F) - 64);
			break;
		case REG_NOTE_OFF:
			break;
		case REG_VOLUME:
			ch.volume.target = static_cast<float>(std::clamp(signedValue, 0, 127));
			break;
		case REG_PAN:
			ch.pan.target = static_cast<float>(std::clamp(signedValue, 0, 127));
			break;
		case modern_param::DEC:
			ch.decaySeconds = static_cast<float>(std::max(signedValue, 1)) * 0.001f;
			break;
		case modern_param::TUNE:
			ch.tuneCents = static_cast<float>(signedValue);
			break;
		case modern_param::TONE:
			ch.tone = std::clamp(static_cast<float>(signedValue), 0.0f, 100.0f);
			break;
		case modern_param::SNAP:
			ch.snap = std::clamp(static_cast<float>(signedValue), 0.0f, 100.0f);
			break;
		case modern_param::VOL:
			ch.volume.target = static_cast<float>(std::clamp(signedValue, 0, 127));
			break;
		case modern_param::PAN:
			ch.pan.target = static_cast<float>(std::clamp(signedValue, 0, 127));
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
				float panL = 0.0f;
				float panR = 0.0f;
				modern_dsp::panGains(ch.pan.current, panL, panR);
				const float level = modern_dsp::volumeCurve127(static_cast<int>(std::lround(ch.volume.current)));
				for (auto& v : ch.voices)
				{
					if (v.active)
					{
						renderVoice(v, out + static_cast<std::size_t>(done) * 2, len,
							level * panL * 1.41421356f, level * panR * 1.41421356f);
					}
				}
			}
			done += static_cast<std::uint32_t>(len);
		}
	}

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

	/// @brief 種類ごとの標準の配合
	[[nodiscard]] static DrumRecipe recipeFor(DrumType type) noexcept
	{
		DrumRecipe r;
		switch (type)
		{
		case DrumType::Kick:
			r.toneStartHz = 165.0f;
			r.toneEndHz = 46.0f;
			r.tonePitchTau = 0.028f;
			r.toneAmp = 1.0f;
			r.toneDecay = 0.42f;
			r.clickAmp = 0.35f;
			r.drive = 1.6f;
			r.level = 0.95f;
			break;
		case DrumType::Snare:
			r.toneStartHz = 205.0f;
			r.toneEndHz = 175.0f;
			r.tonePitchTau = 0.02f;
			r.toneAmp = 0.55f;
			r.toneDecay = 0.14f;
			r.tone2Ratio = 1.78f;
			r.tone2Amp = 0.3f;
			r.noiseAmp = 0.8f;
			r.noiseDecay = 0.2f;
			r.noiseFilter = 1;
			r.noiseCutoff = 1800.0f;
			r.clickAmp = 0.2f;
			r.drive = 1.3f;
			r.level = 0.8f;
			break;
		case DrumType::ClosedHat:
			r.noiseAmp = 0.5f;
			r.noiseDecay = 0.05f;
			r.noiseFilter = 1;
			r.noiseCutoff = 7500.0f;
			r.metalAmp = 0.5f;
			r.metalDecay = 0.05f;
			r.metalHp = 7000.0f;
			r.level = 0.5f;
			break;
		case DrumType::OpenHat:
			r.noiseAmp = 0.5f;
			r.noiseDecay = 0.38f;
			r.noiseFilter = 1;
			r.noiseCutoff = 7500.0f;
			r.metalAmp = 0.5f;
			r.metalDecay = 0.38f;
			r.metalHp = 7000.0f;
			r.level = 0.5f;
			break;
		case DrumType::Tom:
			r.toneStartHz = 190.0f;
			r.toneEndHz = 110.0f;
			r.tonePitchTau = 0.05f;
			r.toneAmp = 1.0f;
			r.toneDecay = 0.36f;
			r.clickAmp = 0.12f;
			r.drive = 1.2f;
			r.level = 0.85f;
			break;
		case DrumType::Cymbal:
			r.noiseAmp = 0.35f;
			r.noiseDecay = 1.4f;
			r.noiseFilter = 1;
			r.noiseCutoff = 5500.0f;
			r.metalAmp = 0.6f;
			r.metalDecay = 1.4f;
			r.metalHp = 5000.0f;
			r.level = 0.45f;
			break;
		case DrumType::Rim:
			r.toneStartHz = 1700.0f;
			r.toneEndHz = 1300.0f;
			r.tonePitchTau = 0.004f;
			r.toneAmp = 0.7f;
			r.toneDecay = 0.04f;
			r.noiseAmp = 0.3f;
			r.noiseDecay = 0.03f;
			r.noiseFilter = 2;
			r.noiseCutoff = 3000.0f;
			r.clickAmp = 0.4f;
			r.level = 0.7f;
			break;
		case DrumType::Clap:
		default:
			r.noiseAmp = 3.2f;
			r.noiseDecay = 0.2f;
			r.noiseFilter = 2;
			r.noiseCutoff = 1500.0f;
			r.noiseRes = 0.35f;
			r.clapBursts = 4;
			r.drive = 1.8f;
			r.level = 0.85f;
			break;
		}
		return r;
	}

private:
	struct Voice
	{
		bool active = false;
		std::uint64_t age = 0;
		ModernRng rng{1};
		double tonePhase = 0.0;
		double tone2Phase = 0.0;
		std::array<double, 6> metalPhase{};
		float pitchEnv = 1.0f;
		float toneEnv = 0.0f;
		float noiseEnv = 0.0f;
		float metalEnv = 0.0f;
		float pitchCoef = 0.0f;
		float toneCoef = 0.0f;
		float noiseCoef = 0.0f;
		float metalCoef = 0.0f;
		float chokeCoef = 1.0f;
		double toneStart = 0.0;
		double toneEnd = 0.0;
		double tone2Ratio = 0.0;
		int samplesPlayed = 0;
		int clickSamples = 0;
		int burstSamples = 0;
		DrumRecipe recipe;
		float noiseDriveScale = 1.0f;
		float toneAmp = 0.0f;
		float noiseAmp = 0.0f;
		float clickAmp = 0.0f;
		modern_dsp::StateVariableFilter noiseFilter;
		modern_dsp::StateVariableFilter metalFilter;
	};

	struct Channel
	{
		DrumRecipe recipe;
		std::array<Voice, VOICES_PER_TYPE> voices{};
		modern_dsp::Smoothed volume{102.0f, 102.0f};
		modern_dsp::Smoothed pan{64.0f, 64.0f};
		float decaySeconds = 0.0f; ///< 0 なら標準
		float tuneCents = 0.0f;
		float tone = 50.0f;
		float snap = 50.0f;
		std::uint32_t hitCounter = 0;
	};

	static constexpr std::array<double, 6> METAL_HZ = {205.3, 304.4, 369.6, 522.7, 540.0, 800.0};

	[[nodiscard]] float coefFor(double sec60) const noexcept
	{
		return static_cast<float>(std::exp(-6.907755278982137 / (std::max(sec60, 0.0005) * static_cast<double>(m_rate))));
	}

	void trigger(int type, Channel& ch, int semitones)
	{
		// 閉じたハイハットは、鳴っているオープンハイハットを止める
		if (type == static_cast<int>(DrumType::ClosedHat))
		{
			for (auto& v : m_channels[static_cast<std::size_t>(DrumType::OpenHat)].voices)
			{
				v.chokeCoef = coefFor(0.006);
			}
		}

		Voice* slot = &ch.voices[0];
		for (auto& v : ch.voices)
		{
			if (!v.active)
			{
				slot = &v;
				break;
			}
			if (v.age < slot->age)
			{
				slot = &v;
			}
		}
		Voice& v = *slot;
		const DrumRecipe& base = ch.recipe;
		v = Voice{};
		v.active = true;
		v.age = ++m_age;
		v.rng = ModernRng(mixSeed(static_cast<std::uint32_t>(type), ch.hitCounter++, 0xD7u));
		v.recipe = base;

		const double tune = modern_dsp::centsToRatio(static_cast<double>(ch.tuneCents) + 100.0 * static_cast<double>(semitones));
		v.toneStart = static_cast<double>(base.toneStartHz) * tune;
		v.toneEnd = static_cast<double>(base.toneEndHz) * tune;
		v.tone2Ratio = static_cast<double>(base.tone2Ratio);

		// 主な減衰を Xdec で替えたとき、ほかの減衰も同じ比率で伸び縮みする
		const bool toneLed = base.toneAmp > 0.0f && (base.noiseAmp <= 0.0f || base.toneDecay >= base.noiseDecay);
		const float mainDecay = toneLed ? base.toneDecay : (base.noiseAmp > 0.0f ? base.noiseDecay : base.metalDecay);
		const float scale = (ch.decaySeconds > 0.0f) ? ch.decaySeconds / mainDecay : 1.0f;
		v.pitchCoef = coefFor(static_cast<double>(base.tonePitchTau) * 6.907755278982137);
		v.toneCoef = coefFor(static_cast<double>(base.toneDecay * scale));
		v.noiseCoef = coefFor(static_cast<double>(base.noiseDecay * scale));
		v.metalCoef = coefFor(static_cast<double>(base.metalDecay * scale));
		v.toneEnv = 1.0f;
		v.noiseEnv = 1.0f;
		v.metalEnv = 1.0f;
		v.clickSamples = static_cast<int>(0.002 * static_cast<double>(m_rate));
		v.burstSamples = static_cast<int>(0.009 * static_cast<double>(m_rate));

		// tone (0〜100、標準 50): スネアは上げるとノイズが増え、ハイハットとシンバルは上げると明るくなる。キックは上げると歪む
		const float tf = (ch.tone - 50.0f) / 50.0f;
		v.toneAmp = base.toneAmp;
		v.noiseAmp = base.noiseAmp;
		float cutoff = base.noiseCutoff;
		float hp = base.metalHp;
		float drive = base.drive;
		switch (static_cast<DrumType>(type))
		{
		case DrumType::Snare:
			v.noiseAmp *= 1.0f + 0.7f * tf;
			v.toneAmp *= 1.0f - 0.6f * tf;
			break;
		case DrumType::ClosedHat:
		case DrumType::OpenHat:
		case DrumType::Cymbal:
			cutoff *= std::exp2(tf * 0.6f);
			hp *= std::exp2(tf * 0.6f);
			break;
		case DrumType::Kick:
			drive *= 1.0f + 1.2f * std::max(0.0f, tf);
			break;
		default:
			break;
		}
		v.recipe.drive = drive;
		v.clickAmp = base.clickAmp * (ch.snap / 50.0f);

		const double rate = static_cast<double>(m_rate);
		v.noiseFilter.setup(static_cast<double>(cutoff), base.noiseRes, rate);
		v.metalFilter.setup(static_cast<double>(hp), 0.0f, rate);
		for (std::size_t i = 0; i < v.metalPhase.size(); ++i)
		{
			v.metalPhase[i] = static_cast<double>(v.rng.nextUnit());
		}
	}

	void renderVoice(Voice& v, float* out, int len, float gl, float gr) const
	{
		const DrumRecipe& r = v.recipe;
		const double rate = static_cast<double>(m_rate);
		const auto nfType = static_cast<modern_dsp::StateVariableFilter::Type>(r.noiseFilter);
		for (int i = 0; i < len; ++i)
		{
			float mix = 0.0f;

			if (r.toneStartHz > 0.0f)
			{
				const double freq = v.toneEnd + (v.toneStart - v.toneEnd) * static_cast<double>(v.pitchEnv);
				v.pitchEnv *= v.pitchCoef;
				v.tonePhase += freq / rate;
				v.tonePhase -= std::floor(v.tonePhase);
				float tone = static_cast<float>(std::sin(2.0 * modern_dsp::PI * v.tonePhase));
				if (v.tone2Ratio > 0.0)
				{
					v.tone2Phase += freq * v.tone2Ratio / rate;
					v.tone2Phase -= std::floor(v.tone2Phase);
					tone += r.tone2Amp / std::max(v.toneAmp, 0.001f)
						* static_cast<float>(std::sin(2.0 * modern_dsp::PI * v.tone2Phase));
				}
				mix += v.toneAmp * v.toneEnv * tone;
			}
			v.toneEnv *= v.toneCoef;

			if (v.noiseAmp > 0.0f)
			{
				float env = v.noiseEnv;
				if (r.clapBursts > 0)
				{
					// 短い間隔で鳴らす部分: 9 ミリ秒ごとに立ち上がり直す
					const int burstIndex = v.samplesPlayed / v.burstSamples;
					if (burstIndex < r.clapBursts - 1)
					{
						const float phase = static_cast<float>(v.samplesPlayed % v.burstSamples) / static_cast<float>(v.burstSamples);
						env = 0.6f * std::exp(-phase * 5.0f);
					}
				}
				const float nz = v.noiseFilter.process(v.rng.nextSigned(), nfType);
				mix += v.noiseAmp * env * nz;
			}
			v.noiseEnv *= v.noiseCoef;

			if (r.metalAmp > 0.0f)
			{
				float m = 0.0f;
				for (std::size_t k = 0; k < v.metalPhase.size(); ++k)
				{
					v.metalPhase[k] += METAL_HZ[k] * 2.0 / rate;
					v.metalPhase[k] -= std::floor(v.metalPhase[k]);
					m += (v.metalPhase[k] < 0.5) ? 1.0f : -1.0f;
				}
				m = v.metalFilter.process(m * (1.0f / 6.0f), modern_dsp::StateVariableFilter::Type::HighPass);
				mix += r.metalAmp * v.metalEnv * m;
			}
			v.metalEnv *= v.metalCoef;

			if (v.samplesPlayed < v.clickSamples)
			{
				const float fade = 1.0f - static_cast<float>(v.samplesPlayed) / static_cast<float>(v.clickSamples);
				mix += v.clickAmp * fade * v.rng.nextSigned();
			}

			if (r.drive > 1.0f)
			{
				mix = std::tanh(mix * r.drive) / std::tanh(r.drive);
			}

			// 立ち上がりのクリックを避けるため、最初の 0.3 ミリ秒だけ直線で上げる
			const float rise = std::min(1.0f, static_cast<float>(v.samplesPlayed + 1) / 14.0f);
			const float choke = v.chokeCoef;
			v.toneEnv *= choke;
			v.noiseEnv *= choke;
			v.metalEnv *= choke;
			const float x = mix * r.level * rise;
			out[i * 2] += x * gl;
			out[i * 2 + 1] += x * gr;
			++v.samplesPlayed;

			const float tail = std::max({v.toneEnv * (r.toneStartHz > 0.0f ? 1.0f : 0.0f),
				v.noiseEnv * (r.noiseAmp > 0.0f ? 1.0f : 0.0f), v.metalEnv * (r.metalAmp > 0.0f ? 1.0f : 0.0f)});
			if (tail < 1.0e-4f && v.samplesPlayed > v.clickSamples)
			{
				v.active = false;
				return;
			}
		}
	}

	std::uint32_t m_rate;
	std::array<Channel, CHANNELS> m_channels{};
	std::uint64_t m_age = 0;
};

} // namespace mitiru_mml
