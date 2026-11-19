/// @file TestPitch.cpp
/// @brief FM の音の高さの検査 (FFT で測った周波数が、440 Hz を基準にした 12 平均律の値に ±1 セントで合うこと)

#include <catch2/catch_test_macros.hpp>

#include "PitchMeasure.hpp"

#include <mitiru_mml/OpnaDriver.hpp>
#include <mitiru_mml/OpnaPresets.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace mitiru_mml;
using namespace mitiru_mml_test;

TEST_CASE("OpnaDriver FM pitch matches equal temperament within one cent for A and C in octaves 3 to 6", "[mml][opna][pitch]")
{
	for (int octave = 3; octave <= 6; ++octave)
	{
		for (const int semitone : {0, 9}) // C と A
		{
			const int key = (octave + 1) * 12 + semitone;
			const double expected = expectedHz(key);
			const ToneMeasure m = measureFmNote(pureSineVoice(), key);
			INFO("key " << key << " expected " << expected << " Hz measured " << m.peakHz << " Hz");
			REQUIRE(std::abs(centsOff(m.peakHz, expected)) <= 1.0);
		}
	}
}

TEST_CASE("OpnaDriver FM pitch doubles with every octave and does not saturate", "[mml][opna][pitch]")
{
	// 修正前は約 6.93 kHz で頭打ちになり、A5 より上が全部同じ高さになっていた
	double previous = measureFmNote(pureSineVoice(), 45).peakHz; // A2
	for (int key = 57; key <= 93; key += 12)
	{
		const double current = measureFmNote(pureSineVoice(), key).peakHz;
		REQUIRE(std::abs(centsOff(current, previous * 2.0)) <= 2.0);
		previous = current;
	}
}

TEST_CASE("OpnaDriver noteToFnumBlock keeps F-Number in the top octave of its block", "[mml][opna][pitch]")
{
	for (int key = 36; key <= 96; ++key)
	{
		const auto fb = OpnaDriver::noteToFnumBlock(key);
		INFO("key " << key);
		REQUIRE(fb.fnum >= 1024);
		REQUIRE(fb.fnum < 2048);
		REQUIRE(fb.block <= 7);
	}
	// A4 = 440 Hz は block 4、F-Number 約 1040 (144 * 440 * 2^20 / (7987200 * 2^3) = 1039.8)
	const auto a4 = OpnaDriver::noteToFnumBlock(69);
	REQUIRE(a4.block == 4);
	REQUIRE(a4.fnum == 1040);
}

TEST_CASE("OpnaDriver SSG pitch is not changed by the FM pitch fix", "[mml][opna][pitch]")
{
	// SSG の A4 は周期の整数化で 441 Hz 付近になる (従来どおり)
	OpnaDriver driver;
	driver.setSsgEnable(true, false, false);
	driver.setSsgTone(0, 440.0f, 15);
	std::vector<float> out(998400 / 4 * 2);
	driver.device().generate(out.data(), 998400 / 4);
	// SSG の矩形波は正の側だけに振れるので、最大と最小の中間を上向きに横切る回数を数える
	float lo = out[0];
	float hi = out[0];
	for (std::size_t i = 0; i < out.size() / 2; ++i)
	{
		lo = std::min(lo, out[i * 2]);
		hi = std::max(hi, out[i * 2]);
	}
	const float mid = 0.5f * (lo + hi);
	REQUIRE(hi > lo);
	int crossings = 0;
	for (std::size_t i = 1; i < out.size() / 2; ++i)
	{
		if (out[(i - 1) * 2] < mid && out[i * 2] >= mid) ++crossings;
	}
	// 0.25 秒で 110 周期前後
	REQUIRE(crossings >= 108);
	REQUIRE(crossings <= 113);
}
