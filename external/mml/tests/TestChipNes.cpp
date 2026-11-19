/// @file TestChipNes.cpp
/// @brief NES の APU (vgsound_emu) の Device と Driver の検査

#include <catch2/catch_test_macros.hpp>

#include "ChipPeriodCheck.hpp"

#include <mitiru_mml/NesApuDriver.hpp>

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
	return buildSong(std::make_unique<NesApuDriver>(), NES_SONG, std::size(NES_SONG));
}

} // namespace

TEST_CASE("NesApuDriver pulse pitch follows the programmed timer", "[mml][nes][pitch]")
{
	NesApuDriver driver;
	checkPeriodChip(driver, NesApuDriver::PULSE_1, 0,
		[](int key) { return NesApuDriver::keyToTimer(key, false) + 1; },
		[](int period) { return NesApuDriver::timerToHz(period - 1, false); });
	REQUIRE(NesApuDriver::keyToTimer(69, false) == 253);
}

TEST_CASE("NesApuDriver triangle pitch follows the programmed timer", "[mml][nes][pitch]")
{
	NesApuDriver driver;
	for (int key = 45; key <= 84; ++key)
	{
		const int timer = NesApuDriver::keyToTimer(key, true);
		const double chipHz = NesApuDriver::timerToHz(timer, true);
		const DriverTone t = measureNote(driver, NesApuDriver::TRIANGLE, key, 0);
		INFO("key " << key << " timer " << timer << " chip " << chipHz << " measured " << t.peakHz);
		REQUIRE(std::abs(centsBetween(t.peakHz, chipHz)) <= 0.3);
		REQUIRE(std::abs(centsBetween(t.peakHz, keyHz(key))) <= quantizationBoundCents(timer + 1.0) + 0.3);
	}
}

TEST_CASE("NesApuDriver second pulse channel plays the same pitch as the first", "[mml][nes][pitch]")
{
	NesApuDriver driver;
	const double first = measureNote(driver, NesApuDriver::PULSE_1, 69, 0).peakHz;
	const double second = measureNote(driver, NesApuDriver::PULSE_2, 69, 0).peakHz;
	REQUIRE(std::abs(centsBetween(first, second)) <= 0.3);
}

TEST_CASE("NesApuDriver volume zero and note off are silent, and duty changes the timbre", "[mml][nes]")
{
	NesApuDriver driver;
	REQUIRE(measureNote(driver, NesApuDriver::PULSE_1, 69, 0, 0).rmsDb < -80.0);
	REQUIRE(measureNote(driver, NesApuDriver::PULSE_1, 69, 0, 15).rmsDb > -30.0);

	driver.reset();
	driver.setVolume(NesApuDriver::PULSE_1, 15);
	driver.noteOn(NesApuDriver::PULSE_1, 69, 15);
	driver.noteOff(NesApuDriver::PULSE_1);
	REQUIRE(measureSounding(driver, 0.4).rmsDb < -70.0);

	// 12.5% の矩形波は 50% より実効値が小さい
	const double duty50 = measureNote(driver, NesApuDriver::PULSE_1, 69, 0, 15).rmsDb;
	const double duty12 = measureNote(driver, NesApuDriver::PULSE_1, 69, 2, 15).rmsDb;
	REQUIRE(duty12 < duty50 - 1.0);
}

TEST_CASE("NesApuDriver noise channel makes noise and the triangle makes sound", "[mml][nes]")
{
	NesApuDriver driver;
	REQUIRE(measureNote(driver, NesApuDriver::NOISE, 72, 0, 15).rmsDb > -40.0);
	REQUIRE(measureNote(driver, NesApuDriver::NOISE, 72, 1, 15).rmsDb > -40.0);
	REQUIRE(measureNote(driver, NesApuDriver::TRIANGLE, 57, 0, 15).rmsDb > -40.0);
	REQUIRE(measureNote(driver, NesApuDriver::TRIANGLE, 57, 0, 0).rmsDb < -80.0);
}

TEST_CASE("NesApuDevice keeps DPCM silent", "[mml][nes]")
{
	NesApuDriver driver;
	driver.noteOn(NesApuDriver::PULSE_1, 69, 0); // 音量 0 のパルスだけ
	driver.setVolume(NesApuDriver::PULSE_1, 0);
	driver.device().write(0, 0x10, 0x0F);
	driver.device().write(0, 0x12, 0x00);
	driver.device().write(0, 0x13, 0x10);
	driver.device().write(0, 0x15, 0x1F); // DPCM を有効にする。サンプルを読む口が無いので鳴らない
	REQUIRE(measureSounding(driver, 0.4).rmsDb < -60.0);
}

TEST_CASE("NesApuDriver song renders the same PCM every time and matches the recorded hash", "[mml][nes][hash]")
{
	checkSongHash("nes_song", &buildTestSong);
}

TEST_CASE("NesApuDriver song is not silent and stays under full scale", "[mml][nes]")
{
	checkSongAudible(&buildTestSong);
}

TEST_CASE("Print NesApuDriver hash", "[.emit]")
{
	auto seq = buildTestSong();
	const RenderedHash h = hashRender(*seq);
	std::cout << "EMIT {\"nes_song\", " << h.frames << "u, \"" << h.sha256 << "\"},\n";
}

TEST_CASE("Print NesApuDriver pitch table", "[.report]")
{
	std::cout << "REPORT nes key idealHz period chipHz measuredHz centsVs12TET\n";
	NesApuDriver driver;
	for (int key = PERIOD_FIRST_KEY; key <= PERIOD_LAST_KEY; ++key)
	{
		const int timer = NesApuDriver::keyToTimer(key, false);
		const DriverTone t = measureNote(driver, 0, key, 0);
		std::cout << "REPORT nes " << key << " " << keyHz(key) << " " << timer << " "
			<< NesApuDriver::timerToHz(timer, false) << " " << t.peakHz << " " << centsBetween(t.peakHz, keyHz(key)) << "\n";
	}
}
