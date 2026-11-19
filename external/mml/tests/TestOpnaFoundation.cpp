/// @file TestOpnaFoundation.cpp
/// @brief OPNA を Device と Driver の 2 層に載せ直した結果の検査 (従来の出力との一致を含む)

#include <catch2/catch_test_macros.hpp>

#include "BaselineHashes.hpp"
#include "BaselineSongs.hpp"
#include "DummyChip.hpp"
#include "PcmCompare.hpp"
#include "Sha256.hpp"

#include <mitiru_mml/MitiruMML.hpp>
#include <mitiru_mml/MultiChipSequencer.hpp>
#include <mitiru_mml/OpnaDevice.hpp>
#include <mitiru_mml/OpnaDriver.hpp>
#include <mitiru_mml/OpnaSequencer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

using namespace mitiru_mml;
using namespace mitiru_mml_test;

namespace
{

float peak(const std::vector<float>& v)
{
	float p = 0.0f;
	for (const float s : v) p = std::max(p, std::abs(s));
	return p;
}

int peak(const PcmBuffer& v)
{
	int p = 0;
	for (const auto s : v) p = std::max(p, std::abs(static_cast<int>(s)));
	return p;
}

} // namespace

// ============================================================================
// 従来の出力との一致
// ============================================================================

TEST_CASE("OpnaSequencer render matches the recorded hash for every song", "[mml][baseline]")
{
	for (const auto& song : baselineSongs())
	{
		if (song.name == "long_five_track")
		{
			continue; // 長いので [long] のテストで確かめる
		}
		INFO("song: " << song.name);
		const auto pcm = buildSequencer(song).render(song.loopCount);
		const auto* expected = findBaseline(song.name);
		REQUIRE(expected != nullptr);
		REQUIRE(pcm.size() == expected->samples);
		REQUIRE(Sha256::ofPcm(pcm) == expected->sha256);
	}
}

TEST_CASE("OpnaSequencer long song matches the recorded hash", "[mml][baseline][long]")
{
	for (const auto& song : baselineSongs())
	{
		if (song.name != "long_five_track")
		{
			continue;
		}
		const auto pcm = buildSequencer(song).render(song.loopCount);
		const auto* expected = findBaseline(song.name);
		REQUIRE(expected != nullptr);
		REQUIRE(pcm.size() == expected->samples);
		REQUIRE(Sha256::ofPcm(pcm) == expected->sha256);
	}
}

TEST_CASE("OpnaSequencer legacy options reproduce the output from before the defaults changed", "[mml][baseline]")
{
	for (const auto& song : baselineSongs())
	{
		if (song.name == "long_five_track")
		{
			continue;
		}
		INFO("song: " << song.name);
		auto seq = buildSequencer(song);
		OpnaSequencer::RenderOptions options;
		options.stableEventOrder = false;
		options.isolateSsgControl = false;
		seq.setRenderOptions(options);
		const auto pcm = seq.render(song.loopCount);
		const BaselineEntry* expected = nullptr;
		for (const auto& entry : BASELINE_HASHES_BEFORE_DEFAULTS)
		{
			if (song.name == entry.name) expected = &entry;
		}
		REQUIRE(expected != nullptr);
		REQUIRE(pcm.size() == expected->samples);
		REQUIRE(Sha256::ofPcm(pcm) == expected->sha256);
	}
}

// ============================================================================
// Device (層 1)
// ============================================================================

TEST_CASE("OpnaDevice reports the native rate derived from the clock", "[mml][foundation][opna]")
{
	OpnaDevice device;
	REQUIRE(device.nativeRate() == OpnaDevice::CLOCK / 8);
	REQUIRE(std::string(device.name()) == "YM2608");
}

TEST_CASE("OpnaDevice state round trip reproduces the same samples", "[mml][foundation][opna]")
{
	OpnaDriver driver;
	driver.setFmVoice(0, opna_presets::PIANO);
	driver.fmNoteOn(0, 60);
	std::vector<float> scratch(40000);
	driver.opnaDevice().generate(scratch.data(), 20000);

	const auto saved = driver.opnaDevice().saveState();
	REQUIRE_FALSE(saved.empty());

	std::vector<float> first(40000);
	driver.opnaDevice().generate(first.data(), 20000);

	REQUIRE(driver.opnaDevice().loadState(saved));
	std::vector<float> second(40000);
	driver.opnaDevice().generate(second.data(), 20000);
	REQUIRE(first == second);
	REQUIRE(peak(first) > 0.01f);
}

TEST_CASE("OpnaDevice register writes alone make sound through the Device interface", "[mml][foundation][opna]")
{
	// Driver を通さず、Device の write() だけで SSG のトーンを鳴らす (VGM 再生と同じ使い方)
	OpnaDevice device;
	device.write(0, 0x29, 0x9F);
	device.write(0, 0x00, 0x40);  // ch A 周期 下位
	device.write(0, 0x01, 0x00);  // 上位
	device.write(0, 0x07, 0x38 | 0x06); // ch A のトーンだけ有効
	device.write(0, 0x08, 0x0F);  // 音量
	std::vector<float> out(20000);
	device.generate(out.data(), 10000);
	REQUIRE(peak(out) > 0.01f);
}

// ============================================================================
// Driver (層 2)
// ============================================================================

TEST_CASE("OpnaDriver exposes FM SSG and drum channels through ChipDriver", "[mml][foundation][opna]")
{
	OpnaDriver driver;
	ChipDriver& chip = driver;
	REQUIRE(chip.channelCount() == 15);
	REQUIRE(chip.channelCaps(0).pitchSlide);
	REQUIRE_FALSE(chip.channelCaps(6).pitchSlide);
	REQUIRE(chip.channelCaps(6).hardwareEnvelope);
	REQUIRE(chip.channelCaps(14).valid);
	REQUIRE_FALSE(chip.channelCaps(15).valid);
	REQUIRE_FALSE(chip.channelCaps(-1).valid);

	// FM、SSG、ドラムのそれぞれが、ChipDriver の呼び出しだけで音を出す
	for (const int channel : {0, 6, 9, 10})
	{
		OpnaDriver d;
		d.reset();
		d.setVoice(0, 2);
		d.noteOn(channel, 69, 15);
		std::vector<float> out(24000);
		d.device().generate(out.data(), 12000);
		INFO("channel " << channel);
		REQUIRE(peak(out) > 0.01f);
	}
}

TEST_CASE("OpnaDriver ignores a channel that does not exist", "[mml][foundation][opna]")
{
	OpnaDriver driver;
	driver.noteOn(-1, 60, 15);
	driver.noteOn(99, 60, 15);
	driver.noteOff(99);
	std::vector<float> out(4000);
	driver.device().generate(out.data(), 2000);
	REQUIRE(peak(out) == 0.0f);
}

TEST_CASE("OpnaDriver pan survives a later change of LFO sensitivity", "[mml][foundation][opna]")
{
	OpnaDriver driver;
	driver.setFmVoice(0, opna_presets::PIANO);
	driver.setFmPan(0, 0); // 左のみ
	driver.setChannelLfoSensitivity(0, 3, 1);
	driver.fmNoteOn(0, 60);
	std::vector<float> out(40000);
	driver.device().generate(out.data(), 20000);
	float left = 0.0f;
	float right = 0.0f;
	for (std::size_t i = 0; i < 20000; ++i)
	{
		left = std::max(left, std::abs(out[i * 2]));
		right = std::max(right, std::abs(out[i * 2 + 1]));
	}
	REQUIRE(left > 0.01f);
	REQUIRE(right == 0.0f);
}

// ============================================================================
// シーケンサー
// ============================================================================

TEST_CASE("OpnaSequencer stable event order gives the same result on every standard library", "[mml][foundation][order]")
{
	BaselineSong song;
	for (const auto& s : baselineSongs())
	{
		if (s.name == "technique_mix") song = s;
	}
	auto seq = buildSequencer(song);
	OpnaSequencer::RenderOptions options;
	options.stableEventOrder = true;
	seq.setRenderOptions(options);
	const auto a = seq.render(song.loopCount);
	const auto b = seq.render(song.loopCount);
	REQUIRE(a == b);
	REQUIRE_FALSE(a.empty());
}

TEST_CASE("OpnaSequencer isolateSsgControl keeps an SSG volume from changing the FM level", "[mml][foundation][ssgvolume]")
{
	auto render = [](bool isolate)
	{
		OpnaSequencer seq;
		seq.addFmTrack("T120 O4 L4 V15 CDEF", 0);
		seq.addSsgTrack("T120 O5 L4 V1 R R R R"); // 音は出さず、V だけを送る
		OpnaSequencer::RenderOptions options;
		options.isolateSsgControl = isolate;
		seq.setRenderOptions(options);
		return seq.render();
	};
	const auto legacy = render(false);
	const auto isolated = render(true);
	// 従来は SSG の V1 が FM の音量 (TL) を書き換えて小さくなる。分離すれば FM 自身の V15 が効く
	REQUIRE(peak(isolated) > peak(legacy) * 2);
}

TEST_CASE("OpnaSequencer renderStereo places a left-panned track on the left", "[mml][foundation][opna]")
{
	OpnaSequencer seq;
	seq.addFmTrack("T120 O4 L4 P0 CDEF", 0);
	const StereoBuffer stereo = seq.renderStereo();
	REQUIRE(stereo.sampleRate == 44100);
	float left = 0.0f;
	float right = 0.0f;
	for (std::size_t i = 0; i < stereo.frames(); ++i)
	{
		left = std::max(left, std::abs(stereo.samples[i * 2]));
		right = std::max(right, std::abs(stereo.samples[i * 2 + 1]));
	}
	REQUIRE(left > 0.05f);
	REQUIRE(right < left * 0.01f);
}

TEST_CASE("OpnaSequencer renderWithAnalysis gives the same result with and without threads", "[mml][foundation][opna]")
{
	auto render = [](bool parallel)
	{
		OpnaSequencer seq;
		seq.addFmTrack("T120 O4 L4 CDEF", 0);
		seq.addSsgTrack("T120 O5 L8 CDEFGAB>C");
		seq.addRhythmTrack("T120 L8 BHSHBHSH");
		OpnaSequencer::RenderOptions options;
		options.parallelAnalysis = parallel;
		seq.setRenderOptions(options);
		return seq.renderWithAnalysis();
	};
	const auto serial = render(false);
	const auto threaded = render(true);
	REQUIRE(serial.mixed == threaded.mixed);
	REQUIRE(serial.trackPcm == threaded.trackPcm);
	REQUIRE(serial.trackAmplitude == threaded.trackAmplitude);
	REQUIRE(serial.trackPcm.size() == 3);
}

TEST_CASE("OPNA and a dummy chip play on one timeline", "[mml][foundation][multichip]")
{
	MultiChipSequencer seq;
	const int opna = seq.addChip(std::make_unique<OpnaDriver>());
	seq.addTrack(opna, 0, "T120 O4 L4 CDEF", 0);
	seq.addRhythmTrack(opna, OpnaDriver::DRUM_CHANNEL_BASE, "T120 L8 BHSHBHSH");
	const int dummy = seq.addChip(std::make_unique<DummySineDriver>(55930), 0.5f);
	seq.addTrack(dummy, 0, "T120 O3 L4 V15 CDEF");
	const StereoBuffer mix = seq.render();
	REQUIRE(mix.sampleRate == 44100);
	// 4 分音符 4 つ (120 BPM) = 2 秒
	REQUIRE(mix.frames() > 88000);
	REQUIRE(peak(mix.samples) > 0.05f);
}
