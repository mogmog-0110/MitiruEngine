/// @file TestChipAy8910.cpp
/// @brief AY-3-8910 / YM2149 (ymfm) の Device と Driver の検査

#include <catch2/catch_test_macros.hpp>

#include "ChipPeriodCheck.hpp"

#include <mitiru_mml/Ay8910Driver.hpp>

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
	return buildSong(std::make_unique<Ay8910Driver>(), AY8910_SONG, std::size(AY8910_SONG));
}

} // namespace

TEST_CASE("Ay8910Driver chooses the nearest period and the chip plays that period", "[mml][ay8910][pitch]")
{
	Ay8910Driver driver;
	checkPeriodChip(driver, 0, 0,
		[](int key) { return Ay8910Driver::keyToPeriod(key); },
		[](int period) { return Ay8910Driver::periodToHz(period); });
	REQUIRE(Ay8910Driver::keyToPeriod(69) == 254);
}

TEST_CASE("Ay8910Driver volume zero is silent and voice 1 makes noise", "[mml][ay8910]")
{
	Ay8910Driver driver;
	REQUIRE(measureNote(driver, 0, 69, 0, 0).rmsDb < -80.0);
	REQUIRE(measureNote(driver, 0, 72, 1, 15).rmsDb > -40.0);
	// 矩形波とノイズの重ねは、両方が High のときだけ出力されるので、矩形波だけより小さい
	const double tone = measureNote(driver, 0, 69, 0, 15).rmsDb;
	const double both = measureNote(driver, 0, 69, 2, 15).rmsDb;
	REQUIRE(both < tone);
	REQUIRE(both > -50.0);
}

TEST_CASE("Ay8910Driver hardware envelope shapes the volume", "[mml][ay8910]")
{
	// 形状 0x0D (立ち上がって保持) は最大で止まり、形状 0x09 (立ち下がって保持) は無音で止まる
	for (const int shape : {0x0D, 0x09})
	{
		Ay8910Driver driver;
		driver.setParam(0, chip_param::ENVELOPE, shape, 0x0400);
		driver.noteOn(0, 69, 15);
		const DriverTone t = measureSounding(driver, 0.8);
		INFO("shape " << shape << " rms " << t.rmsDb);
		if (shape == 0x0D)
		{
			REQUIRE(t.rmsDb > -30.0);
		}
		else
		{
			REQUIRE(t.rmsDb < -80.0);
		}
	}
}

TEST_CASE("Ay8910Driver song renders the same PCM every time and matches the recorded hash", "[mml][ay8910][hash]")
{
	checkSongHash("ay8910_song", &buildTestSong);
}

TEST_CASE("Ay8910Driver song is not silent and stays under full scale", "[mml][ay8910]")
{
	checkSongAudible(&buildTestSong);
}

TEST_CASE("Ay8910Driver Device saves and restores its state", "[mml][ay8910]")
{
	checkSaveRestore<Ay8910Driver>();
}

TEST_CASE("Ay8910Driver pan sends a voice to one side", "[mml][ay8910]")
{
	checkPanSplit<Ay8910Driver>(0, 69);
}

TEST_CASE("Print Ay8910Driver hash", "[.emit]")
{
	auto seq = buildTestSong();
	const RenderedHash h = hashRender(*seq);
	std::cout << "EMIT {\"ay8910_song\", " << h.frames << "u, \"" << h.sha256 << "\"},\n";
}

TEST_CASE("Print Ay8910Driver pitch table", "[.report]")
{
	std::cout << "REPORT ay8910 key idealHz period chipHz measuredHz centsVs12TET\n";
	Ay8910Driver driver;
	for (int key = PERIOD_FIRST_KEY; key <= PERIOD_LAST_KEY; ++key)
	{
		const int period = Ay8910Driver::keyToPeriod(key);
		const DriverTone t = measureNote(driver, 0, key, 0);
		std::cout << "REPORT ay8910 " << key << " " << keyHz(key) << " " << period << " "
			<< Ay8910Driver::periodToHz(period) << " " << t.peakHz << " " << centsBetween(t.peakHz, keyHz(key)) << "\n";
	}
}
