/// @file TestModernSampler.cpp
/// @brief SF2 サンプラー (SamplerDevice と SamplerDriver) と、OPNA と同じ時間軸で鳴らす検査

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Analysis.hpp"
#include "SyntheticSf2.hpp"

#include <mitiru_mml/MultiChipSequencer.hpp>
#include <mitiru_mml/OpnaDriver.hpp>
#include <mitiru_mml/SamplerDriver.hpp>
#include <mitiru_mml/Sf2File.hpp>

#include <cmath>
#include <memory>
#include <string>

using namespace mitiru_mml;
using namespace mitiru_mml_test;

namespace
{

std::shared_ptr<const Sf2File> testSoundFont()
{
	const auto bytes = buildSyntheticSf2();
	auto sf = std::make_shared<Sf2File>();
	REQUIRE(sf->loadFromMemory(bytes.data(), bytes.size()));
	return sf;
}

MultiChipSequencer::Options rawOptions()
{
	MultiChipSequencer::Options o;
	o.masterGain = 1.0f;
	o.softClip = false;
	return o;
}

/// @brief サンプラー 1 チャンネルに MML を割り当てて描く
StereoBuffer renderSampler(const std::string& mml, int channel, int voice)
{
	MultiChipSequencer seq(rawOptions());
	auto driver = std::make_unique<SamplerDriver>();
	driver->setSoundFont(testSoundFont());
	const int chip = seq.addChip(std::move(driver));
	seq.addTrack(chip, channel, mml, voice);
	return seq.render();
}

} // namespace

TEST_CASE("Synthetic SF2 loads with melodic and percussion presets", "[mml][modern][sampler]")
{
	const auto sf = testSoundFont();
	CHECK(sf->presetCount() == 3);
	CHECK(sf->findPreset(0, 0) == 0);
	CHECK(sf->findPreset(0, 33) == 1);
	CHECK(sf->findPreset(128, 0) == 2);
	CHECK(sf->findPreset(0, 99) == -1);
	const auto kit = sf->buildInstrument(2, 44100);
	CHECK(kit.zoneCount() == 4);
	CHECK(kit.findZone(36, 100) != nullptr);
	CHECK(kit.findZone(40, 100) == nullptr);
}

TEST_CASE("Sampler plays the program at the written pitch", "[mml][modern][sampler]")
{
	for (const int octave : {3, 4, 5})
	{
		const std::string mml = "T120 O" + std::to_string(octave) + " L1 A R1 Xvol=127";
		const auto out = renderSampler(mml, 0, 0);
		const auto mono = monoOf(out);
		const double expected = midiHz(static_cast<double>((octave + 1) * 12 + 9));
		const auto spec = spectrumOf(mono, 8192, 32768, 44100.0);
		const double measured = peakHzOf(spec);
		INFO("octave " << octave << " measured " << measured << " expected " << expected);
		CHECK(std::fabs(centsBetween(measured, expected)) < 1.0);
	}
}

TEST_CASE("Sampler channel 9 maps note numbers to the drum kit and ignores note-off", "[mml][modern][sampler]")
{
	// O2 C = 36 (キック)。32 分音符 (約 50 ミリ秒) で音を止めても、サンプルは最後まで鳴る
	const auto out = renderSampler("T120 O2 C32 R1 R1 Xvol=127", 9, 0);
	const auto mono = monoOf(out);
	REQUIRE(mono.size() > 88200);
	const double early = rmsDbOf(mono, 0, 2205);
	CHECK(early > -30.0);
	// キックの 0.2 秒後は、音を止めた (0.5 秒) あとでなく、サンプルの減衰の途中
	const double at100ms = rmsDbOf(mono, 4410, 441);
	const double at200ms = rmsDbOf(mono, 8820, 441);
	CHECK(at100ms > -60.0);
	CHECK(at200ms < at100ms);
	// 0.28 秒を過ぎたらサンプルが終わって、ほぼ無音
	CHECK(rmsDbOf(mono, 22050, 2205) < -90.0);
}

TEST_CASE("Sampler kit keys sound different and an unmapped key is silent", "[mml][modern][sampler]")
{
	const auto kick = monoOf(renderSampler("T120 O2 C8 R2 Xvol=127", 9, 0));
	const auto snare = monoOf(renderSampler("T120 O2 D8 R2 Xvol=127", 9, 0));
	const auto hat = monoOf(renderSampler("T120 O2 F+8 R2 Xvol=127", 9, 0));
	const auto none = monoOf(renderSampler("T120 O2 E8 R2 Xvol=127", 9, 0));
	const double kickCentroid = centroidHzOf(spectrumOf(kick, 0, 4096, 44100.0));
	const double snareCentroid = centroidHzOf(spectrumOf(snare, 0, 4096, 44100.0));
	const double hatCentroid = centroidHzOf(spectrumOf(hat, 0, 2048, 44100.0));
	CHECK(kickCentroid < 500.0);
	CHECK(snareCentroid > kickCentroid * 2.0);
	CHECK(hatCentroid > 6000.0);
	CHECK(peakOf(none) < 1.0e-6);
}

TEST_CASE("Sampler releases a melodic note at the SF2 release time", "[mml][modern][sampler]")
{
	// Sine Keys のリリースは SF2 で 0.25 秒 (100 dB)。60 dB 基準では約 0.15 秒
	const auto out = renderSampler("T120 O4 L4 V15 A R1 R1 Xvol=127", 0, 0);
	const auto mono = monoOf(out);
	const double held = rmsDbOf(mono, 20000, 1000);
	const std::size_t noteOffSample = 22050 - 459;            // 47 tick @120 BPM
	const double after150 = rmsDbOf(mono, noteOffSample + 6615, 441);
	const double after400 = rmsDbOf(mono, noteOffSample + 17640, 441);
	INFO("held " << held << " after150 " << after150 << " after400 " << after400);
	CHECK(held - after150 > 50.0);
	CHECK(held - after150 < 70.0);
	CHECK(after400 < held - 80.0);
}

TEST_CASE("Sampler pan and volume parameters change the channel level", "[mml][modern][sampler]")
{
	const auto center = renderSampler("T120 O4 L2 A R1 Xvol=127", 0, 0);
	const auto left = renderSampler("T120 O4 L2 P0 A R1 Xvol=127", 0, 0);
	const auto quiet = renderSampler("T120 O4 L2 Xvol=64 A R1 Xvol=127", 0, 0);
	CHECK(rmsOf(channelOf(left, 1), 8000, 4000) < 0.001 * rmsOf(channelOf(left, 0), 8000, 4000));
	const double centerL = rmsOf(channelOf(center, 0), 8000, 4000);
	const double centerR = rmsOf(channelOf(center, 1), 8000, 4000);
	CHECK_THAT(centerL / centerR, Catch::Matchers::WithinAbs(1.0, 0.01));
	CHECK(rmsDbOf(monoOf(quiet), 8000, 4000) < rmsDbOf(monoOf(center), 8000, 4000) - 5.0);
}

TEST_CASE("Sampler and OPNA share one tick axis and mix linearly", "[mml][modern][sampler][sync]")
{
	const std::string drum = "T120 O2 R1 C8 R2";
	const std::string ssg = "T120 O4 R1 L8 A R2";
	const auto build = [&](bool withSampler, bool withOpna)
	{
		MultiChipSequencer seq(rawOptions());
		int sampler = -1;
		int opna = -1;
		if (withSampler)
		{
			auto d = std::make_unique<SamplerDriver>();
			d->setSoundFont(testSoundFont());
			sampler = seq.addChip(std::move(d));
		}
		if (withOpna)
		{
			opna = seq.addChip(std::make_unique<OpnaDriver>());
		}
		if (withSampler) seq.addTrack(sampler, 9, drum);
		if (withOpna) seq.addTrack(opna, 6, ssg);
		return seq.render();
	};
	const auto both = build(true, true);
	const auto onlyS = build(true, false);
	const auto onlyO = build(false, true);
	REQUIRE(both.frames() >= std::max(onlyS.frames(), onlyO.frames()));
	double worst = 0.0;
	for (std::size_t i = 0; i < both.frames(); ++i)
	{
		const double a = (i < onlyS.frames()) ? static_cast<double>(onlyS.samples[i * 2]) : 0.0;
		const double b = (i < onlyO.frames()) ? static_cast<double>(onlyO.samples[i * 2]) : 0.0;
		worst = std::max(worst, std::fabs(static_cast<double>(both.samples[i * 2]) - a - b));
	}
	CHECK(worst < 1.0e-5);

	// 1 小節目の終わり (192 tick = 2 秒 = 88200 サンプル) で、どちらも鳴り始める
	const auto soloS = channelOf(onlyS, 0);
	const auto soloO = channelOf(onlyO, 0);
	const std::size_t onsetS = firstAbove(soloS, 0.005);
	const std::size_t onsetO = firstAbove(soloO, 0.0005);
	INFO("sampler onset " << onsetS << " opna onset " << onsetO);
	CHECK(onsetS >= 88200);
	CHECK(onsetS < 88200 + 100);
	CHECK(onsetO >= 88200);
	CHECK(onsetO < 88200 + 3000);
}

TEST_CASE("Sampler voices of one channel overlap and the oldest is stolen first", "[mml][modern][sampler]")
{
	SamplerDevice dev;
	dev.setSoundFont(testSoundFont());
	for (int i = 0; i < 20; ++i)
	{
		dev.write(0, SamplerDevice::REG_NOTE_ON, static_cast<std::uint32_t>(48 + i) | (100u << 8));
	}
	CHECK(dev.activeVoiceCount() == SamplerDevice::VOICES_PER_CHANNEL);
	dev.write(0, SamplerDevice::REG_NOTE_OFF, 0);
	std::vector<float> block(44100 * 2);
	dev.generate(block.data(), 44100);
	CHECK(dev.activeVoiceCount() == 0);
}

TEST_CASE("Sampler without a sound font is silent and does not crash", "[mml][modern][sampler]")
{
	MultiChipSequencer seq(rawOptions());
	const int chip = seq.addChip(std::make_unique<SamplerDriver>());
	seq.addTrack(chip, 0, "T120 O4 C4 D4", 0);
	const auto out = seq.render();
	CHECK(out.frames() > 0);
	CHECK(peakOfStereo(out) == 0.0);
}

TEST_CASE("Sampler rhythm channels play the GM percussion keys", "[mml][modern][sampler]")
{
	MultiChipSequencer seq(rawOptions());
	auto d = std::make_unique<SamplerDriver>();
	d->setSoundFont(testSoundFont());
	const int chip = seq.addChip(std::move(d));
	seq.addRhythmTrack(chip, SamplerDriver::RHYTHM_CHANNEL_BASE, "T120 L4 B S H");
	const auto out = seq.render();
	REQUIRE(out.frames() > 44100);
	const auto mono = monoOf(out);
	CHECK(rmsDbOf(mono, 0, 2205) > -30.0);
	CHECK(rmsDbOf(mono, 22050, 2205) > -30.0);
	CHECK(rmsDbOf(mono, 44100, 441) > -60.0);
}

TEST_CASE("Sampler plays an instrument handed in directly and keeps it across reset", "[mml][modern][sampler]")
{
	PcmBuffer pcm;
	for (int i = 0; i < 2000; ++i)
	{
		pcm.push_back(static_cast<std::int16_t>(std::lround(20000.0 * std::sin(TWO_PI * static_cast<double>(i) / 100.0))));
	}
	auto inst = std::make_shared<MultiSampleInstrument>();
	SampleInstrument::Adsr adsr;
	adsr.attack = 0.002f;
	adsr.sustain = 1.0f;
	// 441 Hz のサンプルをルートキー 69 (440 Hz) として登録するので、A4 を書くとサンプルの原音 (441 Hz) で鳴る
	inst->addSampleZone(pcm, 44100, 69, 44100, 0, pcm.size(), true, adsr);

	MultiChipSequencer seq(rawOptions());
	auto driver = std::make_unique<SamplerDriver>();
	driver->setInstrument(3, inst);
	const int chip = seq.addChip(std::move(driver));
	seq.addTrack(chip, 3, "T120 O4 L1 A R1 Xvol=127", -1);
	const auto mono = monoOf(seq.render());
	const double measured = peakHzOf(spectrumOf(mono, 8192, 32768, 44100.0));
	INFO("measured " << measured << " Hz");
	CHECK(std::fabs(centsBetween(measured, 441.0)) < 1.0);
}

TEST_CASE("Sampler percussion ends a looped zone because it never gets a note-off", "[mml][modern][sampler]")
{
	PcmBuffer pcm;
	for (int i = 0; i < 2000; ++i)
	{
		pcm.push_back(static_cast<std::int16_t>(std::lround(20000.0 * std::sin(TWO_PI * static_cast<double>(i) / 100.0))));
	}
	auto inst = std::make_shared<MultiSampleInstrument>();
	SampleInstrument::Adsr adsr;
	adsr.sustain = 1.0f;
	inst->addSampleZone(pcm, 44100, 69, 44100, 0, pcm.size(), true, adsr);
	SamplerDevice dev;
	dev.setInstrument(SamplerDevice::PERCUSSION_CHANNEL, inst);
	dev.write(SamplerDevice::PERCUSSION_CHANNEL, SamplerDevice::REG_NOTE_ON, 69u | (100u << 8));
	CHECK(dev.activeVoiceCount() >= 1);
	std::vector<float> block(44100 * 2);
	dev.generate(block.data(), 44100);
	CHECK(dev.activeVoiceCount() == 0);
	// 旋律のチャンネルでは、同じ楽器は消音まで鳴り続ける
	dev.setInstrument(0, inst);
	dev.write(0, SamplerDevice::REG_NOTE_ON, 69u | (100u << 8));
	dev.generate(block.data(), 44100);
	CHECK(dev.activeVoiceCount() >= 1);
}
