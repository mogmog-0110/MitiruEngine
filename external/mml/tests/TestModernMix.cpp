/// @file TestModernMix.cpp
/// @brief リミッター、サイドチェーン、ダッキング、スウィング、ステレオ幅、ディザを数字で確かめる

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Analysis.hpp"

#include <mitiru_mml/Dynamics.hpp>
#include <mitiru_mml/EventTransforms.hpp>
#include <mitiru_mml/MmlEventBuilder.hpp>
#include <mitiru_mml/MmlParser.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace mitiru_mml;
using namespace mitiru_mml_test;

namespace
{

constexpr std::uint32_t RATE = 44100;

StereoBuffer sineBuffer(double seconds, double hz, double amp)
{
	StereoBuffer b;
	b.sampleRate = RATE;
	b.samples.resize(static_cast<std::size_t>(seconds * RATE) * 2);
	for (std::size_t i = 0; i < b.frames(); ++i)
	{
		const auto v = static_cast<float>(amp * std::sin(TWO_PI * hz * static_cast<double>(i) / RATE));
		b.samples[i * 2] = v;
		b.samples[i * 2 + 1] = v;
	}
	return b;
}

void addBurst(StereoBuffer& b, double startSec, double lengthSec, double hz, double amp)
{
	const auto s = static_cast<std::size_t>(startSec * RATE);
	const auto n = static_cast<std::size_t>(lengthSec * RATE);
	for (std::size_t i = 0; i < n && s + i < b.frames(); ++i)
	{
		const auto v = static_cast<float>(amp * std::sin(TWO_PI * hz * static_cast<double>(s + i) / RATE));
		b.samples[(s + i) * 2] = v;
		b.samples[(s + i) * 2 + 1] = v;
	}
}

} // namespace

TEST_CASE("Limiter keeps every sample under the ceiling and reports the reduction", "[mml][modern][mix]")
{
	StereoBuffer b = sineBuffer(2.0, 220.0, 0.3);
	addBurst(b, 0.5, 0.05, 220.0, 1.6);
	LimiterSettings s;
	s.ceilingDb = -1.0f;
	const float reduction = applyLimiter(b, s);
	const double ceiling = std::pow(10.0, -1.0 / 20.0);
	INFO("peak " << peakOfStereo(b) << ", ceiling " << ceiling << ", reduction " << reduction << " dB");
	CHECK(peakOfStereo(b) <= ceiling * (1.0 + 1.0e-6));
	CHECK(peakOfStereo(b) > ceiling * 0.995);
	// 1.6 を 0.891 に収めるので約 -5.1 dB
	CHECK_THAT(static_cast<double>(reduction), Catch::Matchers::WithinAbs(-5.1, 0.3));
}

TEST_CASE("Limiter leaves a quiet signal untouched and starts before the peak", "[mml][modern][mix]")
{
	StereoBuffer quiet = sineBuffer(1.0, 330.0, 0.4);
	const auto before = quiet.samples;
	CHECK(applyLimiter(quiet, LimiterSettings{}) == 0.0f);
	CHECK(quiet.samples == before);

	StereoBuffer b = sineBuffer(1.0, 220.0, 0.3);
	addBurst(b, 0.5, 0.02, 220.0, 1.6);
	const auto original = b.samples;
	LimiterSettings s;
	s.lookaheadMs = 3.0f;
	static_cast<void>(applyLimiter(b, s));
	// 3 ms (132 サンプル) 先読み: 最大のピーク (バーストの開始の約 55 サンプル後) に届く前に、ゲインは下がり始めている
	const std::size_t burst = static_cast<std::size_t>(0.5 * RATE);
	double ratioAtMinus100 = 1.0;
	for (std::size_t i = burst - 40; i < burst; ++i)
	{
		if (std::fabs(original[i * 2]) > 0.2f)
		{
			ratioAtMinus100 = std::min(ratioAtMinus100, static_cast<double>(b.samples[i * 2] / original[i * 2]));
		}
	}
	INFO("gain just before the burst: " << ratioAtMinus100);
	CHECK(ratioAtMinus100 < 1.0);
	CHECK(ratioAtMinus100 > 0.6);
}

TEST_CASE("Limiter recovers its gain in about the release time", "[mml][modern][mix]")
{
	StereoBuffer b = sineBuffer(2.0, 220.0, 0.3);
	addBurst(b, 0.5, 0.02, 220.0, 1.6);
	const auto original = b.samples;
	LimiterSettings s;
	s.releaseMs = 80.0f;
	static_cast<void>(applyLimiter(b, s));
	// バーストの終わりのゲインから、90% 戻るまでの時間
	const std::size_t end = static_cast<std::size_t>(0.52 * RATE);
	const auto gainAt = [&](std::size_t centre)
	{
		double best = 0.0;
		for (std::size_t i = centre; i < centre + 200; ++i)
		{
			if (std::fabs(original[i * 2]) > 0.25f)
			{
				best = static_cast<double>(b.samples[i * 2] / original[i * 2]);
			}
		}
		return best;
	};
	const double g0 = gainAt(end + 220);
	REQUIRE(g0 > 0.0);
	std::size_t recovered = 0;
	for (std::size_t off = 0; off < 22050; off += 100)
	{
		const double g = gainAt(end + 220 + off);
		if (g > 0.0 && (1.0 - g) < 0.1 * (1.0 - g0))
		{
			recovered = off;
			break;
		}
	}
	INFO("gain right after burst " << g0 << ", 90% recovered after " << recovered / 44.1 << " ms");
	CHECK(recovered > 3000);
	CHECK(recovered < 4500);
}

TEST_CASE("Sidechain compressor ducks the target by the key level and releases", "[mml][modern][mix]")
{
	StereoBuffer target = sineBuffer(3.0, 440.0, 0.3);
	StereoBuffer key;
	key.sampleRate = RATE;
	key.samples.assign(target.samples.size(), 0.0f);
	addBurst(key, 1.0, 0.05, 80.0, 0.8);
	SidechainSettings s;
	s.thresholdDb = -30.0f;
	s.ratio = 8.0f;
	s.attackMs = 2.0f;
	s.releaseMs = 150.0f;
	const float worst = applySidechainCompressor(target, key, s);
	const auto mono = monoOf(target);
	const double before = rmsDbOf(mono, 22050 - 4410, 4410);
	const double during = rmsDbOf(mono, 44100 + 400, 800);
	const double late = rmsDbOf(mono, 44100 + 66150, 4410);
	INFO("worst " << worst << " dB, before " << before << ", during " << during << ", late " << late);
	// 0.8 のピーク = -1.9 dB は、しきい値 -30 dB の 28 dB 上。8:1 なら 24.5 dB 下がる
	CHECK(worst < -18.0);
	CHECK(worst > -26.0);
	CHECK(during < before - 15.0);
	CHECK(std::fabs(late - before) < 0.5);
}

TEST_CASE("Sidechain key low-pass makes the compressor ignore a bright key", "[mml][modern][mix]")
{
	StereoBuffer target = sineBuffer(2.0, 440.0, 0.3);
	StereoBuffer key;
	key.sampleRate = RATE;
	key.samples.assign(target.samples.size(), 0.0f);
	addBurst(key, 0.5, 0.2, 9000.0, 0.8);
	StereoBuffer plain = target;
	SidechainSettings s;
	s.keyLowPassHz = 0.0f;
	const float noFilter = applySidechainCompressor(plain, key, s);
	StereoBuffer filtered = target;
	s.keyLowPassHz = 150.0f;
	const float withFilter = applySidechainCompressor(filtered, key, s);
	INFO("without filter " << noFilter << " dB, with 150 Hz low-pass " << withFilter << " dB");
	CHECK(noFilter < -15.0);
	CHECK(withFilter > -2.0);
}

TEST_CASE("Event ducking curve has the set depth, attack and release", "[mml][modern][mix]")
{
	DuckSettings d;
	d.depthDb = -9.0f;
	d.attackMs = 4.0f;
	d.releaseMs = 180.0f;
	const std::uint64_t t = 22050;
	const auto curve = buildDuckCurve(88200, {t, t + 22050}, RATE, d);
	CHECK(curve[t - 1] == 1.0f);
	const double floorGain = std::pow(10.0, -9.0 / 20.0);
	// 直線のアタック 4 ms (176 サンプル)
	CHECK_THAT(static_cast<double>(curve[t + 88]), Catch::Matchers::WithinAbs(0.5 * (1.0 + floorGain), 0.01));
	CHECK_THAT(static_cast<double>(curve[t + 176]), Catch::Matchers::WithinAbs(floorGain, 0.005));
	// 深さ (dB で見た最小値)
	float lowest = 1.0f;
	for (std::size_t i = t; i < t + 22050; ++i) lowest = std::min(lowest, curve[i]);
	CHECK_THAT(static_cast<double>(linearToDb(lowest)), Catch::Matchers::WithinAbs(-9.0, 0.05));
	// リリース: アタックのあと 180 ms で、深さの 10% (-0.9 dB) まで戻る
	CHECK_THAT(static_cast<double>(linearToDb(curve[t + 176 + 7938])), Catch::Matchers::WithinAbs(-0.9, 0.15));
	// 次の合図の直前にはほぼ戻っている。そこからもう一度同じ深さまで下がる
	CHECK(curve[t + 22050 - 1] > 0.99f);
	CHECK_THAT(static_cast<double>(linearToDb(curve[t + 22050 + 176])), Catch::Matchers::WithinAbs(-9.0, 0.1));
}

TEST_CASE("Swing moves only the offbeats and keeps the order", "[mml][modern][mix]")
{
	CHECK(swingTick(0, 8, 0.667f) == 0);
	CHECK(swingTick(48, 8, 0.667f) == 48);
	CHECK(swingTick(24, 8, 0.667f) == 32);
	CHECK(swingTick(12, 8, 0.5f) == 12);
	std::int64_t previous = -1;
	for (std::int64_t tick = 0; tick < 2000; ++tick)
	{
		const std::int64_t s = swingTick(tick, 8, 0.62f);
		CHECK(s >= previous);
		previous = s;
	}
	// 16 分のグリッドでは、12 tick の裏が動く
	CHECK(swingTick(12, 16, 0.667f) > 12);
	CHECK(swingTick(24, 16, 0.667f) == 24);
}

TEST_CASE("Swing delays the offbeat note by the ratio and not the downbeat", "[mml][modern][mix]")
{
	TrackTarget t;
	t.caps.valid = true;
	t.extended = true;
	auto events = MmlEventBuilder::fromMelodic(MmlEventBuilder::expandLoops(MmlParser::parse("L8 CCCC")), t);
	applySwing(events, 8, 0.6667f);
	std::vector<std::int64_t> onsets;
	for (const auto& e : events)
	{
		if (e.type == SeqEventType::NoteOn) onsets.push_back(e.tick);
	}
	REQUIRE(onsets.size() == 4);
	CHECK(onsets[0] == 0);
	CHECK(onsets[1] == 32);
	CHECK(onsets[2] == 48);
	CHECK(onsets[3] == 80);
	// 120 BPM では 1 tick = 459.375 サンプル。裏拍は 8 tick = 約 3675 サンプル遅れる
	CHECK_THAT(static_cast<double>(onsets[1] - 24) * 22050.0 / 48.0, Catch::Matchers::WithinAbs(3675.0, 1.0));
}

TEST_CASE("Text loops expand nested repeats and drop comments", "[mml][modern][mix]")
{
	CHECK(expandTextLoops("[B S]3") == "B SB SB S");
	CHECK(expandTextLoops("[B[S]2]2") == "BSSBSS");
	CHECK(expandTextLoops("[B S]") == "B SB S");
	CHECK(expandTextLoops("B ; kick drum\nS") == "B \nS");
	CHECK(expandTextLoops("B]") == "B]");
}

TEST_CASE("Stereo width of zero is mono and of two doubles the side", "[mml][modern][mix]")
{
	StereoBuffer b;
	b.sampleRate = RATE;
	b.samples = {0.5f, 0.1f, -0.3f, 0.3f};
	StereoBuffer mono = b;
	applyStereoWidth(mono, 0.0f);
	CHECK(mono.samples[0] == mono.samples[1]);
	CHECK_THAT(static_cast<double>(mono.samples[0]), Catch::Matchers::WithinAbs(0.3, 1.0e-6));
	StereoBuffer wide = b;
	applyStereoWidth(wide, 2.0f);
	CHECK_THAT(static_cast<double>(wide.samples[0] - wide.samples[1]), Catch::Matchers::WithinAbs(0.8, 1.0e-6));
	StereoBuffer same = b;
	applyStereoWidth(same, 1.0f);
	CHECK_THAT(static_cast<double>(same.samples[0]), Catch::Matchers::WithinAbs(0.5, 1.0e-6));
}

TEST_CASE("Dither is deterministic and adds about half an LSB of noise", "[mml][modern][mix]")
{
	StereoBuffer silence;
	silence.sampleRate = RATE;
	silence.samples.assign(88200, 0.0f);
	const auto a = toPcm16StereoDithered(silence, true);
	const auto b = toPcm16StereoDithered(silence, true);
	CHECK(a == b);
	double acc = 0.0;
	for (const auto v : a) acc += static_cast<double>(v) * v;
	const double rms = std::sqrt(acc / static_cast<double>(a.size()));
	INFO("dither rms " << rms << " LSB");
	CHECK(rms > 0.3);
	CHECK(rms < 0.7);
	const auto plain = toPcm16StereoDithered(silence, false);
	CHECK(std::all_of(plain.begin(), plain.end(), [](std::int16_t v) { return v == 0; }));
}
