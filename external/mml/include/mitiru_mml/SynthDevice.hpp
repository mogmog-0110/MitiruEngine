#pragma once
/// @file SynthDevice.hpp
/// @brief 減算シンセ (オシレーター 2 つ + ユニゾン + フィルター + エンベロープ + LFO) の Device
/// @details 16 チャンネルがそれぞれ音色 (SynthPatch) を持ち、チャンネルごとに 8 声の和音を鳴らせる。
///          オシレーターは PolyBLEP で帯域を制限し、フィルターは TPT 形式の状態変数フィルター。
///          パラメーターの変化は 16 サンプルごとに時定数 8 ミリ秒でなめらかにつなぐので、
///          MML のランプ (Xcutoff=500>4000/8) でもクリックが出ない。
///          乱数 (ノイズ、ユニゾンの初期位相、S&H) は、チャンネルと発音の通し番号から作ったシードだけを使う。
///
/// レジスタ (port = チャンネル 0〜15)
///   0x00 発音: value = 鍵 | (ベロシティ << 8)。消音は 0x01 (そのチャンネルの鳴っている音すべて)
///   0x02 組み込みの音色を選ぶ (SynthPatch の番号)
///   0x03〜 は使わない。1000 以上は modern_param の id で、value はその単位の整数 (符号つき 32bit)

#include <mitiru_mml/ChipDevice.hpp>
#include <mitiru_mml/MmlExtensions.hpp>
#include <mitiru_mml/ModernDsp.hpp>
#include <mitiru_mml/ModernRng.hpp>
#include <mitiru_mml/SynthPatch.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace mitiru_mml
{

/// @brief 減算シンセの Device
class SynthDevice final : public ChipDevice
{
public:
	static constexpr int CHANNELS = 16;
	static constexpr int VOICES_PER_CHANNEL = 8;
	static constexpr int MAX_UNISON = 7;

	static constexpr std::uint32_t REG_NOTE_ON = 0x00;
	static constexpr std::uint32_t REG_NOTE_OFF = 0x01;
	static constexpr std::uint32_t REG_PRESET = 0x02;

	explicit SynthDevice(std::uint32_t outputRate = 44100)
		: m_rate(outputRate)
	{
		reset();
	}

	/// @brief チャンネルの音色を設定する (reset() の後も、この音色に戻る)
	void setPatch(int channel, const SynthPatch& patch)
	{
		if (channel < 0 || channel >= CHANNELS)
		{
			return;
		}
		m_configured[static_cast<std::size_t>(channel)] = patch;
		loadPatch(m_channels[static_cast<std::size_t>(channel)], patch);
	}

	[[nodiscard]] std::string_view name() const noexcept override
	{
		return "subtractive synth";
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
			loadPatch(fresh, m_configured[static_cast<std::size_t>(c)]);
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
		const auto channel = static_cast<int>(port);
		Channel& ch = m_channels[port];
		if (reg == REG_NOTE_ON)
		{
			noteOn(channel, ch, static_cast<int>(value & 0xFF));
		}
		else if (reg == REG_NOTE_OFF)
		{
			noteOff(ch);
		}
		else if (reg == REG_PRESET)
		{
			// 曲の途中で音色だけを替えても、音量と定位と音程は保つ
			const modern_dsp::Smoothed volume = ch.volume;
			const modern_dsp::Smoothed pan = ch.pan;
			const modern_dsp::Smoothed tune = ch.tuneCents;
			m_configured[port] = synthPreset(static_cast<int>(value));
			loadPatch(ch, m_configured[port]);
			ch.volume = volume;
			ch.pan = pan;
			ch.tuneCents = tune;
		}
		else if (reg >= 1000)
		{
			applyParam(ch, static_cast<int>(reg), static_cast<int>(static_cast<std::int32_t>(value)));
		}
	}

	void generate(float* out, std::uint32_t frames) override
	{
		std::fill(out, out + static_cast<std::size_t>(frames) * 2, 0.0f);
		std::uint32_t done = 0;
		while (done < frames)
		{
			const int len = static_cast<int>(std::min<std::uint32_t>(modern_dsp::CONTROL_BLOCK, frames - done));
			const float coef = modern_dsp::smoothCoef(len, 0.008, static_cast<double>(m_rate));
			for (auto& ch : m_channels)
			{
				stepChannel(ch, coef);
				bool any = false;
				for (const auto& v : ch.voices)
				{
					any = any || v.active;
				}
				if (!any)
				{
					continue;
				}
				BlockParams bp = blockParams(ch);
				for (auto& v : ch.voices)
				{
					if (v.active)
					{
						renderVoice(v, ch, bp, out + static_cast<std::size_t>(done) * 2, len);
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

private:
	struct Voice
	{
		bool active = false;
		bool released = false;
		int key = 60;
		std::uint64_t age = 0;
		std::array<double, MAX_UNISON> phase1{};
		double phase2 = 0.0;
		double phaseSub = 0.0;
		double lfoPhase = 0.0;
		double elapsedSec = 0.0;
		float lfoHeld = 0.0f;
		float filterLevel = 0.0f;
		ModernRng rng{1};
		modern_dsp::Adsr amp;
		modern_dsp::Adsr filterEnv;
		std::array<modern_dsp::StateVariableFilter, 2> filter;
		SynthPatch::Wave wave1 = SynthPatch::Wave::Saw;
		SynthPatch::Wave wave2 = SynthPatch::Wave::Saw;
		int unison = 1;
		int tune2 = 0;
	};

	struct Channel
	{
		SynthPatch patch;
		std::array<Voice, VOICES_PER_CHANNEL> voices{};
		modern_dsp::Smoothed cutoffLog2;
		modern_dsp::Smoothed resonance;
		modern_dsp::Smoothed filterEnv;
		modern_dsp::Smoothed detune;
		modern_dsp::Smoothed unisonDetune;
		modern_dsp::Smoothed spread;
		modern_dsp::Smoothed pulseWidth;
		modern_dsp::Smoothed mix2;
		modern_dsp::Smoothed sub;
		modern_dsp::Smoothed noise;
		modern_dsp::Smoothed gain;
		modern_dsp::Smoothed lfoPitch;
		modern_dsp::Smoothed lfoFilter;
		modern_dsp::Smoothed lfoAmp;
		modern_dsp::Smoothed lfoPulseWidth;
		modern_dsp::Smoothed tuneCents;
		modern_dsp::Smoothed volume;
		modern_dsp::Smoothed pan;
		std::uint32_t noteCounter = 0;
	};

	/// @brief 1 ブロックの間に使う、チャンネルのパラメーターの値
	struct BlockParams
	{
		float cutoffLog2 = 0.0f;
		float resonance = 0.0f;
		float filterEnv = 0.0f;
		float detune = 0.0f;
		float unisonDetune = 0.0f;
		float spread = 0.0f;
		float pulseWidth = 0.5f;
		float mix2 = 0.0f;
		float sub = 0.0f;
		float noise = 0.0f;
		float lfoPitch = 0.0f;
		float lfoFilter = 0.0f;
		float lfoAmp = 0.0f;
		float lfoPulseWidth = 0.0f;
		float tune = 0.0f;
		float level = 0.0f;   ///< 音色の gain と音量の積
		float pan = 64.0f;
	};

	static void loadPatch(Channel& ch, const SynthPatch& p)
	{
		ch.patch = p;
		ch.cutoffLog2.snap(std::log2(std::max(p.cutoffHz, 10.0f)));
		ch.resonance.snap(p.resonance);
		ch.filterEnv.snap(p.filterEnvSemitones);
		ch.detune.snap(p.detuneCents);
		ch.unisonDetune.snap(p.unisonDetune);
		ch.spread.snap(p.spread);
		ch.pulseWidth.snap(p.pulseWidth);
		ch.mix2.snap(p.mix2);
		ch.sub.snap(p.sub);
		ch.noise.snap(p.noise);
		ch.gain.snap(p.gain);
		ch.lfoPitch.snap(p.lfoPitchCents);
		ch.lfoFilter.snap(p.lfoFilterSemitones);
		ch.lfoAmp.snap(p.lfoAmp);
		ch.lfoPulseWidth.snap(p.lfoPulseWidth);
		ch.tuneCents.snap(0.0f);
		ch.volume.snap(102.0f); // V12 の既定
		ch.pan.snap(64.0f);
	}

	static void stepChannel(Channel& ch, float coef) noexcept
	{
		ch.cutoffLog2.step(coef);
		ch.resonance.step(coef);
		ch.filterEnv.step(coef);
		ch.detune.step(coef);
		ch.unisonDetune.step(coef);
		ch.spread.step(coef);
		ch.pulseWidth.step(coef);
		ch.mix2.step(coef);
		ch.sub.step(coef);
		ch.noise.step(coef);
		ch.gain.step(coef);
		ch.lfoPitch.step(coef);
		ch.lfoFilter.step(coef);
		ch.lfoAmp.step(coef);
		ch.lfoPulseWidth.step(coef);
		ch.tuneCents.step(coef);
		ch.volume.step(coef);
		ch.pan.step(coef);
	}

	static BlockParams blockParams(const Channel& ch) noexcept
	{
		BlockParams b;
		b.cutoffLog2 = ch.cutoffLog2.current;
		b.resonance = ch.resonance.current;
		b.filterEnv = ch.filterEnv.current;
		b.detune = ch.detune.current;
		b.unisonDetune = ch.unisonDetune.current;
		b.spread = ch.spread.current;
		b.pulseWidth = ch.pulseWidth.current;
		b.mix2 = ch.mix2.current;
		b.sub = ch.sub.current;
		b.noise = ch.noise.current;
		b.lfoPitch = ch.lfoPitch.current;
		b.lfoFilter = ch.lfoFilter.current;
		b.lfoAmp = ch.lfoAmp.current;
		b.lfoPulseWidth = ch.lfoPulseWidth.current;
		b.tune = ch.tuneCents.current;
		b.level = ch.gain.current * modern_dsp::volumeCurve127(static_cast<int>(std::lround(ch.volume.current)));
		b.pan = ch.pan.current;
		return b;
	}

	/// @brief パラメーターを設定する。声が鳴っていないチャンネルは、なめらかにつながずすぐ切り替える
	void applyParam(Channel& ch, int id, int value)
	{
		bool idle = true;
		for (const auto& v : ch.voices)
		{
			idle = idle && !v.active;
		}
		const auto set = [idle](modern_dsp::Smoothed& s, float target)
		{
			s.target = target;
			if (idle)
			{
				s.current = target;
			}
		};
		const auto f = static_cast<float>(value);
		SynthPatch& p = ch.patch;
		switch (id)
		{
		case modern_param::CUTOFF: set(ch.cutoffLog2, std::log2(std::max(f, 10.0f))); break;
		case modern_param::RESONANCE: set(ch.resonance, f * 0.01f); break;
		case modern_param::FENV: set(ch.filterEnv, f); break;
		case modern_param::FKEY: p.keyTrack = f * 0.01f; break;
		case modern_param::FTYPE: p.filterType = static_cast<SynthPatch::FilterType>(std::clamp(value, 0, 2)); break;
		case modern_param::DETUNE: set(ch.detune, f); break;
		case modern_param::UDETUNE: set(ch.unisonDetune, f); break;
		case modern_param::SPREAD: set(ch.spread, f * 0.01f); break;
		case modern_param::UNISON: p.unison = std::clamp(value, 1, MAX_UNISON); break;
		case modern_param::PW: set(ch.pulseWidth, f * 0.01f); break;
		case modern_param::WAVE: p.wave1 = static_cast<SynthPatch::Wave>(std::clamp(value, 0, 4)); break;
		case modern_param::WAVE2: p.wave2 = static_cast<SynthPatch::Wave>(std::clamp(value, 0, 4)); break;
		case modern_param::MIX2: set(ch.mix2, f * 0.01f); break;
		case modern_param::SUB: set(ch.sub, f * 0.01f); break;
		case modern_param::NOISE: set(ch.noise, f * 0.01f); break;
		case modern_param::TUNE2: p.tune2Semitones = value; break;
		case modern_param::ATK: p.ampAttack = f * 0.001f; break;
		case modern_param::DEC: p.ampDecay = f * 0.001f; break;
		case modern_param::SUS: p.ampSustain = f * 0.01f; break;
		case modern_param::REL: p.ampRelease = f * 0.001f; break;
		case modern_param::FATK: p.filterAttack = f * 0.001f; break;
		case modern_param::FDEC: p.filterDecay = f * 0.001f; break;
		case modern_param::FSUS: p.filterSustain = f * 0.01f; break;
		case modern_param::FREL: p.filterRelease = f * 0.001f; break;
		case modern_param::LFOR: p.lfoRateHz = f * 0.1f; break;
		case modern_param::LFOWAVE: p.lfoWave = static_cast<SynthPatch::LfoWave>(std::clamp(value, 0, 4)); break;
		case modern_param::LFOP: set(ch.lfoPitch, f); break;
		case modern_param::LFOF: set(ch.lfoFilter, f); break;
		case modern_param::LFOA: set(ch.lfoAmp, f * 0.01f); break;
		case modern_param::LFOPW: set(ch.lfoPulseWidth, f * 0.01f); break;
		case modern_param::LFODELAY: p.lfoDelaySec = f * 0.001f; break;
		case modern_param::GAIN: set(ch.gain, p.gain * f * 0.01f); break;
		case modern_param::VOL: set(ch.volume, f); break;
		case modern_param::PAN: set(ch.pan, f); break;
		case modern_param::TUNE: set(ch.tuneCents, f); break;
		default: break;
		}
	}

	void configureEnvelopes(Voice& v, const SynthPatch& p) const
	{
		const double rate = static_cast<double>(m_rate);
		v.amp.configure(std::max(static_cast<double>(p.ampAttack), 0.001), static_cast<double>(p.ampDecay),
			p.ampSustain, static_cast<double>(p.ampRelease), rate);
		v.filterEnv.configure(static_cast<double>(p.filterAttack), static_cast<double>(p.filterDecay),
			p.filterSustain, static_cast<double>(p.filterRelease), rate);
	}

	void noteOn(int channel, Channel& ch, int key)
	{
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
		const SynthPatch& p = ch.patch;
		v = Voice{};
		v.active = true;
		v.key = std::clamp(key, 0, 127);
		v.age = ++m_age;
		v.rng = ModernRng(mixSeed(static_cast<std::uint32_t>(channel), ch.noteCounter++, 0x51u));
		v.wave1 = p.wave1;
		v.wave2 = p.wave2;
		v.unison = std::clamp(p.unison, 1, MAX_UNISON);
		v.tune2 = p.tune2Semitones;
		if (v.unison > 1)
		{
			for (int i = 0; i < v.unison; ++i)
			{
				v.phase1[static_cast<std::size_t>(i)] = static_cast<double>(v.rng.nextUnit());
			}
		}
		configureEnvelopes(v, p);
		v.amp.trigger();
		v.filterEnv.trigger();
		v.filter[0].reset();
		v.filter[1].reset();
	}

	void noteOff(Channel& ch)
	{
		for (auto& v : ch.voices)
		{
			if (v.active && !v.released)
			{
				v.released = true;
				configureEnvelopes(v, ch.patch);
				v.amp.release();
				v.filterEnv.release();
			}
		}
	}

	[[nodiscard]] static float lfoValue(SynthPatch::LfoWave wave, double phase, Voice& v, bool wrapped) noexcept
	{
		switch (wave)
		{
		case SynthPatch::LfoWave::Triangle: return modern_dsp::triangleNaive(phase);
		case SynthPatch::LfoWave::Saw: return static_cast<float>(1.0 - 2.0 * phase);
		case SynthPatch::LfoWave::Square: return phase < 0.5 ? 1.0f : -1.0f;
		case SynthPatch::LfoWave::SampleAndHold:
			if (wrapped)
			{
				v.lfoHeld = v.rng.nextSigned();
			}
			return v.lfoHeld;
		case SynthPatch::LfoWave::Sine:
		default: return static_cast<float>(std::sin(2.0 * modern_dsp::PI * phase));
		}
	}

	[[nodiscard]] static float oscSample(SynthPatch::Wave wave, double phase, double dt, double pw, ModernRng& rng) noexcept
	{
		switch (wave)
		{
		case SynthPatch::Wave::Saw: return modern_dsp::sawBlep(phase, dt);
		case SynthPatch::Wave::Square: return modern_dsp::squareBlep(phase, dt, pw);
		case SynthPatch::Wave::Triangle: return modern_dsp::triangleNaive(phase);
		case SynthPatch::Wave::Sine: return static_cast<float>(std::sin(2.0 * modern_dsp::PI * phase));
		case SynthPatch::Wave::Noise:
		default: return rng.nextSigned();
		}
	}

	void renderVoice(Voice& v, const Channel& ch, const BlockParams& bp, float* out, int len) const
	{
		const SynthPatch& p = ch.patch;
		const double rate = static_cast<double>(m_rate);

		// LFO は 1 ブロックに 1 回だけ読む。S&H は周期ごとに値を替える
		const double lfoInc = static_cast<double>(p.lfoRateHz) * static_cast<double>(len) / rate;
		const double lfoBefore = v.lfoPhase;
		v.lfoPhase += lfoInc;
		const bool wrapped = v.lfoPhase >= 1.0;
		modern_dsp::wrapPhase(v.lfoPhase);
		const float lfo = lfoValue(p.lfoWave, lfoBefore, v, wrapped);
		const float fade = (p.lfoDelaySec <= 0.0f) ? 1.0f
			: std::clamp(static_cast<float>(v.elapsedSec) / p.lfoDelaySec, 0.0f, 1.0f);
		v.elapsedSec += static_cast<double>(len) / rate;

		const double centsMod = static_cast<double>(bp.tune + bp.lfoPitch * lfo * fade);
		const double f0 = modern_dsp::midiToHz(static_cast<double>(v.key)) * modern_dsp::centsToRatio(centsMod);
		const double pw = std::clamp(static_cast<double>(bp.pulseWidth + bp.lfoPulseWidth * lfo * fade), 0.02, 0.98);

		// フィルターの係数。エンベロープは直前のサンプルの値を使う
		const double semis = static_cast<double>(bp.filterEnv * v.filterLevel + bp.lfoFilter * lfo * fade)
			+ static_cast<double>(p.keyTrack) * static_cast<double>(v.key - 60);
		const double cutoff = std::exp2(static_cast<double>(bp.cutoffLog2) + semis / 12.0);
		v.filter[0].setup(cutoff, bp.resonance, rate);
		v.filter[1].setup(cutoff, bp.resonance, rate);
		const auto ftype = static_cast<modern_dsp::StateVariableFilter::Type>(static_cast<int>(p.filterType));

		// ユニゾンの各声の周波数と定位
		std::array<double, MAX_UNISON> inc1{};
		std::array<float, MAX_UNISON> gl{};
		std::array<float, MAX_UNISON> gr{};
		const int n = v.unison;
		for (int i = 0; i < n; ++i)
		{
			const double pos = (n == 1) ? 0.0 : (static_cast<double>(i) / static_cast<double>(n - 1)) * 2.0 - 1.0;
			const double ratio = modern_dsp::centsToRatio(pos * static_cast<double>(bp.unisonDetune));
			inc1[static_cast<std::size_t>(i)] = std::min(f0 * ratio / rate, 0.45);
			const float pan = bp.pan + static_cast<float>(pos) * bp.spread * 63.5f;
			modern_dsp::panGains(pan, gl[static_cast<std::size_t>(i)], gr[static_cast<std::size_t>(i)]);
		}
		const double inc2 = std::min(f0 * modern_dsp::centsToRatio(static_cast<double>(bp.detune))
			* modern_dsp::centsToRatio(static_cast<double>(v.tune2) * 100.0) / rate, 0.45);
		const double incSub = std::min(f0 * 0.5 / rate, 0.45);
		float panL = 0.0f;
		float panR = 0.0f;
		modern_dsp::panGains(bp.pan, panL, panR);
		const float unisonScale = 1.0f / std::sqrt(static_cast<float>(n));
		const float mixNorm = 1.0f / (1.0f + bp.mix2);
		const float tremolo = bp.lfoAmp * fade;

		for (int i = 0; i < len; ++i)
		{
			const float ampLevel = v.amp.next();
			v.filterLevel = v.filterEnv.next();
			if (!v.amp.active())
			{
				v.active = false;
				return;
			}

			float left = 0.0f;
			float right = 0.0f;
			if (n == 1)
			{
				float s = oscSample(v.wave1, v.phase1[0], inc1[0], pw, v.rng);
				if (bp.mix2 > 0.0f)
				{
					s = (s + bp.mix2 * oscSample(v.wave2, v.phase2, inc2, pw, v.rng)) * mixNorm;
				}
				left = s;
				right = s;
			}
			else
			{
				for (int k = 0; k < n; ++k)
				{
					const auto ki = static_cast<std::size_t>(k);
					float s = oscSample(v.wave1, v.phase1[ki], inc1[ki], pw, v.rng) * unisonScale;
					if (bp.mix2 > 0.0f && k == n / 2)
					{
						s = (s + bp.mix2 * oscSample(v.wave2, v.phase2, inc2, pw, v.rng)) * mixNorm;
					}
					// 等パワーの定位。中央のとき 1/√2 になるので √2 倍して、モノラルの声と音量をそろえる
					left += s * gl[ki] * 1.41421356f;
					right += s * gr[ki] * 1.41421356f;
				}
			}
			if (bp.sub > 0.0f)
			{
				const float sub = bp.sub * static_cast<float>(std::sin(2.0 * modern_dsp::PI * v.phaseSub));
				left += sub;
				right += sub;
			}
			if (bp.noise > 0.0f)
			{
				const float nz = bp.noise * v.rng.nextSigned();
				left += nz;
				right += nz;
			}

			left = v.filter[0].process(left, ftype);
			right = v.filter[1].process(right, ftype);

			const float trem = 1.0f - tremolo * (0.5f - 0.5f * lfo);
			const float a = ampLevel * trem * bp.level;
			if (n == 1)
			{
				out[i * 2] += left * a * panL * 1.41421356f;
				out[i * 2 + 1] += right * a * panR * 1.41421356f;
			}
			else
			{
				out[i * 2] += left * a;
				out[i * 2 + 1] += right * a;
			}

			for (int k = 0; k < n; ++k)
			{
				auto& ph = v.phase1[static_cast<std::size_t>(k)];
				ph += inc1[static_cast<std::size_t>(k)];
				ph -= std::floor(ph);
			}
			v.phase2 += inc2;
			v.phase2 -= std::floor(v.phase2);
			v.phaseSub += incSub;
			v.phaseSub -= std::floor(v.phaseSub);
		}
	}

	std::uint32_t m_rate;
	std::array<SynthPatch, CHANNELS> m_configured{};
	std::array<Channel, CHANNELS> m_channels{};
	std::uint64_t m_age = 0;
};

} // namespace mitiru_mml
