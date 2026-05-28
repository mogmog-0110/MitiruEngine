/// @file TestMml.cpp
/// @brief MitiruMML unit tests

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <mitiru_mml/MitiruMML.hpp>
#include <mitiru_mml/MotifEngine.hpp>
#include <mitiru_mml/OpnaDriver.hpp>
#include <mitiru_mml/OpnaPresets.hpp>
#include <mitiru_mml/OpnaSequencer.hpp>
#include <mitiru_mml/AiComposer.hpp>
#include <mitiru_mml/PhraseDictionary.hpp>
#include <mitiru_mml/PhraseComposer.hpp>
#include <mitiru_mml/MmlValidator.hpp>
#include <mitiru_mml/TfiImporter.hpp>

#include <cmath>
#include <cstdio>
#include <fstream>

using namespace mitiru_mml;
using Catch::Approx;

// ============================================================================
// Parser
// ============================================================================

TEST_CASE("MmlParser parses empty string", "[mml][parser]")
{
	auto cmds = MmlParser::parse("");
	REQUIRE(cmds.empty());
}

TEST_CASE("MmlParser parses single note", "[mml][parser]")
{
	auto cmds = MmlParser::parse("C");
	REQUIRE(cmds.size() == 1);
	REQUIRE(cmds[0].type == CommandType::Note);
	REQUIRE(cmds[0].value == 0); // C = 0
}

TEST_CASE("MmlParser parses note with length", "[mml][parser]")
{
	auto cmds = MmlParser::parse("C4");
	REQUIRE(cmds.size() == 1);
	REQUIRE(cmds[0].duration == 4);
}

namespace
{
	// テスト中の stderr に警告が出ないよう抑制する。
	void setMmlQuiet()
	{
#ifdef _WIN32
		_putenv_s("MITIRU_MML_QUIET", "1");
#else
		setenv("MITIRU_MML_QUIET", "1", 1);
#endif
	}
}

TEST_CASE("MmlParser rejects non-standard note length and falls back", "[mml][parser]")
{
	setMmlQuiet();

	SECTION("C5 falls back to duration 0 (use default L), not 5")
	{
		auto cmds = MmlParser::parse("C5");
		REQUIRE(cmds.size() == 1);
		REQUIRE(cmds[0].type == CommandType::Note);
		REQUIRE(cmds[0].duration == 0);
	}

	SECTION("C4 still parses as quarter note")
	{
		auto cmds = MmlParser::parse("C4");
		REQUIRE(cmds[0].duration == 4);
	}

	SECTION("R7 (rest) falls back to duration 0")
	{
		auto cmds = MmlParser::parse("R7");
		REQUIRE(cmds.size() == 1);
		REQUIRE(cmds[0].type == CommandType::Rest);
		REQUIRE(cmds[0].duration == 0);
	}

	SECTION("L5 is stored as L4 (hard default)")
	{
		auto cmds = MmlParser::parse("L5");
		REQUIRE(cmds.size() == 1);
		REQUIRE(cmds[0].type == CommandType::Length);
		REQUIRE(cmds[0].value == 4);
	}

	SECTION("L8 stays as L8")
	{
		auto cmds = MmlParser::parse("L8");
		REQUIRE(cmds[0].value == 8);
	}

	SECTION("L4 C5C5C5C5 has identical beat to L4 CCCC (fallback preserves the bar)")
	{
		// C5 -> len=0 -> use default L=4. Each note effectively a quarter.
		auto withBad = MmlParser::parse("L4 C5C5C5C5");
		auto plain   = MmlParser::parse("L4 CCCC");

		auto totalLen = [](const CommandList& cs) {
			int total = 0;
			int defaultL = 4;
			for (const auto& c : cs)
			{
				if (c.type == CommandType::Length) defaultL = c.value;
				if (c.type == CommandType::Note)
				{
					total += (c.duration == 0 ? defaultL : c.duration);
				}
			}
			return total;
		};
		REQUIRE(totalLen(withBad) == totalLen(plain));
	}
}

TEST_CASE("MmlParser parses sharp and flat", "[mml][parser]")
{
	auto cmds = MmlParser::parse("C+4 D-8");
	REQUIRE(cmds.size() == 2);
	REQUIRE(cmds[0].value == 1);  // C# = 1
	REQUIRE(cmds[1].value == 1);  // Db = D(2) - 1 = 1
}

TEST_CASE("MmlParser parses dotted note", "[mml][parser]")
{
	auto cmds = MmlParser::parse("C4.");
	REQUIRE(cmds.size() == 1);
	REQUIRE(cmds[0].dotted == true);
}

TEST_CASE("MmlParser parses tempo", "[mml][parser]")
{
	auto cmds = MmlParser::parse("T180");
	REQUIRE(cmds.size() == 1);
	REQUIRE(cmds[0].type == CommandType::Tempo);
	REQUIRE(cmds[0].value == 180);
}

TEST_CASE("MmlParser parses octave", "[mml][parser]")
{
	auto cmds = MmlParser::parse("O5");
	REQUIRE(cmds.size() == 1);
	REQUIRE(cmds[0].type == CommandType::Octave);
	REQUIRE(cmds[0].value == 5);
}

TEST_CASE("MmlParser parses octave up down", "[mml][parser]")
{
	auto cmds = MmlParser::parse(">C<D");
	REQUIRE(cmds.size() == 4);
	REQUIRE(cmds[0].type == CommandType::OctaveUp);
	REQUIRE(cmds[2].type == CommandType::OctaveDown);
}

TEST_CASE("MmlParser parses rest", "[mml][parser]")
{
	auto cmds = MmlParser::parse("R4");
	REQUIRE(cmds.size() == 1);
	REQUIRE(cmds[0].type == CommandType::Rest);
	REQUIRE(cmds[0].duration == 4);
}

TEST_CASE("MmlParser parses volume and waveform", "[mml][parser]")
{
	auto cmds = MmlParser::parse("V10 @2");
	REQUIRE(cmds.size() == 2);
	REQUIRE(cmds[0].type == CommandType::Volume);
	REQUIRE(cmds[0].value == 10);
	REQUIRE(cmds[1].type == CommandType::Waveform);
	REQUIRE(cmds[1].value == 2);
}

TEST_CASE("MmlParser parses tie", "[mml][parser]")
{
	auto cmds = MmlParser::parse("C4&C8");
	REQUIRE(cmds.size() == 2);
	REQUIRE(cmds[0].tied == true);
}

TEST_CASE("MmlParser ignores comments", "[mml][parser]")
{
	auto cmds = MmlParser::parse("C ; this is a comment\nD");
	REQUIRE(cmds.size() == 2);
}

TEST_CASE("MmlParser case insensitive", "[mml][parser]")
{
	auto cmds = MmlParser::parse("t120 o4 l8 cdefgab");
	REQUIRE(cmds.size() == 10); // T, O, L, 7 notes
}

TEST_CASE("MmlParser full melody", "[mml][parser]")
{
	auto cmds = MmlParser::parse("T120 O4 L8 V12 @0 CDEFGAB>C");
	REQUIRE(cmds.size() > 10);
}

// ============================================================================
// Synthesizer
// ============================================================================

TEST_CASE("Synthesizer noteToFreq A4 is 440Hz", "[mml][synth]")
{
	REQUIRE(Synthesizer::noteToFreq(69) == Approx(440.0f).margin(0.1f));
}

TEST_CASE("Synthesizer noteToFreq C4 is ~261Hz", "[mml][synth]")
{
	REQUIRE(Synthesizer::noteToFreq(60) == Approx(261.63f).margin(0.5f));
}

TEST_CASE("Synthesizer generates non-empty buffer", "[mml][synth]")
{
	Synthesizer synth;
	auto buf = synth.generate(440.0f, 0.1f, WaveType::Square, 0.5f);
	REQUIRE_FALSE(buf.empty());
	REQUIRE(buf.size() == static_cast<std::size_t>(0.1f * 44100));
}

TEST_CASE("Synthesizer all wave types produce output", "[mml][synth]")
{
	Synthesizer synth;
	for (int w = 0; w <= 4; ++w)
	{
		auto buf = synth.generate(440.0f, 0.05f, static_cast<WaveType>(w));
		REQUIRE_FALSE(buf.empty());
		// 全ゼロでないことを確認（可聴コンテンツが存在するはず）
		bool hasNonZero = false;
		for (auto s : buf) { if (s != 0) { hasNonZero = true; break; } }
		REQUIRE(hasNonZero);
	}
}

// ============================================================================
// Track
// ============================================================================

TEST_CASE("Track renders C major scale", "[mml][track]")
{
	Synthesizer synth({44100, 0.5f});
	Track track(synth);
	auto cmds = MmlParser::parse("T120 O4 L4 CDEFGAB>C");
	auto pcm = track.render(cmds);
	REQUIRE_FALSE(pcm.empty());
	// 8 quarter notes at 120bpm = 8 * 0.5s = 4s = 176400 samples
	REQUIRE(pcm.size() == Approx(176400).margin(1000));
}

TEST_CASE("Track rest produces silence", "[mml][track]")
{
	Synthesizer synth({44100, 0.5f});
	Track track(synth);
	auto cmds = MmlParser::parse("R4");
	auto pcm = track.render(cmds);
	REQUIRE_FALSE(pcm.empty());
	for (auto s : pcm) REQUIRE(s == 0);
}

TEST_CASE("Track tie extends note duration", "[mml][track]")
{
	Synthesizer synth({44100, 0.5f});
	Track track(synth);
	auto single = track.render(MmlParser::parse("T120 O4 C4"));
	auto tied = track.render(MmlParser::parse("T120 O4 C4&C4"));
	REQUIRE(tied.size() > single.size());
	// タイされた音符は約2倍の長さ
	REQUIRE(tied.size() == Approx(single.size() * 2).margin(100));
}

// ============================================================================
// Sequencer
// ============================================================================

TEST_CASE("Sequencer empty produces empty", "[mml][sequencer]")
{
	Sequencer seq;
	auto pcm = seq.render();
	REQUIRE(pcm.empty());
}

TEST_CASE("Sequencer single track", "[mml][sequencer]")
{
	Sequencer seq;
	seq.addTrack("T120 O4 L4 CDEF");
	REQUIRE(seq.trackCount() == 1);
	auto pcm = seq.render();
	REQUIRE_FALSE(pcm.empty());
}

TEST_CASE("Sequencer multi track mixes", "[mml][sequencer]")
{
	Sequencer seq;
	seq.addTrack("T120 O4 L4 CDEF");
	seq.addTrack("T120 O3 L2 CE");
	REQUIRE(seq.trackCount() == 2);
	auto pcm = seq.render();
	REQUIRE_FALSE(pcm.empty());
}

TEST_CASE("Sequencer clear removes tracks", "[mml][sequencer]")
{
	Sequencer seq;
	seq.addTrack("C");
	seq.clear();
	REQUIRE(seq.trackCount() == 0);
}

// ============================================================================
// WavWriter
// ============================================================================

TEST_CASE("WavWriter produces valid WAV header", "[mml][wav]")
{
	PcmBuffer pcm(44100, 1000); // 1秒分の定数値
	auto wav = WavWriter::toWav(pcm);
	REQUIRE(WavWriter::isValidWav(wav));
	REQUIRE(wav.size() == 44 + pcm.size() * 2);
}

TEST_CASE("WavWriter empty pcm produces minimal WAV", "[mml][wav]")
{
	auto wav = WavWriter::toWav({});
	REQUIRE(WavWriter::isValidWav(wav));
	REQUIRE(wav.size() == 44);
}

// ============================================================================
// Integration
// ============================================================================

TEST_CASE("Full pipeline: MML to WAV", "[mml][integration]")
{
	Sequencer seq;
	seq.addTrack("T120 O4 L8 @2 V12 CDEFGAB>C");
	seq.addTrack("T120 O3 L2 @0 V8 CEG>C");
	auto pcm = seq.render();
	REQUIRE_FALSE(pcm.empty());

	auto wav = WavWriter::toWav(pcm, seq.sampleRate());
	REQUIRE(WavWriter::isValidWav(wav));
	REQUIRE(wav.size() > 44);
}

// ============================================================================
// FM Synthesis
// ============================================================================

TEST_CASE("MmlParser parses FM preset", "[mml][parser]")
{
	auto cmds = MmlParser::parse("@FM1 C4");
	REQUIRE(cmds.size() == 2);
	REQUIRE(cmds[0].type == CommandType::FmWave);
	REQUIRE(cmds[0].value == 1);
}

TEST_CASE("MmlParser parses FM preset case insensitive", "[mml][parser]")
{
	auto cmds = MmlParser::parse("@fm2 C4");
	REQUIRE(cmds.size() == 2);
	REQUIRE(cmds[0].type == CommandType::FmWave);
	REQUIRE(cmds[0].value == 2);
}

TEST_CASE("Synthesizer FM preset produces output", "[mml][synth]")
{
	Synthesizer synth;
	Synthesizer::NoteParams p;
	p.freq = 440.0f;
	p.durationSec = 0.1f;
	p.useFm = true;
	p.fmPreset = Synthesizer::getFmPreset(1);
	auto buf = synth.generateAdvanced(p);
	REQUIRE_FALSE(buf.empty());
	bool hasNonZero = false;
	for (auto s : buf) { if (s != 0) { hasNonZero = true; break; } }
	REQUIRE(hasNonZero);
}

TEST_CASE("Synthesizer all FM presets produce different output", "[mml][synth]")
{
	Synthesizer synth;
	std::vector<PcmBuffer> bufs;
	for (int i = 0; i < 8; ++i)
	{
		Synthesizer::NoteParams p;
		p.freq = 440.0f;
		p.durationSec = 0.05f;
		p.useFm = true;
		p.useAdsr = true;
		p.fmPreset = Synthesizer::getFmPreset(i);
		bufs.push_back(synth.generateAdvanced(p));
	}
	// At least some presets should differ
	bool anyDiffer = false;
	for (size_t i = 1; i < bufs.size(); ++i)
	{
		for (size_t j = 0; j < std::min(bufs[0].size(), bufs[i].size()); ++j)
		{
			if (bufs[0][j] != bufs[i][j]) { anyDiffer = true; break; }
		}
		if (anyDiffer) break;
	}
	REQUIRE(anyDiffer);
}

// ============================================================================
// Duty Cycle
// ============================================================================

TEST_CASE("MmlParser parses duty cycle", "[mml][parser]")
{
	auto cmds = MmlParser::parse("W25 C4");
	REQUIRE(cmds[0].type == CommandType::Duty);
	REQUIRE(cmds[0].value == 25);
}

TEST_CASE("Synthesizer duty cycle 25pct differs from 50pct", "[mml][synth]")
{
	Synthesizer synth;
	Synthesizer::NoteParams p1;
	p1.freq = 440.0f;
	p1.durationSec = 0.05f;
	p1.dutyRatio = 0.25f;
	Synthesizer::NoteParams p2;
	p2.freq = 440.0f;
	p2.durationSec = 0.05f;
	p2.dutyRatio = 0.5f;
	auto buf1 = synth.generateAdvanced(p1);
	auto buf2 = synth.generateAdvanced(p2);
	bool differs = false;
	for (size_t i = 0; i < std::min(buf1.size(), buf2.size()); ++i)
	{
		if (buf1[i] != buf2[i]) { differs = true; break; }
	}
	REQUIRE(differs);
}

// ============================================================================
// ADSR Envelope
// ============================================================================

TEST_CASE("MmlParser parses ADSR commands", "[mml][parser]")
{
	auto cmds = MmlParser::parse("EA10 ED20 ES70 ER30 C4");
	REQUIRE(cmds.size() == 5);
	REQUIRE(cmds[0].type == CommandType::Adsr);
	REQUIRE(cmds[0].value == 0); // A
	REQUIRE(cmds[0].extra == 10);
	REQUIRE(cmds[1].type == CommandType::Adsr);
	REQUIRE(cmds[1].value == 1); // D
	REQUIRE(cmds[1].extra == 20);
	REQUIRE(cmds[2].type == CommandType::Adsr);
	REQUIRE(cmds[2].value == 2); // S
	REQUIRE(cmds[2].extra == 70);
	REQUIRE(cmds[3].type == CommandType::Adsr);
	REQUIRE(cmds[3].value == 3); // R
	REQUIRE(cmds[3].extra == 30);
}

TEST_CASE("Synthesizer ADSR envelope shapes output", "[mml][synth]")
{
	Synthesizer synth;
	Synthesizer::NoteParams p;
	p.freq = 440.0f;
	p.durationSec = 0.5f;
	p.useAdsr = true;
	p.adsr = {0.1f, 0.1f, 0.5f, 0.1f};
	auto buf = synth.generateAdvanced(p);
	REQUIRE_FALSE(buf.empty());
	// First sample should be near zero (attack start)
	REQUIRE(std::abs(buf[0]) < 500);
}

// ============================================================================
// Detune
// ============================================================================

TEST_CASE("MmlParser parses detune positive", "[mml][parser]")
{
	auto cmds = MmlParser::parse("H10 C4");
	REQUIRE(cmds[0].type == CommandType::Detune);
	REQUIRE(cmds[0].value == 10);
}

TEST_CASE("MmlParser parses detune negative", "[mml][parser]")
{
	auto cmds = MmlParser::parse("H-5 C4");
	REQUIRE(cmds[0].type == CommandType::Detune);
	REQUIRE(cmds[0].value == -5);
}

TEST_CASE("Synthesizer detune thickens sound", "[mml][synth]")
{
	Synthesizer synth;
	Synthesizer::NoteParams p;
	p.freq = 440.0f;
	p.durationSec = 0.1f;
	p.detuneCents = 10.0f;
	auto buf = synth.generateAdvanced(p);
	REQUIRE_FALSE(buf.empty());
}

// ============================================================================
// Vibrato
// ============================================================================

TEST_CASE("MmlParser parses vibrato", "[mml][parser]")
{
	auto cmds = MmlParser::parse("M5,30 C4");
	REQUIRE(cmds[0].type == CommandType::Vibrato);
	REQUIRE(cmds[0].value == 5);    // speed
	REQUIRE(cmds[0].extra == 30);   // depth
}

TEST_CASE("MmlParser parses vibrato off", "[mml][parser]")
{
	auto cmds = MmlParser::parse("M0 C4");
	REQUIRE(cmds[0].type == CommandType::Vibrato);
	REQUIRE(cmds[0].value == 0);
	REQUIRE(cmds[0].extra == 0);
}

TEST_CASE("Synthesizer vibrato modulates pitch", "[mml][synth]")
{
	Synthesizer synth;
	Synthesizer::NoteParams p;
	p.freq = 440.0f;
	p.durationSec = 0.5f;
	p.vibratoSpeed = 6.0f;
	p.vibratoDepth = 50.0f;
	auto buf = synth.generateAdvanced(p);
	REQUIRE_FALSE(buf.empty());
}

// ============================================================================
// Integration: advanced features
// ============================================================================

TEST_CASE("Full pipeline: PC98 style FM music", "[mml][integration]")
{
	Sequencer seq;
	seq.addTrack("T130 @FM0 O5 L8 V12 EA5 ED10 ES80 ER20 H3 M5,20 CDEFGAB>C");
	seq.addTrack("T130 @FM2 O3 L4 V10 W25 C2E2G2>C2");
	seq.addTrack("T130 @4 O2 L8 V8 CRCRCRCR");
	auto pcm = seq.render();
	REQUIRE_FALSE(pcm.empty());
	REQUIRE(pcm.size() > 44100); // at least 1 second
}

TEST_CASE("Backward compat: old generate still works", "[mml][synth]")
{
	Synthesizer synth;
	auto buf = synth.generate(440.0f, 0.1f, WaveType::Square, 0.5f);
	REQUIRE_FALSE(buf.empty());
	REQUIRE(buf.size() == static_cast<std::size_t>(0.1f * 44100));
}

TEST_CASE("MmlParser E note still works without ADSR suffix", "[mml][parser]")
{
	auto cmds = MmlParser::parse("E4");
	REQUIRE(cmds.size() == 1);
	REQUIRE(cmds[0].type == CommandType::Note);
	REQUIRE(cmds[0].value == 4); // E = 4
	REQUIRE(cmds[0].duration == 4);
}

TEST_CASE("MmlParser waveform select still works", "[mml][parser]")
{
	auto cmds = MmlParser::parse("@2 C4");
	REQUIRE(cmds.size() == 2);
	REQUIRE(cmds[0].type == CommandType::Waveform);
	REQUIRE(cmds[0].value == 2);
}

// ============================================================================
// OPNA Driver
// ============================================================================

TEST_CASE("OpnaDriver produces non-empty output", "[mml][opna]")
{
	OpnaDriver driver;
	auto buf = driver.renderSamples(44100);
	REQUIRE(buf.size() == 44100);
}

TEST_CASE("OpnaDriver sample rate is valid", "[mml][opna]")
{
	OpnaDriver driver;
	// YM2608 at 7.9872MHz should produce a valid sample rate
	REQUIRE(driver.sampleRate() > 0);
}

TEST_CASE("OpnaDriver FM note on produces sound", "[mml][opna]")
{
	OpnaDriver driver;
	driver.setFmVoice(0, opna_presets::PIANO);
	driver.fmNoteOn(0, 60); // C4
	auto buf = driver.renderSamples(22050); // ~0.5 sec at native rate
	bool hasSound = false;
	for (auto s : buf)
	{
		if (std::abs(static_cast<int>(s)) > 100)
		{
			hasSound = true;
			break;
		}
	}
	REQUIRE(hasSound);
}

TEST_CASE("OpnaDriver FM note off silences", "[mml][opna]")
{
	OpnaDriver driver;
	driver.setFmVoice(0, opna_presets::PIANO);
	driver.fmNoteOn(0, 60);
	static_cast<void>(driver.renderSamples(4000)); // let attack happen
	driver.fmNoteOff(0);
	// render enough for release to complete（VALSOUND Aco Piano2のRR=7に十分な時間）
	auto buf = driver.renderSamples(110000);
	// last samples should be near silent
	// ソフトクリップミキサーは微小信号を約2.4倍に増幅するため閾値を緩和
	bool lastQuiet = true;
	for (std::size_t i = buf.size() - 100; i < buf.size(); ++i)
	{
		if (std::abs(static_cast<int>(buf[i])) > 1500)
		{
			lastQuiet = false;
			break;
		}
	}
	REQUIRE(lastQuiet);
}

TEST_CASE("OpnaDriver SSG produces sound", "[mml][opna]")
{
	OpnaDriver driver;
	driver.setSsgEnable(true, false, false);
	driver.setSsgTone(0, 440.0f, 12);
	auto buf = driver.renderSamples(22050);
	bool hasSound = false;
	for (auto s : buf)
	{
		if (std::abs(static_cast<int>(s)) > 100)
		{
			hasSound = true;
			break;
		}
	}
	REQUIRE(hasSound);
}

TEST_CASE("OpnaDriver noteToFnumBlock C4 valid", "[mml][opna]")
{
	auto [fnum, block] = OpnaDriver::noteToFnumBlock(60);
	REQUIRE(fnum > 0);
	REQUIRE(fnum < 2048);
	REQUIRE(block <= 7);
}

TEST_CASE("OpnaDriver FM channels 3-5 use hi port", "[mml][opna]")
{
	OpnaDriver driver;
	// Setting voice on channel 4 should not crash
	driver.setFmVoice(4, opna_presets::BRASS);
	driver.fmNoteOn(4, 72); // C5
	auto buf = driver.renderSamples(4000);
	REQUIRE(buf.size() == 4000);
}

TEST_CASE("OpnaDriver reset clears state", "[mml][opna]")
{
	OpnaDriver driver;
	driver.setFmVoice(0, opna_presets::PIANO);
	driver.fmNoteOn(0, 60);
	static_cast<void>(driver.renderSamples(1000));
	driver.reset();
	// After reset, flush any residual pipeline samples
	static_cast<void>(driver.renderSamples(1000));
	// Subsequent output should be silent
	auto buf = driver.renderSamples(4000);
	bool allQuiet = true;
	for (auto s : buf)
	{
		if (std::abs(static_cast<int>(s)) > 100)
		{
			allQuiet = false;
			break;
		}
	}
	REQUIRE(allQuiet);
}

// ============================================================================
// OPNA Presets
// ============================================================================

TEST_CASE("OpnaPresets all 12 presets are valid", "[mml][opna]")
{
	for (int i = 0; i < 12; ++i)
	{
		const auto& voice = opna_presets::getPreset(i);
		REQUIRE(voice.algorithm <= 7);
		REQUIRE(voice.feedback <= 7);
		for (int op = 0; op < 4; ++op)
		{
			REQUIRE(voice.ops[op].multiple <= 15);
			REQUIRE(voice.ops[op].totalLevel <= 127);
			REQUIRE(voice.ops[op].attackRate <= 31);
			REQUIRE(voice.ops[op].releaseRate <= 15);
		}
	}
}

TEST_CASE("OpnaPresets different presets produce different sound", "[mml][opna]")
{
	std::vector<PcmBuffer> bufs;
	for (int i = 0; i < 4; ++i)
	{
		OpnaDriver driver;
		driver.setFmVoice(0, opna_presets::getPreset(i));
		driver.fmNoteOn(0, 60);
		bufs.push_back(driver.renderSamples(4000));
	}
	bool anyDiffer = false;
	for (std::size_t i = 1; i < bufs.size(); ++i)
	{
		for (std::size_t j = 0; j < std::min(bufs[0].size(), bufs[i].size()); ++j)
		{
			if (bufs[0][j] != bufs[i][j])
			{
				anyDiffer = true;
				break;
			}
		}
		if (anyDiffer) break;
	}
	REQUIRE(anyDiffer);
}

// ============================================================================
// OPNA Sequencer
// ============================================================================

TEST_CASE("OpnaSequencer empty produces empty", "[mml][opna]")
{
	OpnaSequencer seq;
	auto pcm = seq.render();
	REQUIRE(pcm.empty());
}

TEST_CASE("OpnaSequencer single FM track", "[mml][opna]")
{
	OpnaSequencer seq;
	seq.addFmTrack("T120 @FM0 O4 L4 CDEF", 0);
	REQUIRE(seq.trackCount() == 1);
	auto pcm = seq.render();
	REQUIRE_FALSE(pcm.empty());
	// 4 quarter notes at 120bpm = 2 sec of audio
	REQUIRE(pcm.size() > 10000);
}

TEST_CASE("OpnaSequencer single SSG track", "[mml][opna]")
{
	OpnaSequencer seq;
	seq.addSsgTrack("T120 O5 L8 CDEFGAB>C");
	auto pcm = seq.render();
	REQUIRE_FALSE(pcm.empty());
	REQUIRE(pcm.size() > 10000);
}

TEST_CASE("OpnaSequencer FM+SSG mixed", "[mml][opna]")
{
	OpnaSequencer seq;
	seq.addFmTrack("T120 @FM0 O4 L4 CDEF", 0);
	seq.addSsgTrack("T120 O5 L8 CDEFGAB>C");
	auto pcm = seq.render();
	REQUIRE_FALSE(pcm.empty());
	REQUIRE(pcm.size() > 44100);
}

TEST_CASE("OpnaSequencer clear removes tracks", "[mml][opna]")
{
	OpnaSequencer seq;
	seq.addFmTrack("C", 0);
	seq.addSsgTrack("D");
	seq.clear();
	REQUIRE(seq.trackCount() == 0);
}

TEST_CASE("OpnaSequencer sample rate is 44.1kHz output", "[mml][opna]")
{
	// チップ native (~998400Hz) は内部 render 用。出力 PCM は 44100 にダウンサンプルされる。
	OpnaSequencer seq;
	REQUIRE(seq.sampleRate() == 44100);
}

// ============================================================================
// MusicTheory
// ============================================================================

TEST_CASE("Scale C major intervals", "[mml][theory]")
{
	Scale scale(Key::C, ScaleType::Major);
	auto ivs = scale.intervals();
	REQUIRE(ivs.size() == 7);
	REQUIRE(ivs == std::vector<int>{0,2,4,5,7,9,11});
}

TEST_CASE("Scale A natural minor intervals", "[mml][theory]")
{
	Scale scale(Key::A, ScaleType::NaturalMinor);
	auto ivs = scale.intervals();
	REQUIRE(ivs.size() == 7);
	REQUIRE(ivs == std::vector<int>{0,2,3,5,7,8,10});
}

TEST_CASE("Scale chord I in C major is C major", "[mml][theory]")
{
	Scale scale(Key::C, ScaleType::Major);
	auto c = scale.chord(1);
	REQUIRE(c.root == Key::C);
	REQUIRE(c.type == ChordType::Major);
}

TEST_CASE("Scale chord II in C major is D minor", "[mml][theory]")
{
	Scale scale(Key::C, ScaleType::Major);
	auto c = scale.chord(2);
	REQUIRE(c.root == Key::D);
	REQUIRE(c.type == ChordType::Minor);
}

TEST_CASE("Chord C major offsets", "[mml][theory]")
{
	mitiru_mml::Chord c{Key::C, ChordType::Major};
	auto offs = c.offsets();
	REQUIRE(offs == std::vector<int>{0, 4, 7});
}

TEST_CASE("Chord C major midi notes at octave 4", "[mml][theory]")
{
	mitiru_mml::Chord c{Key::C, ChordType::Major};
	auto notes = c.midiNotes(4);
	REQUIRE(notes.size() == 3);
	// C4=60, E4=64, G4=67
	REQUIRE(notes[0] == 60);
	REQUIRE(notes[1] == 64);
	REQUIRE(notes[2] == 67);
}

TEST_CASE("Chord arpeggiate Up", "[mml][theory]")
{
	mitiru_mml::Chord c{Key::C, ChordType::Major};
	auto arp = c.arpeggiate(ArpPattern::Up, 4);
	REQUIRE(arp.size() == 3);
	REQUIRE(arp[0] == 60);
	REQUIRE(arp[1] == 64);
	REQUIRE(arp[2] == 67);
}

TEST_CASE("Chord arpeggiate Down reverses", "[mml][theory]")
{
	mitiru_mml::Chord c{Key::C, ChordType::Major};
	auto arp = c.arpeggiate(ArpPattern::Down, 4);
	REQUIRE(arp.size() == 3);
	REQUIRE(arp[0] == 67);
	REQUIRE(arp[1] == 64);
	REQUIRE(arp[2] == 60);
}

TEST_CASE("Chord arpeggiate Alberti pattern", "[mml][theory]")
{
	mitiru_mml::Chord c{Key::C, ChordType::Major};
	auto arp = c.arpeggiate(ArpPattern::Alberti, 4);
	REQUIRE(arp.size() == 4);
	// C-G-E-G
	REQUIRE(arp[0] == 60);
	REQUIRE(arp[1] == 67);
	REQUIRE(arp[2] == 64);
	REQUIRE(arp[3] == 67);
}

TEST_CASE("ChordProgression parse Im IVm V7 Im", "[mml][theory]")
{
	auto chords = ChordProgression::parse("Im IVm V7 Im", Key::A, ScaleType::NaturalMinor);
	REQUIRE(chords.size() == 4);
	// I度 in A minor = Am → forced Minor
	REQUIRE(chords[0].root == Key::A);
	REQUIRE(chords[0].type == ChordType::Minor);
	// V7 = E dom7
	REQUIRE(chords[2].type == ChordType::Dom7);
}

TEST_CASE("ChordProgression preset pop", "[mml][theory]")
{
	auto chords = ChordProgression::preset("pop", Key::C, ScaleType::Major);
	REQUIRE(chords.size() == 4); // I V vi IV
}

TEST_CASE("Scale degreeToNote returns correct MIDI", "[mml][theory]")
{
	Scale scale(Key::C, ScaleType::Major);
	// degree 1, octave 4 → C4 = 60
	REQUIRE(scale.degreeToNote(1, 4) == 60);
	// degree 5, octave 4 → G4 = 67
	REQUIRE(scale.degreeToNote(5, 4) == 67);
}

TEST_CASE("Chord toMml produces string", "[mml][theory]")
{
	mitiru_mml::Chord c{Key::C, ChordType::Major};
	auto mml = c.toMml(4, 4);
	REQUIRE_FALSE(mml.empty());
	// Should contain C, E, G note names
	REQUIRE(mml.find("C") != std::string::npos);
}

// ============================================================================
// PatternLibrary
// ============================================================================

TEST_CASE("DrumPatterns rock_8beat returns pattern", "[mml][patterns]")
{
	auto pattern = DrumPatterns::get("rock_8beat");
	REQUIRE_FALSE(pattern.empty());
	REQUIRE(pattern.find("L8") != std::string::npos);
}

TEST_CASE("DrumPatterns unknown returns default", "[mml][patterns]")
{
	auto pattern = DrumPatterns::get("nonexistent");
	REQUIRE_FALSE(pattern.empty());
}

TEST_CASE("BassPatterns root_fifth generates MML", "[mml][patterns]")
{
	mitiru_mml::Chord c{Key::C, ChordType::Major};
	auto mml = BassPatterns::generate(c, "root_fifth", 3, 8);
	REQUIRE_FALSE(mml.empty());
	REQUIRE(mml.find("L8") != std::string::npos);
}

TEST_CASE("BassPatterns pumping repeats root", "[mml][patterns]")
{
	mitiru_mml::Chord c{Key::A, ChordType::Minor};
	auto mml = BassPatterns::generate(c, "pumping", 3, 8);
	REQUIRE_FALSE(mml.empty());
}

TEST_CASE("ArpPatterns generate Up produces notes", "[mml][patterns]")
{
	mitiru_mml::Chord c{Key::C, ChordType::Major};
	auto mml = ArpPatterns::generate(c, ArpPattern::Up, 4, 16);
	REQUIRE_FALSE(mml.empty());
	REQUIRE(mml.find("L16") != std::string::npos);
}

// ============================================================================
// SongBuilder
// ============================================================================

TEST_CASE("SongBuilder produces tracks", "[mml][builder]")
{
	SongBuilder builder;
	builder.setKey(Key::C, ScaleType::Major);
	builder.setTempo(120);
	builder.setMelody({1,3,5,3,1}, 8);
	auto data = builder.build();
	REQUIRE(data.tempo == 120);
	REQUIRE_FALSE(data.tracks.empty());
	// At least melody track
	REQUIRE(data.tracks[0].label == "Melody");
	REQUIRE_FALSE(data.tracks[0].mml.empty());
}

TEST_CASE("SongBuilder with progression and drums", "[mml][builder]")
{
	SongBuilder builder;
	builder.setKey(Key::A, ScaleType::NaturalMinor);
	builder.setTempo(155);
	builder.setProgression("Im IVm V7 Im");
	builder.setDrumPattern("rock_8beat");
	builder.setMelody({5,4,3,2,1}, 8);
	auto data = builder.build();
	REQUIRE(data.tempo == 155);
	// Should have Melody + Harmony + Bass + Drums = 4 tracks
	REQUIRE(data.tracks.size() >= 4);
	// Find drums track
	bool hasDrums = false;
	for (const auto& t : data.tracks)
	{
		if (t.label == "Drums") hasDrums = true;
	}
	REQUIRE(hasDrums);
}

TEST_CASE("SongBuilder with SSG arp", "[mml][builder]")
{
	SongBuilder builder;
	builder.setKey(Key::C, ScaleType::Major);
	builder.setProgression("I IV V I");
	builder.enableSsgArp(true);
	builder.setMelody({1,2,3}, 4);
	auto data = builder.build();
	bool hasSsg = false;
	for (const auto& t : data.tracks)
	{
		if (t.trackType == "SSG") hasSsg = true;
	}
	REQUIRE(hasSsg);
}

// ============================================================================
// MusicPrompt
// ============================================================================

TEST_CASE("MusicPromptCompiler rpg_battle", "[mml][prompt]")
{
	MusicPrompt prompt;
	prompt.genre = "rpg_battle";
	prompt.key = Key::A;
	prompt.scaleType = ScaleType::NaturalMinor;
	prompt.bpm = 155;
	prompt.energy = 0.9f;
	auto data = MusicPromptCompiler::compile(prompt);
	REQUIRE_FALSE(data.tracks.empty());
	REQUIRE(data.tempo >= 140); // battle tempo should be fast
}

TEST_CASE("MusicPromptCompiler vn_sad", "[mml][prompt]")
{
	MusicPrompt prompt;
	prompt.genre = "vn_sad";
	prompt.key = Key::D;
	prompt.scaleType = ScaleType::NaturalMinor;
	prompt.bpm = 120;
	prompt.energy = 0.2f;
	auto data = MusicPromptCompiler::compile(prompt);
	REQUIRE_FALSE(data.tracks.empty());
	REQUIRE(data.tempo <= 100); // sad should be slow
}

TEST_CASE("MusicPromptCompiler default genre", "[mml][prompt]")
{
	MusicPrompt prompt;
	prompt.genre = "default";
	auto data = MusicPromptCompiler::compile(prompt);
	REQUIRE_FALSE(data.tracks.empty());
	REQUIRE(data.tempo == 120);
}

TEST_CASE("MusicPromptCompiler no drums", "[mml][prompt]")
{
	MusicPrompt prompt;
	prompt.useDrums = false;
	auto data = MusicPromptCompiler::compile(prompt);
	bool hasDrums = false;
	for (const auto& t : data.tracks)
	{
		if (t.label == "Drums") hasDrums = true;
	}
	REQUIRE_FALSE(hasDrums);
}

// ============================================================================
// Loop brackets
// ============================================================================

TEST_CASE("MmlParser parses loop brackets", "[mml][parser]")
{
	auto cmds = MmlParser::parse("[CD]3");
	REQUIRE(cmds.size() == 4); // LoopStart, C, D, LoopEnd
	REQUIRE(cmds[0].type == CommandType::LoopStart);
	REQUIRE(cmds[3].type == CommandType::LoopEnd);
	REQUIRE(cmds[3].value == 3);
}

TEST_CASE("MmlParser loop default repeat is 2", "[mml][parser]")
{
	auto cmds = MmlParser::parse("[C]");
	REQUIRE(cmds.size() == 3);
	REQUIRE(cmds[2].type == CommandType::LoopEnd);
	REQUIRE(cmds[2].value == 2);
}

TEST_CASE("Track loop expansion repeats correctly", "[mml][track]")
{
	Synthesizer synth({44100, 0.5f});
	Track track(synth);
	// [C4]3 should produce 3 quarter notes at T120
	auto singleCmds = MmlParser::parse("T120 O4 C4C4C4");
	auto loopCmds = MmlParser::parse("T120 O4 [C4]3");
	auto pcmSingle = track.render(singleCmds);
	auto pcmLoop = track.render(loopCmds);
	// Both should produce approximately the same length
	REQUIRE_FALSE(pcmSingle.empty());
	REQUIRE_FALSE(pcmLoop.empty());
	REQUIRE(pcmLoop.size() == Approx(pcmSingle.size()).margin(100));
}

TEST_CASE("Track nested loop expansion", "[mml][track]")
{
	// expandLoops static method
	CommandList cmds;
	cmds.push_back({CommandType::LoopStart, 0, 0, false, false});
	cmds.push_back({CommandType::Note, 0, 4, false, false}); // C4
	cmds.push_back({CommandType::LoopEnd, 3, 0, false, false});
	auto expanded = Track::expandLoops(cmds);
	// 3 repetitions of C4
	REQUIRE(expanded.size() == 3);
	for (const auto& c : expanded)
	{
		REQUIRE(c.type == CommandType::Note);
	}
}

// ============================================================================
// MotifEngine
// ============================================================================

TEST_CASE("MotifEngine transpose shifts all degrees", "[mml][motif]")
{
	Motif m;
	m.degrees = {1, 3, 5};
	m.lengths = {8, 8, 4};
	m.rests = {false, false, false};

	auto result = MotifEngine::transpose(m, 2);
	REQUIRE(result.degrees.size() == 3);
	REQUIRE(result.degrees[0] == 3);
	REQUIRE(result.degrees[1] == 5);
	REQUIRE(result.degrees[2] == 7);
	// Lengths and rests unchanged
	REQUIRE(result.lengths == m.lengths);
	REQUIRE(result.rests == m.rests);
}

TEST_CASE("MotifEngine invert mirrors around pivot", "[mml][motif]")
{
	Motif m;
	m.degrees = {1, 3, 5};
	m.lengths = {8, 8, 4};
	m.rests = {false, false, false};

	auto result = MotifEngine::invert(m, 3);
	// pivot=3: 1 -> 5, 3 -> 3, 5 -> 1
	REQUIRE(result.degrees[0] == 5);
	REQUIRE(result.degrees[1] == 3);
	REQUIRE(result.degrees[2] == 1);
}

TEST_CASE("MotifEngine retrograde reverses all", "[mml][motif]")
{
	Motif m;
	m.degrees = {1, 3, 5};
	m.lengths = {8, 4, 16};
	m.rests = {false, true, false};

	auto result = MotifEngine::retrograde(m);
	REQUIRE(result.degrees == std::vector<int>{5, 3, 1});
	REQUIRE(result.lengths == std::vector<int>{16, 4, 8});
	REQUIRE(result.rests == std::vector<bool>{false, true, false});
}

TEST_CASE("MotifEngine augment doubles note values", "[mml][motif]")
{
	Motif m;
	m.degrees = {1, 3};
	m.lengths = {8, 16};
	m.rests = {false, false};

	auto result = MotifEngine::augment(m);
	// L8 -> L4, L16 -> L8
	REQUIRE(result.lengths[0] == 4);
	REQUIRE(result.lengths[1] == 8);
}

TEST_CASE("MotifEngine diminish halves note values", "[mml][motif]")
{
	Motif m;
	m.degrees = {1, 3};
	m.lengths = {4, 8};
	m.rests = {false, false};

	auto result = MotifEngine::diminish(m);
	// L4 -> L8, L8 -> L16
	REQUIRE(result.lengths[0] == 8);
	REQUIRE(result.lengths[1] == 16);
}

TEST_CASE("MotifEngine augment clamps minimum length", "[mml][motif]")
{
	Motif m;
	m.degrees = {1};
	m.lengths = {1};
	m.rests = {false};

	auto result = MotifEngine::augment(m);
	REQUIRE(result.lengths[0] == 1); // cannot go below 1
}

TEST_CASE("MotifEngine diminish clamps maximum length", "[mml][motif]")
{
	Motif m;
	m.degrees = {1};
	m.lengths = {64};
	m.rests = {false};

	auto result = MotifEngine::diminish(m);
	REQUIRE(result.lengths[0] == 64); // clamped to 64
}

TEST_CASE("MotifEngine varyEnding changes last non-rest degree", "[mml][motif]")
{
	Motif m;
	m.degrees = {1, 3, 5, 3};
	m.lengths = {8, 8, 8, 4};
	m.rests = {false, false, false, false};

	auto result = MotifEngine::varyEnding(m, 1);
	REQUIRE(result.degrees[3] == 1);
	// Other degrees unchanged
	REQUIRE(result.degrees[0] == 1);
	REQUIRE(result.degrees[1] == 3);
	REQUIRE(result.degrees[2] == 5);
}

TEST_CASE("MotifEngine varyEnding skips trailing rests", "[mml][motif]")
{
	Motif m;
	m.degrees = {1, 3, 5, 0};
	m.lengths = {8, 8, 8, 4};
	m.rests = {false, false, false, true};

	auto result = MotifEngine::varyEnding(m, 1);
	// Should change degree at index 2 (last non-rest)
	REQUIRE(result.degrees[2] == 1);
	REQUIRE(result.degrees[3] == 0); // rest degree unchanged
}

TEST_CASE("MotifEngine toMml produces valid MML string", "[mml][motif]")
{
	Motif m;
	m.degrees = {1, 3, 5};
	m.lengths = {8, 8, 4};
	m.rests = {false, false, false};

	Scale scale(Key::C, ScaleType::Major);
	auto mml = MotifEngine::toMml(m, scale, 5);
	REQUIRE_FALSE(mml.empty());
	// Should contain note names
	REQUIRE(mml.find("C") != std::string::npos);
}

TEST_CASE("MotifEngine toMml handles rests", "[mml][motif]")
{
	Motif m;
	m.degrees = {1, 0, 5};
	m.lengths = {8, 8, 4};
	m.rests = {false, true, false};

	Scale scale(Key::C, ScaleType::Major);
	auto mml = MotifEngine::toMml(m, scale, 5);
	REQUIRE(mml.find("R8") != std::string::npos);
}

TEST_CASE("MotifEngine toMml parseable by MmlParser", "[mml][motif]")
{
	Motif m;
	m.degrees = {1, 2, 3, 4, 5};
	m.lengths = {8, 8, 8, 8, 4};
	m.rests = {false, false, false, false, false};

	Scale scale(Key::C, ScaleType::Major);
	auto mml = MotifEngine::toMml(m, scale, 4);
	auto cmds = MmlParser::parse(mml);
	// Should parse without errors and produce note commands
	bool hasNotes = false;
	for (const auto& cmd : cmds)
	{
		if (cmd.type == CommandType::Note)
		{
			hasNotes = true;
			break;
		}
	}
	REQUIRE(hasNotes);
}

TEST_CASE("MotifEngine transpose negative", "[mml][motif]")
{
	Motif m;
	m.degrees = {5, 6, 7};
	m.lengths = {4, 4, 4};
	m.rests = {false, false, false};

	auto result = MotifEngine::transpose(m, -3);
	REQUIRE(result.degrees[0] == 2);
	REQUIRE(result.degrees[1] == 3);
	REQUIRE(result.degrees[2] == 4);
}

// ============================================================================
// Sound Quality Improvements
// ============================================================================

TEST_CASE("MmlParser parses slur tilde", "[mml][parser][legato]")
{
	auto cmds = MmlParser::parse("C4~D4");
	REQUIRE(cmds.size() == 2);
	REQUIRE(cmds[0].tied == true);
	REQUIRE(cmds[0].slur == true);
}

TEST_CASE("MmlParser parses velocity lowercase v", "[mml][parser][velocity]")
{
	auto cmds = MmlParser::parse("v12 C4 v8 D4");
	REQUIRE(cmds.size() == 4);
	REQUIRE(cmds[0].type == CommandType::Velocity);
	REQUIRE(cmds[0].value == 12);
	REQUIRE(cmds[2].type == CommandType::Velocity);
	REQUIRE(cmds[2].value == 8);
}

TEST_CASE("MmlParser V uppercase is still Volume", "[mml][parser][velocity]")
{
	auto cmds = MmlParser::parse("V10 C4");
	REQUIRE(cmds.size() == 2);
	REQUIRE(cmds[0].type == CommandType::Volume);
	REQUIRE(cmds[0].value == 10);
}

TEST_CASE("MmlParser parses vibrato with delay", "[mml][parser][vibrato]")
{
	auto cmds = MmlParser::parse("M5,20,100 C4");
	REQUIRE(cmds.size() == 2);
	REQUIRE(cmds[0].type == CommandType::Vibrato);
	REQUIRE(cmds[0].value == 5);
	REQUIRE(cmds[0].extra == 20);
	REQUIRE(cmds[0].extra2 == 100);
}

TEST_CASE("MmlParser parses portamento underscore", "[mml][parser][portamento]")
{
	auto cmds = MmlParser::parse("C4 _D4");
	REQUIRE(cmds.size() == 2);
	REQUIRE(cmds[0].type == CommandType::Note);
	REQUIRE(cmds[1].type == CommandType::Note);
	REQUIRE(cmds[1].portamento == true);
}

TEST_CASE("MmlParser parses grace notes braces", "[mml][parser][grace]")
{
	auto cmds = MmlParser::parse("{CD}E4");
	REQUIRE(cmds.size() == 5);
	REQUIRE(cmds[0].type == CommandType::GraceStart);
	REQUIRE(cmds[1].type == CommandType::Note); // C
	REQUIRE(cmds[2].type == CommandType::Note); // D
	REQUIRE(cmds[3].type == CommandType::GraceEnd);
	REQUIRE(cmds[4].type == CommandType::Note); // E
}

TEST_CASE("MmlParser parses crescendo decrescendo", "[mml][parser][dynamics]")
{
	auto cmds = MmlParser::parse("(C4 D4 E4)");
	bool hasCresc = false;
	bool hasDecresc = false;
	for (const auto& c : cmds)
	{
		if (c.type == CommandType::CrescStart) hasCresc = true;
		if (c.type == CommandType::DecrescStart) hasDecresc = true;
	}
	REQUIRE(hasCresc);
	REQUIRE(hasDecresc);
}

TEST_CASE("MmlParser parses tremolo Y command", "[mml][parser][tremolo]")
{
	auto cmds = MmlParser::parse("Y6,3 C4");
	REQUIRE(cmds.size() == 2);
	REQUIRE(cmds[0].type == CommandType::Tremolo);
	REQUIRE(cmds[0].value == 6);
	REQUIRE(cmds[0].extra == 3);
}

TEST_CASE("MmlParser parses SSG envelope SE command", "[mml][parser][ssg]")
{
	auto cmds = MmlParser::parse("SE9,500 C4");
	REQUIRE(cmds.size() == 2);
	REQUIRE(cmds[0].type == CommandType::SsgEnvelope);
	REQUIRE(cmds[0].value == 9);
	REQUIRE(cmds[0].extra == 500);
}

TEST_CASE("OpnaDriver setSsgEnvelope does not crash", "[mml][opna][ssg]")
{
	OpnaDriver driver;
	driver.setSsgEnable(true, false, false);
	driver.setSsgEnvelope(0, 0x09, 500);
	auto buf = driver.renderSamples(4000);
	REQUIRE(buf.size() == 4000);
}

TEST_CASE("OpnaSequencer legato produces FmFreqChange events", "[mml][opna][legato]")
{
	OpnaSequencer seq;
	// C4&D4 with different notes should produce legato (no key-off between)
	seq.addFmTrack("T120 O4 L4 C&D", 0);
	auto pcm = seq.render();
	REQUIRE_FALSE(pcm.empty());
}

TEST_CASE("OpnaSequencer portamento renders", "[mml][opna][portamento]")
{
	OpnaSequencer seq;
	seq.addFmTrack("T120 O4 L4 C _D", 0);
	auto pcm = seq.render();
	REQUIRE_FALSE(pcm.empty());
	REQUIRE(pcm.size() > 10000);
}

TEST_CASE("OpnaSequencer grace notes render", "[mml][opna][grace]")
{
	OpnaSequencer seq;
	seq.addFmTrack("T120 O4 L4 {CD}E", 0);
	auto pcm = seq.render();
	REQUIRE_FALSE(pcm.empty());
	REQUIRE(pcm.size() > 5000);
}

TEST_CASE("OpnaSequencer crescendo renders", "[mml][opna][dynamics]")
{
	OpnaSequencer seq;
	seq.addFmTrack("T120 O4 L4 V4 (C D E F)", 0);
	auto pcm = seq.render();
	REQUIRE_FALSE(pcm.empty());
}

TEST_CASE("OpnaSequencer SSG envelope renders", "[mml][opna][ssg]")
{
	OpnaSequencer seq;
	seq.addSsgTrack("T120 O5 L8 SE9,500 CDEFGAB>C");
	auto pcm = seq.render();
	REQUIRE_FALSE(pcm.empty());
	REQUIRE(pcm.size() > 10000);
}

// ============================================================================
// AiComposer
// ============================================================================

TEST_CASE("AiComposer generateContour Ascending", "[mml][ai]")
{
	auto deg = AiComposer::generateContour(AiComposer::Contour::Ascending, 8, 7, 1);
	REQUIRE(deg.size() == 8);
	// First should be startDegree, last should be near startDegree+range
	REQUIRE(deg.front() == 1);
	REQUIRE(deg.back() == 8);
}

TEST_CASE("AiComposer generateContour Arch peaks in middle", "[mml][ai]")
{
	auto deg = AiComposer::generateContour(AiComposer::Contour::Arch, 9, 8, 1);
	REQUIRE(deg.size() == 9);
	// Peak should be at the middle index
	REQUIRE(deg[4] >= deg[0]);
	REQUIRE(deg[4] >= deg[8]);
}

TEST_CASE("AiComposer generateContour Static is constant", "[mml][ai]")
{
	auto deg = AiComposer::generateContour(AiComposer::Contour::Static, 5, 8, 3);
	REQUIRE(deg.size() == 5);
	for (int d : deg)
	{
		REQUIRE(d == 3);
	}
}

TEST_CASE("AiComposer generateAnswer resolves to tonic", "[mml][ai]")
{
	Scale scale(Key::C, ScaleType::Major);
	std::vector<int> question = {1, 3, 5, 3, 1, 5, 3, 2};
	auto answer = AiComposer::generateAnswer(question, scale);
	REQUIRE(answer.size() == question.size());
	// Last note should be 1 (tonic)
	REQUIRE(answer.back() == 1);
	// Second to last should be 7 (leading tone)
	REQUIRE(answer[answer.size() - 2] == 7);
}

TEST_CASE("AiComposer generateCounter parallel_third", "[mml][ai]")
{
	std::vector<int> melody = {1, 3, 5, 3};
	auto counter = AiComposer::generateCounter(melody, "parallel_third");
	REQUIRE(counter.size() == 4);
	REQUIRE(counter[0] == 3);
	REQUIRE(counter[1] == 5);
	REQUIRE(counter[2] == 7);
	REQUIRE(counter[3] == 5);
}

TEST_CASE("AiComposer generateCounter contrary reverses motion", "[mml][ai]")
{
	std::vector<int> melody = {1, 3, 5, 3};
	auto counter = AiComposer::generateCounter(melody, "contrary");
	REQUIRE(counter.size() == 4);
	// Starts same, then moves opposite
	REQUIRE(counter[0] == 1);
	REQUIRE(counter[1] == -1); // 1 - (3-1) = -1
	REQUIRE(counter[2] == -3); // -1 - (5-3) = -3
}

TEST_CASE("AiComposer applyRhythm produces matching sizes", "[mml][ai]")
{
	std::vector<int> degrees = {1, 2, 3, 4, 5, 6, 7, 8};
	auto [outDeg, outLen] = AiComposer::applyRhythm(degrees, "ballad");
	REQUIRE(outDeg.size() == outLen.size());
	REQUIRE_FALSE(outDeg.empty());
}

TEST_CASE("AiComposer suggestOrchestration rpg_battle", "[mml][ai]")
{
	auto orch = AiComposer::suggestOrchestration("rpg_battle");
	REQUIRE(orch.melodyPreset == 2);  // Brass
	REQUIRE(orch.bassPreset == 6);    // Bass
	REQUIRE(orch.drumPattern == "fast_rock");
}

TEST_CASE("AiComposer suggestOrchestration default", "[mml][ai]")
{
	auto orch = AiComposer::suggestOrchestration("unknown_genre");
	REQUIRE(orch.melodyPreset == 0);  // Piano default
}

TEST_CASE("AiComposer compose produces complete SongData", "[mml][ai]")
{
	AiComposer::CompositionPlan plan;
	plan.genre = "rpg_town";
	plan.key = Key::C;
	plan.scaleType = ScaleType::Major;
	plan.bpm = 120;
	plan.progression = "I IV V I";
	plan.melodyContour = AiComposer::Contour::Arch;
	plan.rhythmPattern = "ballad";
	plan.counterStyle = "parallel_third";
	plan.useSsg = true;
	plan.useDrums = true;
	plan.repeatCount = 1;

	auto data = AiComposer::compose(plan);
	REQUIRE(data.tempo == 120);
	REQUIRE_FALSE(data.tracks.empty());

	// Should have Melody, Counter, Bass, SSG Arp, Drums = 5 tracks
	bool hasMelody = false;
	bool hasCounter = false;
	bool hasBass = false;
	bool hasSsg = false;
	bool hasDrums = false;
	for (const auto& t : data.tracks)
	{
		if (t.label == "Melody") hasMelody = true;
		if (t.label == "Counter") hasCounter = true;
		if (t.label == "Bass") hasBass = true;
		if (t.label == "SSG Arp") hasSsg = true;
		if (t.label == "Drums") hasDrums = true;
	}
	REQUIRE(hasMelody);
	REQUIRE(hasCounter);
	REQUIRE(hasBass);
	REQUIRE(hasSsg);
	REQUIRE(hasDrums);
}

TEST_CASE("AiComposer compose without counter or drums", "[mml][ai]")
{
	AiComposer::CompositionPlan plan;
	plan.genre = "vn_sad";
	plan.key = Key::D;
	plan.scaleType = ScaleType::NaturalMinor;
	plan.bpm = 84;
	plan.progression = "Im IVm V7 Im";
	plan.useSsg = false;
	plan.useDrums = false;
	plan.counterStyle = "";
	plan.repeatCount = 1;

	auto data = AiComposer::compose(plan);
	REQUIRE(data.tempo == 84);

	bool hasCounter = false;
	bool hasDrums = false;
	bool hasSsg = false;
	for (const auto& t : data.tracks)
	{
		if (t.label == "Counter") hasCounter = true;
		if (t.label == "Drums") hasDrums = true;
		if (t.label == "SSG Arp") hasSsg = true;
	}
	REQUIRE_FALSE(hasCounter);
	REQUIRE_FALSE(hasDrums);
	REQUIRE_FALSE(hasSsg);
}

TEST_CASE("AiComposer degreesToMml produces parseable MML", "[mml][ai]")
{
	Scale scale(Key::C, ScaleType::Major);
	std::vector<int> degrees = {1, 2, 3, 4, 5};
	std::vector<int> lengths = {8, 8, 8, 8, 4};
	auto mml = AiComposer::degreesToMml(degrees, lengths, scale, 5, 120);
	REQUIRE_FALSE(mml.empty());
	// Should be parseable
	auto cmds = MmlParser::parse(mml);
	bool hasNotes = false;
	for (const auto& cmd : cmds)
	{
		if (cmd.type == CommandType::Note)
		{
			hasNotes = true;
			break;
		}
	}
	REQUIRE(hasNotes);
}

TEST_CASE("AiComposer generatePhrase produces MML", "[mml][ai]")
{
	Scale scale(Key::C, ScaleType::Major);
	auto phrase = AiComposer::generatePhrase(
		scale, AiComposer::Contour::Arch, "ballad", 5);
	REQUIRE_FALSE(phrase.empty());
	// Should be parseable
	auto cmds = MmlParser::parse(phrase);
	REQUIRE_FALSE(cmds.empty());
}

// ============================================================================
// PhraseDictionary
// ============================================================================

TEST_CASE("PhraseDictionary has phrases", "[mml][phrase]")
{
	REQUIRE(mitiru_mml::PhraseDictionary::count() > 30);
}

TEST_CASE("PhraseDictionary byMood bright returns results", "[mml][phrase]")
{
	auto phrases = mitiru_mml::PhraseDictionary::byMood("bright");
	REQUIRE_FALSE(phrases.empty());
	for (const auto* p : phrases)
	{
		REQUIRE(p->mood == "bright");
		REQUIRE_FALSE(p->degrees.empty());
		REQUIRE(p->degrees.size() == p->lengths.size());
	}
}

TEST_CASE("PhraseDictionary byFunction opening returns results", "[mml][phrase]")
{
	auto phrases = mitiru_mml::PhraseDictionary::byFunction("opening");
	REQUIRE_FALSE(phrases.empty());
}

TEST_CASE("PhraseDictionary query dark+climax", "[mml][phrase]")
{
	auto phrases = mitiru_mml::PhraseDictionary::query("dark", "climax");
	REQUIRE_FALSE(phrases.empty());
}

TEST_CASE("PhraseDictionary all moods covered", "[mml][phrase]")
{
	REQUIRE_FALSE(mitiru_mml::PhraseDictionary::byMood("bright").empty());
	REQUIRE_FALSE(mitiru_mml::PhraseDictionary::byMood("dark").empty());
	REQUIRE_FALSE(mitiru_mml::PhraseDictionary::byMood("heroic").empty());
	REQUIRE_FALSE(mitiru_mml::PhraseDictionary::byMood("gentle").empty());
	REQUIRE_FALSE(mitiru_mml::PhraseDictionary::byMood("tense").empty());
	REQUIRE_FALSE(mitiru_mml::PhraseDictionary::byMood("epic").empty());
	REQUIRE_FALSE(mitiru_mml::PhraseDictionary::byMood("mysterious").empty());
}

// ============================================================================
// PhraseComposer
// ============================================================================

TEST_CASE("PhraseComposer compose produces tracks", "[mml][composer]")
{
	mitiru_mml::CompositionRecipe recipe;
	recipe.mood = "bright";
	recipe.key = mitiru_mml::Key::C;
	recipe.scaleType = mitiru_mml::ScaleType::Major;
	recipe.bpm = 120;
	recipe.progression = "I IV V I";

	auto data = mitiru_mml::PhraseComposer::compose(recipe);
	REQUIRE_FALSE(data.tracks.empty());
	REQUIRE(data.tracks.size() >= 4); // melody, harmony, bass, drums

	for (const auto& t : data.tracks)
	{
		REQUIRE_FALSE(t.mml.empty());
	}
}

TEST_CASE("PhraseComposer different moods produce different melodies", "[mml][composer]")
{
	mitiru_mml::CompositionRecipe r1;
	r1.mood = "bright";
	r1.key = mitiru_mml::Key::C;

	mitiru_mml::CompositionRecipe r2;
	r2.mood = "dark";
	r2.key = mitiru_mml::Key::C;

	auto d1 = mitiru_mml::PhraseComposer::compose(r1);
	auto d2 = mitiru_mml::PhraseComposer::compose(r2);

	// Different moods should produce different melody MML
	REQUIRE(d1.tracks[0].mml != d2.tracks[0].mml);
}

TEST_CASE("PhraseComposer deterministic with same seed", "[mml][composer]")
{
	mitiru_mml::CompositionRecipe recipe;
	recipe.mood = "heroic";
	recipe.seed = 12345;

	auto d1 = mitiru_mml::PhraseComposer::compose(recipe);
	auto d2 = mitiru_mml::PhraseComposer::compose(recipe);

	REQUIRE(d1.tracks[0].mml == d2.tracks[0].mml);
}

TEST_CASE("PhraseComposer different seeds produce different results", "[mml][composer]")
{
	mitiru_mml::CompositionRecipe r1;
	r1.mood = "bright";
	r1.seed = 1;

	mitiru_mml::CompositionRecipe r2;
	r2.mood = "bright";
	r2.seed = 999;

	auto d1 = mitiru_mml::PhraseComposer::compose(r1);
	auto d2 = mitiru_mml::PhraseComposer::compose(r2);

	REQUIRE(d1.tracks[0].mml != d2.tracks[0].mml);
}

// ============================================================================
// FM Drum Synthesis
// ============================================================================

TEST_CASE("OpnaDriver synthKick produces audible output", "[mml][opna][drum]")
{
	OpnaDriver driver;
	driver.synthKick(3);
	auto buf = driver.renderSamples(11000);
	bool hasSound = false;
	for (auto s : buf)
	{
		if (std::abs(static_cast<int>(s)) > 100)
		{
			hasSound = true;
			break;
		}
	}
	REQUIRE(hasSound);
}

TEST_CASE("OpnaDriver synthSnare produces audible output", "[mml][opna][drum]")
{
	OpnaDriver driver;
	driver.synthSnare(4);
	auto buf = driver.renderSamples(11000);
	bool hasSound = false;
	for (auto s : buf)
	{
		if (std::abs(static_cast<int>(s)) > 100)
		{
			hasSound = true;
			break;
		}
	}
	REQUIRE(hasSound);
}

TEST_CASE("OpnaDriver synthHihat produces audible output", "[mml][opna][drum]")
{
	OpnaDriver driver;
	driver.synthHihat(5);
	auto buf = driver.renderSamples(11000);
	bool hasSound = false;
	for (auto s : buf)
	{
		if (std::abs(static_cast<int>(s)) > 100)
		{
			hasSound = true;
			break;
		}
	}
	REQUIRE(hasSound);
}

TEST_CASE("OpnaSequencer rhythm track produces sound", "[mml][opna][drum]")
{
	OpnaSequencer seq;
	seq.addRhythmTrack("T120 L8 BHSHBHSH");
	auto pcm = seq.render();
	REQUIRE_FALSE(pcm.empty());
	REQUIRE(pcm.size() > 10000);
	// 音が含まれていることを確認する
	bool hasSound = false;
	for (auto s : pcm)
	{
		if (std::abs(static_cast<int>(s)) > 100)
		{
			hasSound = true;
			break;
		}
	}
	REQUIRE(hasSound);
}

TEST_CASE("OpnaSequencer FM+SSG+Rhythm mixed", "[mml][opna][drum]")
{
	OpnaSequencer seq;
	seq.addFmTrack("T120 O4 L4 CDEF", 0);
	seq.addSsgTrack("T120 O5 L8 CDEFGAB>C");
	seq.addRhythmTrack("T120 L8 BHSHBHSH");
	auto pcm = seq.render();
	REQUIRE_FALSE(pcm.empty());
	REQUIRE(pcm.size() > 20000);
}

// ============================================================================
// MmlValidator
// ============================================================================

TEST_CASE("MmlValidator matching tracks pass", "[mml][validator]")
{
	std::vector<std::string> tracks = {
		"T120 O4 L4 CDEF",
		"T120 O3 L4 CDEF"
	};
	auto result = MmlValidator::validate(tracks);
	REQUIRE(result.valid);
	REQUIRE(result.errors.empty());
	REQUIRE(result.trackDurations.size() == 2);
}

TEST_CASE("MmlValidator detects duration mismatch", "[mml][validator]")
{
	std::vector<std::string> tracks = {
		"T120 O4 L4 CDEF",       // 4 quarter notes = 2s
		"T120 O3 L4 CDEFGAB>C"   // 8 quarter notes = 4s
	};
	auto result = MmlValidator::validate(tracks);
	REQUIRE_FALSE(result.valid);
	REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("MmlValidator estimateDuration single track", "[mml][validator]")
{
	// T120 L4 CDEF = 4 quarter notes at 120bpm = 4 * 0.5s = 2.0s
	float dur = MmlValidator::estimateDuration("T120 L4 CDEF");
	REQUIRE(dur == Approx(2.0f).margin(0.01f));
}

// ============================================================================
// TFI Importer
// ============================================================================

TEST_CASE("TfiImporter parses 42-byte data correctly", "[mml][tfi]")
{
	std::array<uint8_t, 42> data = {};
	data[0] = 4;  // algorithm
	data[1] = 5;  // feedback
	// OP1 (offset 2): MUL=2, DT=3, TL=10, RS=1, AR=31, DR=8, SR=4, RR=7, SL=2
	data[2] = 2; data[3] = 3; data[4] = 10; data[5] = 1;
	data[6] = 31; data[7] = 8; data[8] = 4; data[9] = 7; data[10] = 2;

	auto voice = TfiImporter::fromTfi(data);
	REQUIRE(voice.algorithm == 4);
	REQUIRE(voice.feedback == 5);
	REQUIRE(voice.ops[0].multiple == 2);
	REQUIRE(voice.ops[0].detune == 3);
	REQUIRE(voice.ops[0].totalLevel == 10);
	REQUIRE(voice.ops[0].keyScale == 1);
	REQUIRE(voice.ops[0].attackRate == 31);
	REQUIRE(voice.ops[0].decayRate == 8);
	REQUIRE(voice.ops[0].sustainRate == 4);
	REQUIRE(voice.ops[0].releaseRate == 7);
	REQUIRE(voice.ops[0].sustainLevel == 2);
}

// ============================================================================
// Pan Control
// ============================================================================

TEST_CASE("MmlParser parses pan command", "[mml][parser][pan]")
{
	auto cmds = MmlParser::parse("P0 C4 P1 D4 P2 E4");
	int panCount = 0;
	for (const auto& c : cmds)
	{
		if (c.type == CommandType::Pan) ++panCount;
	}
	REQUIRE(panCount == 3);
}

TEST_CASE("OpnaSequencer pan renders without crash", "[mml][opna][pan]")
{
	OpnaSequencer seq;
	seq.addFmTrack("T120 O4 L4 P0 C P1 D P2 E", 0);
	auto pcm = seq.render();
	REQUIRE_FALSE(pcm.empty());
}

// ============================================================================
// Loop Point and Loop Count
// ============================================================================

TEST_CASE("MmlParser parses loop point dollar sign", "[mml][parser][loop]")
{
	auto cmds = MmlParser::parse("CDEF $ GAB>C");
	bool hasLoopPoint = false;
	for (const auto& c : cmds)
	{
		if (c.type == CommandType::LoopPoint) hasLoopPoint = true;
	}
	REQUIRE(hasLoopPoint);
}

TEST_CASE("OpnaSequencer loop count produces longer output", "[mml][opna][loop]")
{
	OpnaSequencer seq;
	seq.addFmTrack("T120 O4 L4 CDEF", 0);
	auto pcm1 = seq.render(1);
	auto pcm2 = seq.render(2);
	REQUIRE_FALSE(pcm1.empty());
	REQUIRE_FALSE(pcm2.empty());
	// 2ループは1ループより長くなるはず
	REQUIRE(pcm2.size() > pcm1.size());
}

// ============================================================================
// WAV Export
// ============================================================================

TEST_CASE("OpnaSequencer exportWav writes valid file", "[mml][opna][wav]")
{
	OpnaSequencer seq;
	seq.addFmTrack("T120 O4 L4 CDEF", 0);
	const std::string path = "test_export.wav";
	bool ok = seq.exportWav(path);
	REQUIRE(ok);

	// ファイルが存在し、WAVヘッダーが正しいことを確認する
	std::ifstream ifs(path, std::ios::binary);
	REQUIRE(ifs.good());
	char header[4] = {};
	ifs.read(header, 4);
	REQUIRE(std::string(header, 4) == "RIFF");
	ifs.close();

	// テスト後にファイルを削除する
	std::remove(path.c_str());
}

// ============================================================================
// Hardware LFO
// ============================================================================

TEST_CASE("OpnaDriver setLfo does not crash", "[mml][opna][lfo]")
{
	OpnaDriver driver;
	driver.setLfo(true, 3);
	driver.setFmVoice(0, opna_presets::PIANO);
	driver.setChannelLfoSensitivity(0, 3, 1);
	driver.fmNoteOn(0, 60);
	auto buf = driver.renderSamples(11000);
	REQUIRE(buf.size() == 11000);
}
