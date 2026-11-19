/// @file TestChipOpm.cpp
/// @brief YM2151 (OPM) の Device と Driver の検査 (音の高さ、プリセット、音量と左右、決定論)

#include <catch2/catch_test_macros.hpp>

#include "ChipHashes.hpp"
#include "ChipPitch.hpp"
#include "ChipSongs.hpp"

#include <mitiru_mml/MultiChipSequencer.hpp>
#include <mitiru_mml/OpmDriver.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>

using namespace mitiru_mml;
using namespace mitiru_mml_test;

namespace
{

/// @brief 基音だけが鳴る音色 (アルゴリズム 7 の op1 だけを出力する持続音)
OpmVoice pureSineOpm()
{
	OpmVoice voice;
	voice.algorithm = 7;
	voice.feedback = 0;
	for (int op = 0; op < 4; ++op)
	{
		OpmOpParams& p = voice.ops[op];
		p.multiple = 1;
		p.totalLevel = (op == 0) ? 0 : 127;
		p.attackRate = 31;
		p.decay1Rate = 0;
		p.decay2Rate = 0;
		p.sustainLevel = 0;
		p.releaseRate = 7;
	}
	return voice;
}

DriverTone measureOpmSine(int key)
{
	OpmDriver driver;
	driver.setFmVoice(0, pureSineOpm());
	driver.setVolume(0, 15);
	driver.noteOn(0, key, 15);
	return measureSounding(driver);
}

std::unique_ptr<MultiChipSequencer> buildOpmSong()
{
	auto seq = std::make_unique<MultiChipSequencer>();
	const int opm = seq->addChip(std::make_unique<OpmDriver>());
	for (const auto& t : OPM_SONG)
	{
		seq->addTrack(opm, t.channel, t.mml, t.voice);
	}
	return seq;
}

} // namespace

TEST_CASE("OpmDriver maps MIDI keys to the OPM key code with octaves starting at C sharp", "[mml][opm]")
{
	// A4 (key 69) は オクターブ 4、音名コード 10
	REQUIRE(OpmDriver::keyToKeyCode(69).kc == ((4 << 4) | 10));
	// C#4 (key 61) はオクターブ 4 の先頭 (コード 0)
	REQUIRE(OpmDriver::keyToKeyCode(61).kc == ((4 << 4) | 0));
	// C4 (key 60) は 1 つ下のオクターブ 3 の最後 (コード 14)
	REQUIRE(OpmDriver::keyToKeyCode(60).kc == ((3 << 4) | 14));
	// 1 オクターブ上げるとオクターブ番号が 1 増える
	for (int key = 25; key <= 96; ++key)
	{
		const auto lo = OpmDriver::keyToKeyCode(key);
		const auto hi = OpmDriver::keyToKeyCode(key + 12);
		REQUIRE((hi.kc & 0x0F) == (lo.kc & 0x0F));
		REQUIRE((hi.kc >> 4) == (lo.kc >> 4) + 1);
	}
	// 音名コードに 3、7、11、15 は現れない
	for (int key = 13; key <= 108; ++key)
	{
		const int code = OpmDriver::keyToKeyCode(key).kc & 0x0F;
		REQUIRE(code != 3);
		REQUIRE(code != 7);
		REQUIRE(code != 11);
		REQUIRE(code != 15);
	}
}

TEST_CASE("OpmDriver pitch matches equal temperament within one cent for C and A in octaves 3 to 6", "[mml][opm][pitch]")
{
	for (int octave = 3; octave <= 6; ++octave)
	{
		for (const int semitone : {0, 9})
		{
			const int key = (octave + 1) * 12 + semitone;
			const double expected = keyHz(key);
			const DriverTone t = measureOpmSine(key);
			INFO("key " << key << " expected " << expected << " Hz measured " << t.peakHz << " Hz");
			REQUIRE(std::abs(centsBetween(t.peakHz, expected)) <= 1.0);
		}
	}
}

TEST_CASE("OpmDriver pitch of every semitone from C2 to C7 stays within one cent", "[mml][opm][pitch]")
{
	// ymfm の位相増分表は 32 刻みで、低いオクターブでは右シフトで下位ビットが落ちるが、この範囲では 1 セントに収まる
	for (int key = 36; key <= 96; ++key)
	{
		const DriverTone t = measureOpmSine(key);
		const double cents = centsBetween(t.peakHz, keyHz(key));
		INFO("key " << key << " cents " << cents);
		REQUIRE(std::abs(cents) <= 1.0);
	}
}

TEST_CASE("OpmDriver pitch doubles with every octave", "[mml][opm][pitch]")
{
	double previous = measureOpmSine(45).peakHz;
	for (int key = 57; key <= 105; key += 12)
	{
		const double current = measureOpmSine(key).peakHz;
		INFO("key " << key);
		REQUIRE(std::abs(centsBetween(current, previous * 2.0)) <= 2.0);
		previous = current;
	}
}

TEST_CASE("OpmDriver presets sound, do not clip, and fade after note off", "[mml][opm]")
{
	for (int preset = 0; preset < opm_presets::COUNT; ++preset)
	{
		OpmDriver driver;
		driver.setVoice(0, preset);
		driver.setVolume(0, 15);
		driver.noteOn(0, 57, 15);
		const DriverTone on = measureSounding(driver, 0.05);
		INFO("preset " << preset);
		REQUIRE(on.rmsDb > -50.0);
		REQUIRE(on.peakDb < -0.5);

		driver.noteOff(0);
		std::vector<float> tail(2 * 55930);
		driver.device().generate(tail.data(), 55930); // 1 秒のリリース
		const DriverTone off = measureSounding(driver, 0.0);
		REQUIRE(off.rmsDb < on.rmsDb - 6.0);
	}
}

TEST_CASE("OpmDriver preset names resolve to indices", "[mml][opm]")
{
	REQUIRE(opm_presets::findPreset("lead") == 4);
	REQUIRE(opm_presets::findPreset("epiano") == 0);
	REQUIRE(opm_presets::findPreset("unknown") == -1);
}

TEST_CASE("OpmDriver volume steps are monotonic and 3 dB apart", "[mml][opm]")
{
	double previous = -300.0;
	for (const int volume : {3, 6, 9, 12, 15})
	{
		OpmDriver driver;
		driver.setFmVoice(0, pureSineOpm());
		driver.setVolume(0, volume);
		driver.noteOn(0, 69, volume);
		const double db = measureSounding(driver).rmsDb;
		INFO("volume " << volume << " rms " << db);
		REQUIRE(db > previous);
		if (previous > -299.0)
		{
			REQUIRE(std::abs((db - previous) - 9.0) < 1.5); // 3 段 = 9 dB
		}
		previous = db;
	}
}

TEST_CASE("OpmDriver silences a channel at volume zero", "[mml][opm]")
{
	OpmDriver driver;
	driver.setFmVoice(0, pureSineOpm());
	driver.setVolume(0, 0);
	driver.noteOn(0, 69, 0);
	REQUIRE(measureSounding(driver).rmsDb < -80.0);
}

TEST_CASE("OpmDriver pan sends the sound to one side", "[mml][opm]")
{
	for (const int pan : {0, 2})
	{
		OpmDriver driver;
		driver.setFmVoice(0, pureSineOpm());
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
}

TEST_CASE("OpmDriver rejects a channel that does not exist and ignores calls to it", "[mml][opm]")
{
	MultiChipSequencer seq;
	const int opm = seq.addChip(std::make_unique<OpmDriver>());
	REQUIRE_THROWS_AS(seq.addTrack(opm, 8, "C"), std::invalid_argument);
	REQUIRE_THROWS_AS(seq.addTrack(opm, -1, "C"), std::invalid_argument);
	OpmDriver driver;
	driver.noteOn(9, 60, 15);
	driver.setVolume(-3, 5);
	driver.setPan(100, 1);
	SUCCEED();
}

TEST_CASE("OpmDevice saves and restores its state", "[mml][opm]")
{
	OpmDriver a;
	a.setVoice(0, 0);
	a.noteOn(0, 60, 15);
	std::vector<float> skip(2 * 5000);
	a.device().generate(skip.data(), 5000);
	const auto state = a.device().saveState();
	REQUIRE_FALSE(state.empty());

	OpmDriver b;
	REQUIRE(b.device().loadState(state));
	std::vector<float> outA(2 * 2000);
	std::vector<float> outB(2 * 2000);
	a.device().generate(outA.data(), 2000);
	b.device().generate(outB.data(), 2000);
	REQUIRE(outA == outB);
}

TEST_CASE("OpmDriver song renders the same PCM every time", "[mml][opm][hash]")
{
	auto first = buildOpmSong();
	auto second = buildOpmSong();
	const RenderedHash h1 = hashRender(*first);
	const RenderedHash h2 = hashRender(*second);
	REQUIRE(h1.frames > 44100u * 3u);
	REQUIRE(h1.sha256 == h2.sha256);

	const ChipHashEntry* expected = findChipHash("opm_song");
	REQUIRE(expected != nullptr);
	INFO("frames " << h1.frames << " sha256 " << h1.sha256);
	REQUIRE(h1.frames == expected->frames);
	REQUIRE(h1.sha256 == expected->sha256);
}

TEST_CASE("OPM song is not silent and stays under full scale", "[mml][opm]")
{
	auto seq = buildOpmSong();
	const StereoBuffer buf = seq->render();
	float peak = 0.0f;
	double energy = 0.0;
	for (const float s : buf.samples)
	{
		peak = std::max(peak, std::abs(s));
		energy += static_cast<double>(s) * s;
	}
	REQUIRE(peak > 0.05f);
	REQUIRE(peak <= 1.0f);
	REQUIRE(energy / static_cast<double>(buf.samples.size()) > 1.0e-5);
}

TEST_CASE("Print chip hashes", "[.emit]")
{
	auto seq = buildOpmSong();
	const RenderedHash h = hashRender(*seq);
	std::cout << "EMIT {\"opm_song\", " << h.frames << "u, \"" << h.sha256 << "\"},\n";
}

TEST_CASE("Print OPM pitch table", "[.report]")
{
	std::cout << "REPORT opm key expectedHz measuredHz cents\n";
	for (int key = 36; key <= 96; ++key)
	{
		const DriverTone t = measureOpmSine(key);
		std::cout << "REPORT opm " << key << " " << keyHz(key) << " " << t.peakHz << " "
			<< centsBetween(t.peakHz, keyHz(key)) << "\n";
	}
}
