/// @file TestChipSn76489.cpp
/// @brief SN76489 (emu76489) の Device と Driver の検査

#include <catch2/catch_test_macros.hpp>

#include "ChipPeriodCheck.hpp"

#include <mitiru_mml/Sn76489Driver.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>

using namespace mitiru_mml;
using namespace mitiru_mml_test;

namespace
{

std::unique_ptr<MultiChipSequencer> buildTestSong()
{
	return buildSong(std::make_unique<Sn76489Driver>(), SN76489_SONG, std::size(SN76489_SONG));
}

} // namespace

TEST_CASE("Sn76489Driver chooses the nearest period and the chip plays that period", "[mml][sn76489][pitch]")
{
	Sn76489Driver driver;
	checkPeriodChip(driver, 0, -1,
		[](int key) { return Sn76489Driver::keyToPeriod(key); },
		[](int period) { return Sn76489Driver::periodToHz(period); });
	REQUIRE(Sn76489Driver::keyToPeriod(69) == 254);
}

TEST_CASE("Sn76489Driver pitch of C and A in octaves 3 to 6 stays within the quantization of the period", "[mml][sn76489][pitch]")
{
	// 10bit の周期の整数化のため、A4 でも約 1.6 セントずれる。±1 セントに入る音は、周期が十分大きい低い音に限られる
	Sn76489Driver driver;
	for (int octave = 3; octave <= 6; ++octave)
	{
		for (const int semitone : {0, 9})
		{
			const int key = (octave + 1) * 12 + semitone;
			const int period = Sn76489Driver::keyToPeriod(key);
			const DriverTone t = measureNote(driver, 0, key, -1);
			const double cents = centsBetween(t.peakHz, keyHz(key));
			INFO("key " << key << " period " << period << " cents " << cents);
			REQUIRE(std::abs(cents) <= quantizationBoundCents(period) + 0.3);
		}
	}
}

TEST_CASE("Sn76489Driver volume steps are 2 dB apart and zero is silent", "[mml][sn76489]")
{
	Sn76489Driver driver;
	const double full = measureNote(driver, 0, 69, -1, 15).rmsDb;
	const double minus4 = measureNote(driver, 0, 69, -1, 11).rmsDb;
	REQUIRE(std::abs((full - minus4) - 8.0) < 1.5);
	REQUIRE(measureNote(driver, 0, 69, -1, 0).rmsDb < -80.0);
}

TEST_CASE("Sn76489Driver noise channel makes broadband noise and the voice selects white or periodic", "[mml][sn76489]")
{
	Sn76489Driver driver;
	const DriverTone white = measureNote(driver, Sn76489Driver::NOISE_CHANNEL, 72, 0);
	REQUIRE(white.rmsDb > -40.0);
	const DriverTone periodic = measureNote(driver, Sn76489Driver::NOISE_CHANNEL, 72, 1);
	REQUIRE(periodic.rmsDb > -40.0);
}

TEST_CASE("Sn76489Device removes the DC offset and keeps the native rate of the chip", "[mml][sn76489]")
{
	Sn76489Device device;
	REQUIRE(device.nativeRate() == 3579545 / 16);
	REQUIRE(std::string(device.name()) == "SN76489");
	Sn76489Driver driver;
	driver.noteOn(0, 69, 15);
	std::vector<float> out(2 * 100000);
	driver.device().generate(out.data(), 100000);
	double mean = 0.0;
	for (std::size_t i = 20000; i < 100000; ++i)
	{
		mean += out[i * 2];
	}
	mean /= 80000.0;
	REQUIRE(std::abs(mean) < 0.02);
}

TEST_CASE("Sn76489Driver song renders the same PCM every time and matches the recorded hash", "[mml][sn76489][hash]")
{
	checkSongHash("sn76489_song", &buildTestSong);
}

TEST_CASE("Sn76489Driver song is not silent and stays under full scale", "[mml][sn76489]")
{
	checkSongAudible(&buildTestSong);
}

TEST_CASE("Sn76489Driver Device saves and restores its state", "[mml][sn76489]")
{
	checkSaveRestore<Sn76489Driver>();
}

TEST_CASE("Sn76489Driver pan sends a voice to one side of the Game Gear stereo mask", "[mml][sn76489]")
{
	checkPanSplit<Sn76489Driver>(0, 69);
}

TEST_CASE("Print Sn76489Driver hash", "[.emit]")
{
	auto seq = buildTestSong();
	const RenderedHash h = hashRender(*seq);
	std::cout << "EMIT {\"sn76489_song\", " << h.frames << "u, \"" << h.sha256 << "\"},\n";
}

TEST_CASE("Print Sn76489Driver pitch table", "[.report]")
{
	std::cout << "REPORT sn76489 key idealHz period chipHz measuredHz centsVs12TET\n";
	Sn76489Driver driver;
	for (int key = PERIOD_FIRST_KEY; key <= PERIOD_LAST_KEY; ++key)
	{
		const int period = Sn76489Driver::keyToPeriod(key);
		const DriverTone t = measureNote(driver, 0, key, -1);
		std::cout << "REPORT sn76489 " << key << " " << keyHz(key) << " " << period << " "
			<< Sn76489Driver::periodToHz(period) << " " << t.peakHz << " " << centsBetween(t.peakHz, keyHz(key)) << "\n";
	}
}
