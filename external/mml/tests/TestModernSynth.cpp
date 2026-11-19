/// @file TestModernSynth.cpp
/// @brief 減算シンセ (オシレーター、デチューン、フィルター、エンベロープ、LFO、supersaw) を数字で確かめる

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Analysis.hpp"

#include <mitiru_mml/MultiChipSequencer.hpp>
#include <mitiru_mml/SynthDriver.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace mitiru_mml;
using namespace mitiru_mml_test;

namespace
{

MultiChipSequencer::Options rawOptions()
{
	MultiChipSequencer::Options o;
	o.masterGain = 1.0f;
	o.softClip = false;
	return o;
}

/// @brief 何も加工しない素直な音色 (フィルターを開き、エンベロープは持続音)
SynthPatch plainPatch(SynthPatch::Wave wave)
{
	SynthPatch p;
	p.wave1 = wave;
	p.cutoffHz = 20000.0f;
	p.resonance = 0.0f;
	p.keyTrack = 0.0f;
	p.ampAttack = 0.002f;
	p.ampDecay = 0.0f;
	p.ampSustain = 1.0f;
	p.ampRelease = 0.01f;
	p.gain = 0.5f;
	return p;
}

/// @brief 音色を channel 0 に置いて MML を描く。MML の最後に X を置いて、リリースの尾を含める
StereoBuffer renderSynth(const SynthPatch& patch, const std::string& mml)
{
	MultiChipSequencer seq(rawOptions());
	auto driver = std::make_unique<SynthDriver>();
	driver->setPatch(0, patch);
	const int chip = seq.addChip(std::move(driver));
	seq.addTrack(chip, 0, mml, -1);
	return seq.render();
}

/// @brief 正弦波に近い信号の 1 周期ごとの周波数 (上向きのゼロ交差の間隔から求める)
std::vector<double> cycleFrequencies(const std::vector<float>& x, std::size_t from, std::size_t to, double rate)
{
	std::vector<double> crossings;
	for (std::size_t i = std::max<std::size_t>(from, 1); i < std::min(to, x.size()); ++i)
	{
		if (x[i - 1] < 0.0f && x[i] >= 0.0f)
		{
			const double frac = static_cast<double>(-x[i - 1]) / static_cast<double>(x[i] - x[i - 1]);
			crossings.push_back(static_cast<double>(i - 1) + frac);
		}
	}
	std::vector<double> out;
	for (std::size_t i = 1; i < crossings.size(); ++i)
	{
		out.push_back(rate / (crossings[i] - crossings[i - 1]));
	}
	return out;
}

double harmonicDb(const Spectrum& s, double f0, int n)
{
	const double target = f0 * static_cast<double>(n);
	const auto center = static_cast<std::size_t>(std::lround(target / s.binHz));
	double best = 0.0;
	for (std::size_t k = center > 3 ? center - 3 : 0; k <= center + 3 && k < s.mag.size(); ++k)
	{
		best = std::max(best, s.mag[k]);
	}
	return toDb(best);
}

} // namespace

TEST_CASE("Synth oscillators play the written pitch within one cent", "[mml][modern][synth]")
{
	const SynthPatch::Wave waves[] = {SynthPatch::Wave::Saw, SynthPatch::Wave::Square,
		SynthPatch::Wave::Triangle, SynthPatch::Wave::Sine};
	for (const auto wave : waves)
	{
		for (const int octave : {2, 3, 4, 5})
		{
			const auto out = renderSynth(plainPatch(wave), "T120 O" + std::to_string(octave) + " L1 A R1 Xvol=127");
			const auto mono = monoOf(out);
			const double expected = midiHz(static_cast<double>((octave + 1) * 12 + 9));
			const double measured = peakHzOf(spectrumOf(mono, 8192, 32768, 44100.0), 30.0);
			INFO("wave " << static_cast<int>(wave) << " octave " << octave << " measured " << measured);
			CHECK(std::fabs(centsBetween(measured, expected)) < 1.0);
		}
	}
}

TEST_CASE("Synth saw is band limited: almost no energy under the fundamental", "[mml][modern][synth]")
{
	// B7 付近 (3951 Hz)。帯域制限が無いと、折り返しが基音より下に落ちてくる
	const auto out = renderSynth(plainPatch(SynthPatch::Wave::Saw), "T120 O7 L1 B R1 Xvol=127");
	const auto mono = monoOf(out);
	const auto spec = spectrumOf(mono, 8192, 32768, 44100.0);
	const double f0 = midiHz(107.0);
	CHECK(bandRatio(spec, 100.0, f0 * 0.8) < 1.0e-4); // 基音のエネルギーに対する比ではなく全体比。-40 dB
	INFO("below-fundamental energy ratio " << bandRatio(spec, 100.0, f0 * 0.8));
}

TEST_CASE("Synth pulse width sets the harmonic balance", "[mml][modern][synth]")
{
	const double f0 = midiHz(57.0); // A3
	const auto measure = [&](const std::string& pw)
	{
		const auto out = renderSynth(plainPatch(SynthPatch::Wave::Square), "T120 O3 L1 " + pw + " A R1 Xvol=127");
		const auto mono = monoOf(out);
		return toDb(toneAmplitude(mono, 8192, 32768, f0 * 2.0, 44100.0)) - toDb(toneAmplitude(mono, 8192, 32768, f0, 44100.0));
	};
	CHECK(measure("Xpw=50") < -50.0);
	// パルス幅 25% の 2 倍音は 1 倍音より 3 dB 低い (|sin(2πd)|/2 と sin(πd) の比)
	CHECK_THAT(measure("Xpw=25"), Catch::Matchers::WithinAbs(-3.0, 1.0));
}

TEST_CASE("Synth detune of two oscillators beats at the frequency difference", "[mml][modern][synth]")
{
	SynthPatch p = plainPatch(SynthPatch::Wave::Sine);
	p.wave2 = SynthPatch::Wave::Sine;
	p.mix2 = 1.0f;
	p.detuneCents = 20.0f;
	const auto out = renderSynth(p, "T120 O4 L1 A R1 Xvol=127");
	const auto mono = monoOf(out);
	const double beatHz = 440.0 * (std::pow(2.0, 20.0 / 1200.0) - 1.0);
	// 10 ミリ秒ごとの RMS の山の間隔 = うなりの周期
	const auto env = windowedRmsDb(mono, 441);
	std::vector<double> peaks;
	for (std::size_t i = 2; i + 2 < env.size() && i < 190; ++i)
	{
		if (env[i] > env[i - 1] && env[i] >= env[i + 1] && env[i] > env[i - 2] && env[i] > env[i + 2])
		{
			peaks.push_back(static_cast<double>(i) * 0.01);
		}
	}
	REQUIRE(peaks.size() >= 5);
	const double period = (peaks.back() - peaks.front()) / static_cast<double>(peaks.size() - 1);
	INFO("beat expected " << beatHz << " Hz, measured " << 1.0 / period << " Hz");
	CHECK(std::fabs(1.0 / period - beatHz) < 0.4);
}

TEST_CASE("Synth low-pass filter rolls off at 12 dB per octave", "[mml][modern][synth]")
{
	const double f0 = midiHz(45.0); // A2 = 110 Hz
	SynthPatch open = plainPatch(SynthPatch::Wave::Saw);
	SynthPatch closed = open;
	closed.cutoffHz = 1000.0f;
	const auto a = spectrumOf(monoOf(renderSynth(open, "T120 O2 L1 A R1 Xvol=127")), 8192, 32768, 44100.0);
	const auto b = spectrumOf(monoOf(renderSynth(closed, "T120 O2 L1 A R1 Xvol=127")), 8192, 32768, 44100.0);
	// 1 kHz の 2 オクターブ上 (4 kHz 付近 = 36 倍音 3960 Hz) は、約 24 dB 下がる (2 極)
	const double drop = harmonicDb(a, f0, 36) - harmonicDb(b, f0, 36);
	INFO("drop at 3960 Hz: " << drop << " dB");
	CHECK_THAT(drop, Catch::Matchers::WithinAbs(24.0, 3.0));
	// カットオフより十分下は変わらない
	CHECK(std::fabs(harmonicDb(a, f0, 2) - harmonicDb(b, f0, 2)) < 0.8);
	// 高域を通すフィルターは逆に、低い音を落とす
	SynthPatch hp = open;
	hp.filterType = SynthPatch::FilterType::HighPass;
	hp.cutoffHz = 2000.0f;
	const auto c = spectrumOf(monoOf(renderSynth(hp, "T120 O2 L1 A R1 Xvol=127")), 8192, 32768, 44100.0);
	CHECK(harmonicDb(a, f0, 2) - harmonicDb(c, f0, 2) > 30.0);
}

TEST_CASE("Synth filter resonance raises the level at the cutoff", "[mml][modern][synth]")
{
	const double f0 = midiHz(45.0);
	SynthPatch flat = plainPatch(SynthPatch::Wave::Saw);
	flat.cutoffHz = 1100.0f; // 10 倍音 (1100 Hz) に合わせる
	SynthPatch peaky = flat;
	peaky.resonance = 0.8f;
	const auto a = spectrumOf(monoOf(renderSynth(flat, "T120 O2 L1 A R1 Xvol=127")), 8192, 32768, 44100.0);
	const auto b = spectrumOf(monoOf(renderSynth(peaky, "T120 O2 L1 A R1 Xvol=127")), 8192, 32768, 44100.0);
	const double boost = harmonicDb(b, f0, 10) - harmonicDb(a, f0, 10);
	INFO("resonance boost " << boost << " dB");
	CHECK(boost > 9.0);
}

TEST_CASE("Synth filter ramp opens the sound: the centroid rises across the note", "[mml][modern][synth]")
{
	SynthPatch p = plainPatch(SynthPatch::Wave::Saw);
	p.cutoffHz = 200.0f;
	const auto out = renderSynth(p, "T120 O3 L1 Xcutoff=200>8000/1 A R1 Xvol=127");
	const auto mono = monoOf(out);
	std::vector<double> centroids;
	for (int i = 0; i < 4; ++i)
	{
		centroids.push_back(centroidHzOf(spectrumOf(mono, static_cast<std::size_t>(i) * 22050 + 2048, 16384, 44100.0)));
	}
	INFO("centroids " << centroids[0] << " " << centroids[1] << " " << centroids[2] << " " << centroids[3]);
	for (std::size_t i = 1; i < centroids.size(); ++i)
	{
		CHECK(centroids[i] > centroids[i - 1] * 1.3);
	}
	CHECK(centroids[3] > centroids[0] * 3.5);
}

TEST_CASE("Synth envelope attack and release follow the set times", "[mml][modern][synth]")
{
	SynthPatch p = plainPatch(SynthPatch::Wave::Sine);
	const auto out = renderSynth(p, "T120 O4 L2 Xatk=100 Xsus=100 Xrel=200 A R1 Xvol=127");
	const auto mono = monoOf(out);
	// アタック: 直線に上がり、約 100 ms で最大。90% の到達はおよそ 90 ms
	double peak = 0.0;
	for (std::size_t i = 4410; i < 8820; ++i) peak = std::max(peak, std::fabs(static_cast<double>(mono[i])));
	const std::size_t reach90 = firstAbove(mono, peak * 0.9);
	INFO("attack: 90% at " << reach90 << " samples (" << reach90 / 44.1 << " ms)");
	CHECK(reach90 > 3500);
	CHECK(reach90 < 4500);
	// リリース: 2 分音符 (96 tick の手前まで) のあと 200 ms で -60 dB
	const std::size_t offAt = 95 * 22050 / 48; // 95 tick
	const double held = rmsDbOf(mono, offAt - 2000, 1000);
	const double r100 = rmsDbOf(mono, offAt + 4410 - 220, 441);
	const double r200 = rmsDbOf(mono, offAt + 8820 - 220, 441);
	INFO("release: held " << held << " dB, +100ms " << r100 << ", +200ms " << r200);
	CHECK_THAT(held - r100, Catch::Matchers::WithinAbs(30.0, 4.0));
	CHECK_THAT(held - r200, Catch::Matchers::WithinAbs(60.0, 5.0));
}

TEST_CASE("Synth vibrato moves the pitch by the set depth at the set rate", "[mml][modern][synth]")
{
	SynthPatch p = plainPatch(SynthPatch::Wave::Sine);
	p.lfoRateHz = 6.0f;
	p.lfoPitchCents = 50.0f;
	const auto out = renderSynth(p, "T120 O4 L1 A R1 Xvol=127");
	const auto mono = monoOf(out);
	const auto freqs = cycleFrequencies(mono, 4410, 80000, 44100.0);
	REQUIRE(freqs.size() > 200);
	const double lo = *std::min_element(freqs.begin(), freqs.end());
	const double hi = *std::max_element(freqs.begin(), freqs.end());
	const double up = centsBetween(hi, 440.0);
	const double down = centsBetween(lo, 440.0);
	INFO("vibrato range " << down << " .. " << up << " cents");
	CHECK_THAT(up, Catch::Matchers::WithinAbs(50.0, 8.0));
	CHECK_THAT(down, Catch::Matchers::WithinAbs(-50.0, 8.0));
	// 周波数の系列が平均を横切る回数から LFO の速さを出す
	const double mean = 0.5 * (lo + hi);
	int crossings = 0;
	for (std::size_t i = 1; i < freqs.size(); ++i)
	{
		if ((freqs[i - 1] - mean) * (freqs[i] - mean) < 0.0) ++crossings;
	}
	const double seconds = (80000.0 - 4410.0) / 44100.0;
	const double lfoHz = static_cast<double>(crossings) / 2.0 / seconds;
	INFO("lfo " << lfoHz << " Hz");
	CHECK_THAT(lfoHz, Catch::Matchers::WithinAbs(6.0, 0.6));
}

TEST_CASE("Synth LFO delay lets the first part of the note stay in tune", "[mml][modern][synth]")
{
	SynthPatch p = plainPatch(SynthPatch::Wave::Sine);
	p.lfoRateHz = 6.0f;
	p.lfoPitchCents = 50.0f;
	p.lfoDelaySec = 0.5f;
	const auto out = renderSynth(p, "T120 O4 L1 A R1 Xvol=127");
	const auto mono = monoOf(out);
	const auto early = cycleFrequencies(mono, 1500, 6000, 44100.0);
	const auto late = cycleFrequencies(mono, 60000, 80000, 44100.0);
	const auto spread = [](const std::vector<double>& f)
	{
		return centsBetween(*std::max_element(f.begin(), f.end()), *std::min_element(f.begin(), f.end()));
	};
	CHECK(spread(early) < 0.35 * spread(late));
}

TEST_CASE("Supersaw is wide: left and right differ, and it is as loud as one saw", "[mml][modern][synth]")
{
	SynthPatch one = plainPatch(SynthPatch::Wave::Saw);
	SynthPatch super = one;
	super.unison = 7;
	super.unisonDetune = 24.0f;
	super.spread = 0.9f;
	const auto a = renderSynth(one, "T120 O3 L1 A R1 Xvol=127");
	const auto b = renderSynth(super, "T120 O3 L1 A R1 Xvol=127");
	const double corrOne = stereoCorrelation(a, 8192, 32768);
	const double corrSuper = stereoCorrelation(b, 8192, 32768);
	INFO("correlation: saw " << corrOne << ", supersaw " << corrSuper);
	CHECK(corrOne > 0.999);
	CHECK(corrSuper < 0.6);
	const double loudOne = rmsDbOf(monoOf(a), 8192, 32768);
	const double loudSuper = rmsDbOf(monoOf(b), 8192, 32768);
	INFO("rms: saw " << loudOne << " dB, supersaw " << loudSuper << " dB");
	CHECK(std::fabs(loudOne - loudSuper) < 4.0);
	// 左右の音量はそろう
	const double l = rmsDbOf(channelOf(b, 0), 8192, 32768);
	const double r = rmsDbOf(channelOf(b, 1), 8192, 32768);
	CHECK(std::fabs(l - r) < 2.0);
	// 離調を広げると、基音のまわり (上下 1%) に残るエネルギーの割合が下がる
	SynthPatch wide = super;
	wide.unisonDetune = 60.0f;
	const auto c = renderSynth(wide, "T120 O3 L1 A R1 Xvol=127");
	const auto specOne = spectrumOf(monoOf(a), 8192, 32768, 44100.0);
	const auto specWide = spectrumOf(monoOf(c), 8192, 32768, 44100.0);
	const double f0 = midiHz(57.0);
	const double nearOne = bandRatio(specOne, f0 * 0.99, f0 * 1.01);
	const double nearWide = bandRatio(specWide, f0 * 0.99, f0 * 1.01);
	INFO("energy near the fundamental: saw " << nearOne << ", wide supersaw " << nearWide);
	CHECK(nearWide < 0.8 * nearOne);
}

TEST_CASE("Synth sub oscillator adds energy one octave below", "[mml][modern][synth]")
{
	SynthPatch p = plainPatch(SynthPatch::Wave::Saw);
	SynthPatch withSub = p;
	withSub.sub = 1.0f;
	const double f0 = midiHz(57.0);
	const auto a = spectrumOf(monoOf(renderSynth(p, "T120 O3 L1 A R1 Xvol=127")), 8192, 32768, 44100.0);
	const auto b = spectrumOf(monoOf(renderSynth(withSub, "T120 O3 L1 A R1 Xvol=127")), 8192, 32768, 44100.0);
	const double half = f0 * 0.5;
	const auto level = [&](const Spectrum& s)
	{
		const auto k = static_cast<std::size_t>(std::lround(half / s.binHz));
		return toDb(std::max({s.mag[k - 1], s.mag[k], s.mag[k + 1]}));
	};
	CHECK(level(b) - level(a) > 40.0);
}

TEST_CASE("Synth plays a chord and steals the oldest voice beyond eight", "[mml][modern][synth]")
{
	const auto out = renderSynth(plainPatch(SynthPatch::Wave::Sine), "T120 O4 L1 'CEG' R1 Xvol=127");
	const auto spec = spectrumOf(monoOf(out), 8192, 32768, 44100.0);
	for (const int key : {60, 64, 67})
	{
		const auto k = static_cast<std::size_t>(std::lround(midiHz(static_cast<double>(key)) / spec.binHz));
		const double level = toDb(std::max({spec.mag[k - 1], spec.mag[k], spec.mag[k + 1]}));
		CHECK(level > toDb(spec.mag[static_cast<std::size_t>(std::lround(midiHz(62.0) / spec.binHz))]) + 30.0);
	}

	SynthDevice dev;
	for (int i = 0; i < 12; ++i)
	{
		dev.write(0, SynthDevice::REG_NOTE_ON, static_cast<std::uint32_t>(48 + i));
	}
	CHECK(dev.activeVoiceCount() == SynthDevice::VOICES_PER_CHANNEL);
}

TEST_CASE("Synth is deterministic and channels are independent", "[mml][modern][synth][determinism]")
{
	SynthPatch noisy = synthPreset(8); // riser: ノイズ
	SynthPatch saw = synthPreset(2);   // supersaw: 初期位相が乱数
	const auto build = [&](bool both, bool second)
	{
		MultiChipSequencer seq(rawOptions());
		auto driver = std::make_unique<SynthDriver>();
		driver->setPatch(0, noisy);
		driver->setPatch(1, saw);
		const int chip = seq.addChip(std::move(driver));
		if (both || !second) seq.addTrack(chip, 0, "T140 O4 L8 CDEFGAB>C R1 Xvol=127", -1);
		if (both || second) seq.addTrack(chip, 1, "T140 O3 L4 CEG>C R1 Xvol=127", -1);
		return seq.render();
	};
	const auto a1 = build(true, false);
	const auto a2 = build(true, false);
	CHECK(hashFloats(a1.samples) == hashFloats(a2.samples));
	// 片方のチャンネルだけを描いた結果の和が、両方を描いた結果と一致する (乱数がチャンネルごとに独立)
	const auto only0 = build(false, false);
	const auto only1 = build(false, true);
	REQUIRE(a1.samples.size() >= only0.samples.size());
	double worst = 0.0;
	for (std::size_t i = 0; i < a1.samples.size(); ++i)
	{
		const double x = (i < only0.samples.size() ? static_cast<double>(only0.samples[i]) : 0.0)
			+ (i < only1.samples.size() ? static_cast<double>(only1.samples[i]) : 0.0);
		worst = std::max(worst, std::fabs(static_cast<double>(a1.samples[i]) - x));
	}
	CHECK(worst < 1.0e-5);
}

TEST_CASE("Synth presets render sound that stays inside the full scale", "[mml][modern][synth]")
{
	for (int i = 0; i < SYNTH_PRESET_COUNT; ++i)
	{
		MultiChipSequencer seq(rawOptions());
		const int chip = seq.addChip(std::make_unique<SynthDriver>());
		seq.addTrack(chip, 0, "T120 O3 L4 V15 CEG>C R1 Xvol=127", i);
		const auto out = seq.render();
		const double peak = peakOfStereo(out);
		INFO("preset " << i << " " << synthPreset(i).name << " peak " << peak);
		CHECK(peak > 0.02);
		CHECK(peak < 1.0);
	}
}
