/// @file TestModernDrums.cpp
/// @brief ドラムシンセ (キック、スネア、ハイハット、クラップ) を数字で確かめる

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Analysis.hpp"

#include <mitiru_mml/DrumSynthDriver.hpp>
#include <mitiru_mml/MultiChipSequencer.hpp>

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

/// @brief ドラムのチャンネル 1 本に MML を割り当てて描く。最後に長い休符と X を置いて、減衰の尾を含める
StereoBuffer renderDrum(const std::string& mml, int channel)
{
	MultiChipSequencer seq(rawOptions());
	const int chip = seq.addChip(std::make_unique<DrumSynthDriver>());
	seq.addTrack(chip, channel, mml + " R1 R1 Xvol=127", 0);
	return seq.render();
}

/// @brief 指定した区間の最大の振幅 (dBFS)
double peakDb(const std::vector<float>& x, std::size_t from, std::size_t count)
{
	double p = 0.0;
	for (std::size_t i = from; i < std::min(x.size(), from + count); ++i)
	{
		p = std::max(p, std::fabs(static_cast<double>(x[i])));
	}
	return toDb(p);
}

/// @brief 上向きのゼロ交差の間隔から、周期ごとの周波数を求める
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

double median(std::vector<double> v)
{
	std::sort(v.begin(), v.end());
	return v.empty() ? 0.0 : v[v.size() / 2];
}

} // namespace

TEST_CASE("Drum kick sweeps from about 130 Hz down to about 46 Hz and decays in half a second", "[mml][modern][drums]")
{
	const auto mono = monoOf(renderDrum("T120 O4 C16", 0));
	const double startHz = median(cycleFrequencies(mono, 200, 900, 44100.0));
	const double endHz = median(cycleFrequencies(mono, 11025, 22050, 44100.0));
	INFO("kick: start " << startHz << " Hz, end " << endHz << " Hz");
	CHECK(startHz > 115.0);
	CHECK(startHz < 175.0);
	CHECK_THAT(endHz, Catch::Matchers::WithinAbs(46.0, 3.0));

	const double p0 = peakDb(mono, 0, 4410);
	const double p250 = peakDb(mono, 11025, 4410);
	const double p450 = peakDb(mono, 19845, 2205);
	const double p600 = peakDb(mono, 26460, 4410);
	INFO("kick peak dB: 0 " << p0 << ", 250ms " << p250 << ", 450ms " << p450 << ", 600ms " << p600);
	CHECK(p0 > -6.0);
	CHECK(p0 < 0.0);
	CHECK(p250 < p0 - 20.0);
	CHECK(p450 < p0 - 50.0);
	CHECK(p600 < p0 - 60.0);
	// ほとんどのエネルギーは 200 Hz 未満にある
	CHECK(bandRatio(spectrumOf(mono, 0, 16384, 44100.0), 20.0, 200.0) > 0.9);
}

TEST_CASE("Drum tune and decay parameters move the kick", "[mml][modern][drums]")
{
	const auto base = monoOf(renderDrum("T120 O4 C16", 0));
	const auto up = monoOf(renderDrum("T120 O4 Xtune=1200 C16", 0));
	const auto shortDecay = monoOf(renderDrum("T120 O4 Xdec=120 C16", 0));
	const double endUp = median(cycleFrequencies(up, 11025, 22050, 44100.0));
	INFO("tuned up an octave: end " << endUp << " Hz");
	CHECK_THAT(endUp, Catch::Matchers::WithinAbs(92.0, 6.0));
	CHECK(peakDb(shortDecay, 11025, 4410) < peakDb(base, 11025, 4410) - 25.0);
}

TEST_CASE("Drum snare is noisy and bright with a body around 200 Hz", "[mml][modern][drums]")
{
	const auto mono = monoOf(renderDrum("T120 O4 C16", 1));
	const auto early = spectrumOf(mono, 0, 4096, 44100.0);
	INFO("snare centroid " << centroidHzOf(early));
	CHECK(centroidHzOf(early) > 1500.0);
	CHECK(bandRatio(early, 1500.0, 20000.0) > 0.6);
	CHECK(rmsDbOf(mono, 0, 4410) > -22.0);
	CHECK(rmsDbOf(mono, 13230, 2205) < rmsDbOf(mono, 0, 4410) - 40.0);
	// 本体の音 (150〜260 Hz) に山がある
	const auto body = spectrumOf(mono, 300, 4096, 44100.0);
	CHECK(bandRatio(body, 150.0, 260.0) > 0.03);
}

TEST_CASE("Drum closed hat is bright and short and open hat rings longer", "[mml][modern][drums]")
{
	const auto closed = monoOf(renderDrum("T120 O4 C16", 2));
	const auto open = monoOf(renderDrum("T120 O4 C16", 7));
	const auto spec = spectrumOf(closed, 0, 2048, 44100.0);
	INFO("closed hat: centroid " << centroidHzOf(spec) << " Hz, above 5 kHz " << bandRatio(spec, 5000.0, 22050.0));
	CHECK(centroidHzOf(spec) > 7000.0);
	CHECK(bandRatio(spec, 5000.0, 22050.0) > 0.85);
	CHECK(peakDb(closed, 6615, 2205) < peakDb(closed, 0, 2205) - 40.0);
	CHECK(peakDb(open, 6615, 2205) > peakDb(open, 0, 2205) - 30.0);
	CHECK(peakDb(open, 26460, 2205) < peakDb(open, 0, 2205) - 40.0);
}

TEST_CASE("Drum closed hat chokes a ringing open hat", "[mml][modern][drums]")
{
	MultiChipSequencer seq(rawOptions());
	const int chip = seq.addChip(std::make_unique<DrumSynthDriver>());
	seq.addTrack(chip, 7, "T120 O4 C16 R1 R1 Xvol=127", 0);
	seq.addTrack(chip, 2, "T120 O4 R16 C16 R1 R1", 0);
	const auto mixed = monoOf(seq.render());
	MultiChipSequencer solo(rawOptions());
	const int chip2 = solo.addChip(std::make_unique<DrumSynthDriver>());
	solo.addTrack(chip2, 7, "T120 O4 C16 R1 R1 Xvol=127", 0);
	const auto open = monoOf(solo.render());
	// 閉じるハイハットは 16 分音符 (0.125 秒) の位置。そのあと 100 ms 後には、開いたままの場合より十分に小さい
	const std::size_t at = 5512 + 4410;
	CHECK(peakDb(mixed, at + 2205, 2205) < peakDb(open, at + 2205, 2205) - 10.0);
}

TEST_CASE("Drum clap has several quick bursts before its tail", "[mml][modern][drums]")
{
	const auto mono = monoOf(renderDrum("T120 O4 C16", 6));
	const auto env = windowedRmsDb(mono, 88); // 2 ミリ秒
	int peaks = 0;
	for (std::size_t i = 2; i + 2 < 24 && i + 2 < env.size(); ++i)
	{
		if (env[i] > env[i - 1] + 2.0 && env[i] > env[i + 1] && env[i] > -40.0)
		{
			++peaks;
		}
	}
	INFO("clap bursts in the first 48 ms: " << peaks);
	CHECK(peaks >= 3);
	CHECK(rmsDbOf(mono, 0, 4410) > -22.0);
}

TEST_CASE("Drum kit channel maps GM keys to the same sounds as the type channels", "[mml][modern][drums]")
{
	const auto kitKick = renderDrum("T120 O2 C16", DrumSynthDriver::KIT_CHANNEL);
	const auto typeKick = renderDrum("T120 O4 C16", 0);
	const auto kitSnare = renderDrum("T120 O2 D16", DrumSynthDriver::KIT_CHANNEL);
	const auto typeSnare = renderDrum("T120 O4 C16", 1);
	const auto kitHat = renderDrum("T120 O2 F+16", DrumSynthDriver::KIT_CHANNEL);
	const auto typeHat = renderDrum("T120 O4 C16", 2);
	CHECK(kitKick.samples == typeKick.samples);
	CHECK(kitSnare.samples == typeSnare.samples);
	CHECK(kitHat.samples == typeHat.samples);
	// 割り当てのない鍵は無音
	CHECK(peakOfStereo(renderDrum("T120 O1 F+16", DrumSynthDriver::KIT_CHANNEL)) == 0.0);
}

TEST_CASE("Drum toms follow the key and the rhythm MML plays the six drums", "[mml][modern][drums]")
{
	const auto low = monoOf(renderDrum("T120 O2 F16", DrumSynthDriver::KIT_CHANNEL));  // 41
	const auto high = monoOf(renderDrum("T120 O3 D16", DrumSynthDriver::KIT_CHANNEL)); // 50
	const double lowHz = median(cycleFrequencies(low, 4410, 11025, 44100.0));
	const double highHz = median(cycleFrequencies(high, 4410, 11025, 44100.0));
	INFO("toms " << lowHz << " Hz and " << highHz << " Hz");
	CHECK(highHz > lowHz * 1.5);

	MultiChipSequencer seq(rawOptions());
	const int chip = seq.addChip(std::make_unique<DrumSynthDriver>());
	seq.addRhythmTrack(chip, 0, "T120 L4 B S H F Y I");
	seq.addTrack(chip, 6, "R1 R1 R2 Xvol=127", 0);
	const auto mono = monoOf(seq.render());
	for (int i = 0; i < 6; ++i)
	{
		const std::size_t at = static_cast<std::size_t>(i) * 22050;
		INFO("hit " << i);
		CHECK(peakDb(mono, at, 2205) > -20.0);
	}
}

TEST_CASE("Drum synth is deterministic and its drums are independent", "[mml][modern][drums][determinism]")
{
	const auto build = [](bool kick, bool snare)
	{
		MultiChipSequencer seq(rawOptions());
		const int chip = seq.addChip(std::make_unique<DrumSynthDriver>());
		if (kick) seq.addTrack(chip, 0, "T150 L8 C R C R C C R C R1 R1 Xvol=127", 0);
		if (snare) seq.addTrack(chip, 1, "T150 L8 R C R C R C R C R1 R1 Xvol=127", 0);
		return seq.render();
	};
	const auto a = build(true, true);
	const auto b = build(true, true);
	CHECK(hashFloats(a.samples) == hashFloats(b.samples));
	const auto k = build(true, false);
	const auto s = build(false, true);
	double worst = 0.0;
	for (std::size_t i = 0; i < a.samples.size(); ++i)
	{
		const double x = (i < k.samples.size() ? static_cast<double>(k.samples[i]) : 0.0)
			+ (i < s.samples.size() ? static_cast<double>(s.samples[i]) : 0.0);
		worst = std::max(worst, std::fabs(static_cast<double>(a.samples[i]) - x));
	}
	CHECK(worst < 1.0e-5);
}

TEST_CASE("Every drum stays inside the full scale at the loudest volume", "[mml][modern][drums]")
{
	for (int type = 0; type < 8; ++type)
	{
		const auto out = renderDrum("T120 O4 V15 C16 R16 C16", type);
		const double peak = peakOfStereo(out);
		INFO("drum " << type << " peak " << peak);
		CHECK(peak > 0.05);
		CHECK(peak < 1.0);
	}
}
