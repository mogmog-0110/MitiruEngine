/// @file TestChipOpl.cpp
/// @brief YM3526 (OPL)、YM3812 (OPL2)、YMF262 (OPL3) の Device と Driver の検査

#include <catch2/catch_test_macros.hpp>

#include "ChipHashes.hpp"
#include "ChipPitch.hpp"
#include "ChipSongs.hpp"

#include <mitiru_mml/MultiChipSequencer.hpp>
#include <mitiru_mml/OplDriver.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>

using namespace mitiru_mml;
using namespace mitiru_mml_test;

namespace
{

/// @brief 基音だけが鳴る音色 (変調器は TL 最大で無音、搬送波は正弦波の持続音)
OplVoice pureSineOpl(int carrierWaveform = 0)
{
	OplVoice voice;
	voice.additive = false;
	voice.feedback = 0;
	voice.modulator.totalLevel = 63;
	voice.carrier.totalLevel = 0;
	voice.carrier.sustain = true;
	voice.carrier.attackRate = 15;
	voice.carrier.sustainLevel = 0;
	voice.carrier.waveform = static_cast<std::uint8_t>(carrierWaveform);
	return voice;
}

DriverTone measureOplSine(OplVariant variant, int key, int channel = 0)
{
	OplDriver driver(variant);
	driver.setFmVoice(channel, pureSineOpl());
	driver.setVolume(channel, 15);
	driver.noteOn(channel, key, 15);
	return measureSounding(driver);
}

const char* variantName(OplVariant v)
{
	switch (v)
	{
	case OplVariant::Opl: return "opl";
	case OplVariant::Opl2: return "opl2";
	case OplVariant::Opl3: return "opl3";
	}
	return "";
}

std::unique_ptr<MultiChipSequencer> buildOplSong(OplVariant variant)
{
	auto seq = std::make_unique<MultiChipSequencer>();
	const int chip = seq->addChip(std::make_unique<OplDriver>(variant));
	for (const auto& t : OPL_SONG)
	{
		seq->addTrack(chip, t.channel, t.mml, t.voice);
	}
	if (variant == OplVariant::Opl3)
	{
		seq->addTrack(chip, OPL3_EXTRA_TRACK.channel, OPL3_EXTRA_TRACK.mml, OPL3_EXTRA_TRACK.voice);
	}
	return seq;
}

} // namespace

TEST_CASE("OplDriver picks an F-Number between 512 and 1023 for the middle of the range", "[mml][opl]")
{
	for (int key = 36; key <= 96; ++key)
	{
		const auto fb = OplDriver::keyToFnumBlock(key);
		INFO("key " << key);
		REQUIRE(fb.fnum >= 512);
		REQUIRE(fb.fnum <= 1023);
		REQUIRE(fb.block <= 7);
		// 選んだ値が作る周波数は 12 平均律から、丸めの上限の範囲に収まる
		const double cents = centsBetween(OplDriver::fnumBlockToHz(fb), keyHz(key));
		REQUIRE(std::abs(cents) <= OplDriver::pitchErrorBoundCents(fb) + 1.0e-9);
	}
	// A4 = 440 Hz は block 4、F-Number 580
	const auto a4 = OplDriver::keyToFnumBlock(69);
	REQUIRE(a4.block == 4);
	REQUIRE(a4.fnum == 580);
}

TEST_CASE("OplDriver pitch matches equal temperament within one cent for C and A in octaves 3 to 6", "[mml][opl][pitch]")
{
	for (const OplVariant variant : {OplVariant::Opl, OplVariant::Opl2, OplVariant::Opl3})
	{
		for (int octave = 3; octave <= 6; ++octave)
		{
			for (const int semitone : {0, 9})
			{
				const int key = (octave + 1) * 12 + semitone;
				const DriverTone t = measureOplSine(variant, key);
				INFO(variantName(variant) << " key " << key << " measured " << t.peakHz << " Hz");
				REQUIRE(std::abs(centsBetween(t.peakHz, keyHz(key))) <= 1.0);
			}
		}
	}
}

TEST_CASE("OplDriver measured pitch of every semitone equals the chip frequency of the chosen F-Number", "[mml][opl][pitch]")
{
	// 10bit の F-Number では、G# と A# が 12 平均律から約 1.4 セントずれる (ハードウェアの精度の限界)。
	// ずれは丸めの上限の範囲に収まり、測った周波数は選んだ F-Number のとおりであることを見る。
	for (int key = 48; key <= 95; ++key)
	{
		const auto fb = OplDriver::keyToFnumBlock(key);
		const double chipHz = OplDriver::fnumBlockToHz(fb);
		const DriverTone t = measureOplSine(OplVariant::Opl2, key);
		INFO("key " << key << " chip " << chipHz << " measured " << t.peakHz);
		REQUIRE(std::abs(centsBetween(t.peakHz, chipHz)) <= 0.3);
		REQUIRE(std::abs(centsBetween(t.peakHz, keyHz(key))) <= OplDriver::pitchErrorBoundCents(fb) + 0.3);
	}
}

TEST_CASE("OplDriver OPL3 second bank channels play the same pitch", "[mml][opl][pitch]")
{
	for (const int channel : {9, 12, 17})
	{
		const DriverTone t = measureOplSine(OplVariant::Opl3, 69, channel);
		INFO("channel " << channel);
		REQUIRE(std::abs(centsBetween(t.peakHz, 440.0)) <= 1.0);
	}
}

TEST_CASE("OplDriver OPL2 waveform select works through the enable bit", "[mml][opl]")
{
	// 波形 2 は全波整流した正弦波で、基音が 1 オクターブ上がって聞こえる
	OplDriver driver(OplVariant::Opl2);
	driver.setFmVoice(0, pureSineOpl(2));
	driver.noteOn(0, 69, 15);
	const DriverTone t = measureSounding(driver);
	REQUIRE(std::abs(centsBetween(t.peakHz, 880.0)) <= 2.0);
}

TEST_CASE("OplDriver presets sound, do not clip, and fade after note off", "[mml][opl]")
{
	for (const OplVariant variant : {OplVariant::Opl, OplVariant::Opl2, OplVariant::Opl3})
	{
		for (int preset = 0; preset < opl_presets::COUNT; ++preset)
		{
			OplDriver driver(variant);
			driver.setVoice(0, preset);
			driver.setVolume(0, 15);
			driver.noteOn(0, 57, 15);
			const DriverTone on = measureSounding(driver, 0.02);
			INFO(variantName(variant) << " preset " << preset);
			REQUIRE(on.rmsDb > -50.0);
			REQUIRE(on.peakDb < -0.5);

			driver.noteOff(0);
			std::vector<float> tail(2 * 49716);
			driver.device().generate(tail.data(), 49716);
			const DriverTone off = measureSounding(driver, 0.0);
			REQUIRE(off.rmsDb < on.rmsDb - 6.0);
		}
	}
}

TEST_CASE("OplDriver volume steps are monotonic and 3 dB apart", "[mml][opl]")
{
	double previous = -300.0;
	for (const int volume : {3, 6, 9, 12, 15})
	{
		OplDriver driver(OplVariant::Opl2);
		driver.setFmVoice(0, pureSineOpl());
		driver.setVolume(0, volume);
		driver.noteOn(0, 69, volume);
		const double db = measureSounding(driver).rmsDb;
		INFO("volume " << volume << " rms " << db);
		REQUIRE(db > previous);
		if (previous > -299.0)
		{
			REQUIRE(std::abs((db - previous) - 9.0) < 1.5);
		}
		previous = db;
	}
}

TEST_CASE("OplDriver does not key on at volume zero", "[mml][opl]")
{
	OplDriver driver(OplVariant::Opl2);
	driver.setFmVoice(0, pureSineOpl());
	driver.setVolume(0, 0);
	driver.noteOn(0, 69, 0);
	REQUIRE(measureSounding(driver).rmsDb < -80.0);
}

TEST_CASE("OplDriver keys on again when the volume comes back during a held note", "[mml][opl]")
{
	OplDriver driver(OplVariant::Opl2);
	driver.setFmVoice(0, pureSineOpl());
	driver.setVolume(0, 0);
	driver.noteOn(0, 69, 0);
	driver.setVolume(0, 15);
	REQUIRE(measureSounding(driver).rmsDb > -30.0);

}

TEST_CASE("OplDriver OPL3 pan sends the sound to one side and the others stay centered", "[mml][opl]")
{
	for (const int pan : {0, 2})
	{
		OplDriver driver(OplVariant::Opl3);
		driver.setFmVoice(0, pureSineOpl());
		driver.setPan(0, pan);
		driver.noteOn(0, 69, 15);
		std::vector<float> out(2 * 20000);
		driver.device().generate(out.data(), 20000);
		double left = 0.0;
		double right = 0.0;
		for (std::size_t i = 4000; i < 20000; ++i)
		{
			left += std::abs(out[i * 2]);
			right += std::abs(out[i * 2 + 1]);
		}
		INFO("pan " << pan << " left " << left << " right " << right);
		if (pan == 0)
		{
			REQUIRE(left > 1.0);
			REQUIRE(right < left * 0.001);
		}
		else
		{
			REQUIRE(right > 1.0);
			REQUIRE(left < right * 0.001);
		}
	}

	OplDriver center(OplVariant::Opl3);
	center.setFmVoice(0, pureSineOpl());
	center.noteOn(0, 69, 15);
	std::vector<float> out(2 * 20000);
	center.device().generate(out.data(), 20000);
	double diff = 0.0;
	for (std::size_t i = 4000; i < 20000; ++i)
	{
		diff += std::abs(out[i * 2] - out[i * 2 + 1]);
	}
	REQUIRE(diff < 1.0e-3);
}

TEST_CASE("OplDriver channel counts depend on the chip", "[mml][opl]")
{
	REQUIRE(OplDriver(OplVariant::Opl).channelCount() == 9);
	REQUIRE(OplDriver(OplVariant::Opl2).channelCount() == 9);
	REQUIRE(OplDriver(OplVariant::Opl3).channelCount() == 18);

	MultiChipSequencer seq;
	const int opl2 = seq.addChip(std::make_unique<OplDriver>(OplVariant::Opl2));
	const int opl3 = seq.addChip(std::make_unique<OplDriver>(OplVariant::Opl3));
	REQUIRE_THROWS_AS(seq.addTrack(opl2, 9, "C"), std::invalid_argument);
	REQUIRE_NOTHROW(seq.addTrack(opl3, 17, "C"));
	REQUIRE_THROWS_AS(seq.addTrack(opl3, 18, "C"), std::invalid_argument);
}

TEST_CASE("OplDevice native rates and names", "[mml][opl]")
{
	OplDevice opl(OplVariant::Opl);
	OplDevice opl2(OplVariant::Opl2);
	OplDevice opl3(OplVariant::Opl3);
	REQUIRE(std::string(opl.name()) == "YM3526");
	REQUIRE(std::string(opl2.name()) == "YM3812");
	REQUIRE(std::string(opl3.name()) == "YMF262");
	REQUIRE(opl.nativeRate() == 49715);
	REQUIRE(opl2.nativeRate() == 49715);
	REQUIRE(opl3.nativeRate() == 49715);
}

TEST_CASE("OplDevice saves and restores its state", "[mml][opl]")
{
	for (const OplVariant variant : {OplVariant::Opl, OplVariant::Opl2, OplVariant::Opl3})
	{
		OplDriver a(variant);
		a.setVoice(0, 0);
		a.noteOn(0, 60, 15);
		std::vector<float> skip(2 * 5000);
		a.device().generate(skip.data(), 5000);
		const auto state = a.device().saveState();
		REQUIRE_FALSE(state.empty());

		OplDriver b(variant);
		REQUIRE(b.device().loadState(state));
		std::vector<float> outA(2 * 2000);
		std::vector<float> outB(2 * 2000);
		a.device().generate(outA.data(), 2000);
		b.device().generate(outB.data(), 2000);
		REQUIRE(outA == outB);
	}
}

TEST_CASE("OplDriver songs render the same PCM every time and match the recorded hash", "[mml][opl][hash]")
{
	for (const OplVariant variant : {OplVariant::Opl, OplVariant::Opl2, OplVariant::Opl3})
	{
		const std::string name = std::string(variantName(variant)) + "_song";
		auto first = buildOplSong(variant);
		auto second = buildOplSong(variant);
		const RenderedHash h1 = hashRender(*first);
		const RenderedHash h2 = hashRender(*second);
		INFO(name << " frames " << h1.frames << " sha256 " << h1.sha256);
		REQUIRE(h1.frames > 44100u * 3u);
		REQUIRE(h1.sha256 == h2.sha256);

		const ChipHashEntry* expected = findChipHash(name);
		REQUIRE(expected != nullptr);
		REQUIRE(h1.frames == expected->frames);
		REQUIRE(h1.sha256 == expected->sha256);
	}
}

TEST_CASE("OPL song is not silent and stays under full scale", "[mml][opl]")
{
	for (const OplVariant variant : {OplVariant::Opl, OplVariant::Opl2, OplVariant::Opl3})
	{
		auto seq = buildOplSong(variant);
		const StereoBuffer buf = seq->render();
		float peak = 0.0f;
		for (const float s : buf.samples)
		{
			peak = std::max(peak, std::abs(s));
		}
		INFO(variantName(variant));
		REQUIRE(peak > 0.05f);
		REQUIRE(peak <= 1.0f);
	}
}

TEST_CASE("Print OPL hashes", "[.emit]")
{
	for (const OplVariant variant : {OplVariant::Opl, OplVariant::Opl2, OplVariant::Opl3})
	{
		auto seq = buildOplSong(variant);
		const RenderedHash h = hashRender(*seq);
		std::cout << "EMIT {\"" << variantName(variant) << "_song\", " << h.frames << "u, \"" << h.sha256 << "\"},\n";
	}
}

TEST_CASE("Print OPL pitch table", "[.report]")
{
	std::cout << "REPORT opl2 key expectedHz chipHz measuredHz centsVs12TET fnum block\n";
	for (int key = 36; key <= 96; ++key)
	{
		const auto fb = OplDriver::keyToFnumBlock(key);
		const DriverTone t = measureOplSine(OplVariant::Opl2, key);
		std::cout << "REPORT opl2 " << key << " " << keyHz(key) << " " << OplDriver::fnumBlockToHz(fb) << " "
			<< t.peakHz << " " << centsBetween(t.peakHz, keyHz(key)) << " " << fb.fnum << " "
			<< static_cast<int>(fb.block) << "\n";
	}
}
