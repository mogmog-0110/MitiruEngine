/// @file TestModernSong.cpp
/// @brief 曲の文書、ステム、ダッキング、スウィング、アルペジエーター、同じ MML から同じ PCM であることの検査

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Analysis.hpp"
#include "ModernExamples.hpp"
#include "SyntheticSf2.hpp"

#include <mitiru_mml/ModernSongScript.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

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

ModernSong::Result renderText(const std::string& text, bool soundFont = false)
{
	auto parsed = ModernSongScript::parse(text, soundFont ? testSoundFont() : nullptr);
	REQUIRE(parsed.ok());
	return parsed.song.render();
}

const StereoBuffer& stemOf(const ModernSong::Result& r, const std::string& name)
{
	for (const auto& s : r.stems)
	{
		if (s.name == name) return s.audio;
	}
	FAIL("stem not found: " << name);
	return r.mix;
}

double peakDbOf(const std::vector<float>& x, std::size_t from, std::size_t count)
{
	double p = 0.0;
	for (std::size_t i = from; i < std::min(x.size(), from + count); ++i)
	{
		p = std::max(p, std::fabs(static_cast<double>(x[i])));
	}
	return toDb(p);
}

/// @brief 検査用の短い曲。サブベース (一定の音量の正弦波) とキックだけ
const char* DUCK_SONG = R"(
#tempo 120
#limiter off
#tail 0.5
#track kick chip=drums drum=kick group=drums
O4 L4 [C]4
#track bass chip=synth voice=sub-bass group=bass
O3 L1 C C
)";

} // namespace

TEST_CASE("Script reads groups, tracks and directives", "[mml][modern][song]")
{
	const auto parsed = ModernSongScript::parse(R"(
#tempo 140
#swing 0.6 grid=16
#limiter ceiling=-2 release=100
#master gain=-3 width=1.2
#tail 1.25
#loop 2
#dither off
#group lead gain=-4
#duck target=bass by=kick depth=-8 release=200
#track kick chip=drums drum=kick group=drums
O4 C4
#track lead chip=synth voice=supersaw group=lead noswing
O5 C4
#track keys chip=sampler voice=5 group=keys
O4 C4
)");
	REQUIRE(parsed.ok());
	const auto& o = parsed.song.options();
	CHECK(o.tempo == 140);
	CHECK_THAT(static_cast<double>(o.swingRatio), Catch::Matchers::WithinAbs(0.6, 1.0e-6));
	CHECK(o.swingGrid == 16);
	CHECK(o.limiter);
	CHECK_THAT(static_cast<double>(o.limit.ceilingDb), Catch::Matchers::WithinAbs(-2.0, 1.0e-6));
	CHECK_THAT(static_cast<double>(o.masterGainDb), Catch::Matchers::WithinAbs(-3.0, 1.0e-6));
	CHECK_THAT(static_cast<double>(o.stereoWidth), Catch::Matchers::WithinAbs(1.2, 1.0e-6));
	CHECK(o.tailSeconds == 1.25);
	CHECK(o.loopCount == 2);
	CHECK_FALSE(o.dither);
	CHECK(parsed.song.trackCount() == 3);
	REQUIRE(parsed.song.groups().size() == 3);
	CHECK(parsed.song.groups()[0].name == "lead");
	CHECK_THAT(static_cast<double>(parsed.song.groups()[0].gainDb), Catch::Matchers::WithinAbs(-4.0, 1.0e-6));
}

TEST_CASE("Script reports bad lines and keeps directives it does not know", "[mml][modern][song]")
{
	const auto parsed = ModernSongScript::parse(R"(
O4 C4
#track a chip=banjo
#track b chip=synth voice=nothing
#tempo
#chip opl3
#track c chip=drums drum=wrongdrum
)");
	CHECK_FALSE(parsed.ok());
	CHECK(parsed.errors.size() >= 5);
	REQUIRE(parsed.unknownDirectives.size() == 1);
	CHECK(parsed.unknownDirectives[0] == "#chip opl3");
}

TEST_CASE("Rendering rejects a duck rule whose group does not exist", "[mml][modern][song]")
{
	auto parsed = ModernSongScript::parse(std::string(DUCK_SONG) + "#duck target=missing by=kick\n");
	REQUIRE(parsed.ok());
	CHECK_THROWS_AS(parsed.song.render(), std::invalid_argument);
}

TEST_CASE("Stems add up to the mix when the master is neutral", "[mml][modern][song]")
{
	const auto r = renderText(std::string(DUCK_SONG) + "#duck target=bass by=kick depth=-9\n");
	REQUIRE(r.stems.size() == 2);
	StereoBuffer sum;
	sum.sampleRate = 44100;
	for (const auto& s : r.stems) sum.addScaled(s.audio, 1.0f);
	REQUIRE(sum.samples.size() == r.mix.samples.size());
	double worst = 0.0;
	for (std::size_t i = 0; i < sum.samples.size(); ++i)
	{
		worst = std::max(worst, std::fabs(static_cast<double>(sum.samples[i] - r.mix.samples[i])));
	}
	CHECK(worst == 0.0);
}

TEST_CASE("Event ducking lowers the target by the set depth under each kick and lets it recover", "[mml][modern][song][duck]")
{
	const auto plain = renderText(DUCK_SONG);
	const auto ducked = renderText(std::string(DUCK_SONG) + "#duck target=bass by=kick depth=-10 attack=3 release=150\n");
	const auto bassPlain = channelOf(stemOf(plain, "bass"), 0);
	const auto bassDucked = channelOf(stemOf(ducked, "bass"), 0);
	const std::size_t kick = 22050; // 2 つ目のキック
	const double before = peakDbOf(bassDucked, kick - 3000, 2500);
	const double during = peakDbOf(bassDucked, kick + 132, 200);
	const double after150 = peakDbOf(bassDucked, kick + 132 + 6615, 900);
	const double after400 = peakDbOf(bassDucked, kick + 17640, 900);
	INFO("bass peak dB: before " << before << ", under the kick " << during << ", +150ms " << after150 << ", +400ms " << after400);
	CHECK_THAT(during - before, Catch::Matchers::WithinAbs(-10.0, 1.0));
	CHECK_THAT(after150 - before, Catch::Matchers::WithinAbs(-1.0, 0.8));
	CHECK(std::fabs(after400 - before) < 0.1);
	// 下げる前のベースと、ダッキングなしのキックは変わらない
	CHECK(peakDbOf(bassPlain, kick + 250, 900) > before - 0.5);
	CHECK(stemOf(plain, "drums").samples == stemOf(ducked, "drums").samples);
}

TEST_CASE("Signal ducking reacts to the level of another group", "[mml][modern][song][duck]")
{
	const auto plain = renderText(DUCK_SONG);
	const auto ducked = renderText(std::string(DUCK_SONG)
		+ "#duck target=bass by=drums mode=signal threshold=-30 ratio=8 lowpass=200 attack=2 release=150\n");
	const auto before = peakDbOf(channelOf(stemOf(ducked, "bass"), 0), 22050 - 3000, 2500);
	const auto during = peakDbOf(channelOf(stemOf(ducked, "bass"), 0), 22050 + 800, 900);
	INFO("signal duck: before " << before << " dB, under the kick " << during << " dB");
	CHECK(during < before - 12.0);
	CHECK(peakDbOf(channelOf(stemOf(plain, "bass"), 0), 22050 + 800, 900) > before - 0.5);
}

TEST_CASE("The song renders the same PCM every time and in parallel", "[mml][modern][song][determinism]")
{
	const std::string text = std::string(modernExamples()[0].script);
	auto a = ModernSongScript::parse(text);
	auto b = ModernSongScript::parse(text);
	REQUIRE(a.ok());
	const auto ra = a.song.render();
	const auto rb = b.song.render();
	CHECK(hashPcm16(ra.toPcm16(true)) == hashPcm16(rb.toPcm16(true)));

	// 2 回目の描画 (同じオブジェクト) も同じ
	const auto ra2 = a.song.render();
	CHECK(ra.mix.samples == ra2.mix.samples);

	// グループごとに別スレッドで描いても同じ
	b.song.options().parallelGroups = true;
	const auto rp = b.song.render();
	CHECK(ra.mix.samples == rp.mix.samples);
}

TEST_CASE("A group renders the same stem whether or not the other groups exist", "[mml][modern][song][determinism]")
{
	const std::string head = "#tempo 130\n#limiter off\n#tail 0.3\n";
	const std::string a = "#track a chip=synth voice=supersaw group=a\nO3 L2 'CEG' 'FAC'\n";
	const std::string b = "#track b chip=synth voice=riser group=b\nXcutoff=300>9000/1 O4 C1\n";
	const std::string c = "#track c chip=drums drum=snare group=c\nO4 L8 C R C C R C R C\n";
	const auto full = renderText(head + a + b + c);
	const auto withoutB = renderText(head + a + c);
	// 長さが違えば、短い方は無音で伸ばされる。重なる部分は一致し、余りは無音
	const auto sameAudio = [](const std::vector<float>& x, const std::vector<float>& y)
	{
		const std::size_t n = std::min(x.size(), y.size());
		if (!std::equal(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(n), y.begin()))
		{
			return false;
		}
		const auto& longer = (x.size() > y.size()) ? x : y;
		return std::all_of(longer.begin() + static_cast<std::ptrdiff_t>(n), longer.end(), [](float v) { return v == 0.0f; });
	};
	CHECK(sameAudio(stemOf(full, "a").samples, stemOf(withoutB, "a").samples));
	CHECK(sameAudio(stemOf(full, "c").samples, stemOf(withoutB, "c").samples));
}

TEST_CASE("The tail option rings the last notes out", "[mml][modern][song]")
{
	const std::string base = "#tempo 120\n#limiter off\n#track lead chip=synth voice=pad group=lead\nO4 L4 C\n";
	const auto none = renderText(base + "#tail 0\n");
	const auto tail = renderText(base + "#tail 1\n");
	CHECK(tail.mix.frames() == none.mix.frames() + 44100);
	CHECK(rmsDbOf(monoOf(tail.mix), none.mix.frames() + 2000, 4000) > -70.0);
	CHECK(peakOfStereo(none.mix) > 0.01);
}

TEST_CASE("The limiter holds the song master under its ceiling", "[mml][modern][song]")
{
	auto parsed = ModernSongScript::parse(R"(
#tempo 128
#limiter ceiling=-3 release=60
#master gain=12
#track a chip=synth voice=supersaw group=a
O3 L1 'CEG' 'CEG'
#track b chip=synth voice=saw-bass group=b
O2 L1 C C
)");
	REQUIRE(parsed.ok());
	const auto r = parsed.song.render();
	const double ceiling = std::pow(10.0, -3.0 / 20.0);
	INFO("peak " << peakOfStereo(r.mix) << " ceiling " << ceiling << ", reduction " << r.limiterReductionDb << " dB");
	CHECK(peakOfStereo(r.mix) <= ceiling * (1.0 + 1.0e-6));
	CHECK(r.limiterReductionDb < -1.0);
}

TEST_CASE("Swing delays the offbeat hit in the rendered audio", "[mml][modern][song][swing]")
{
	const std::string track = "#track hat chip=drums drum=hat group=drums\nO4 L8 C C C C R1\n";
	const auto straight = renderText("#tempo 120\n#limiter off\n#tail 0.2\n" + track);
	const auto swung = renderText("#tempo 120\n#limiter off\n#tail 0.2\n#swing 0.6667\n" + track);
	const auto a = channelOf(straight.mix, 0);
	const auto b = channelOf(swung.mix, 0);
	// 8 分音符 (0.25 秒 = 11025 サンプル) の 2 つ目: 直線なら 11025、スウィングなら 8 tick 後ろの約 14700 サンプル
	const std::size_t second = firstAbove(a, 0.01, 5000);
	const std::size_t secondSwung = firstAbove(b, 0.01, 5000);
	const std::size_t third = firstAbove(b, 0.01, secondSwung + 4000);
	INFO("second hit: straight " << second << ", swung " << secondSwung << ", third (swung) " << third);
	CHECK(second > 10900);
	CHECK(second < 11300);
	CHECK_THAT(static_cast<double>(secondSwung) - static_cast<double>(second), Catch::Matchers::WithinAbs(3675.0, 100.0));
	CHECK(third > 21900);
	CHECK(third < 22300);
}

TEST_CASE("The arpeggiator plays the chord tones in order at the step rate", "[mml][modern][song][arp]")
{
	const auto r = renderText(R"(
#tempo 120
#limiter off
#tail 0.1
#track arp chip=synth group=a
Xwave=sine Xcutoff=20000 Xres=0 Xatk=1 Xdec=0 Xsus=100 Xrel=5 Xarp=up Xarprate=16 Xarpgate=80 O4 L1 'CEG'
)");
	const auto mono = monoOf(r.mix);
	// 16 分音符 = 5512.5 サンプル。各ステップの真ん中を測る
	const double expected[] = {261.63, 329.63, 392.0, 261.63, 329.63, 392.0};
	for (int step = 0; step < 6; ++step)
	{
		const auto start = static_cast<std::size_t>(5512.5 * step) + 1200;
		const double hz = peakHzOf(spectrumOf(mono, start, 2048, 44100.0), 100.0);
		INFO("step " << step << " measured " << hz << " Hz");
		CHECK(std::fabs(centsBetween(hz, expected[step])) < 30.0);
	}
}

TEST_CASE("OPNA, synth, drums and sampler play on one tick axis with separate stems", "[mml][modern][song][sync]")
{
	const auto r = renderText(R"(
#tempo 120
#limiter off
#tail 0.3
#track ssg chip=opna part=ssg group=retro
O4 R4 L4 A
#track sine chip=synth voice=sub-bass group=modern
O4 R4 L4 A
#track kick chip=sampler drum=kit group=kit
O2 R4 L4 C
)", true);
	REQUIRE(r.stems.size() == 3);
	const auto ssg = channelOf(stemOf(r, "retro"), 0);
	const auto synth = channelOf(stemOf(r, "modern"), 0);
	const auto kit = channelOf(stemOf(r, "kit"), 0);
	const std::size_t a = firstAbove(ssg, 0.003, 0);
	const std::size_t b = firstAbove(synth, 0.01, 0);
	const std::size_t c = firstAbove(kit, 0.01, 0);
	INFO("onsets (beat 2 = 22050): OPNA SSG " << a << ", synth " << b << ", sampler " << c);
	for (const std::size_t onset : {a, b, c})
	{
		CHECK(onset >= 22050 - 50);
		CHECK(onset < 22050 + 1500);
	}
	CHECK(peakOfStereo(stemOf(r, "retro")) > 0.003);
	StereoBuffer sum;
	sum.sampleRate = 44100;
	for (const auto& stem : r.stems) sum.addScaled(stem.audio, 1.0f);
	CHECK(sum.samples == r.mix.samples);
}

TEST_CASE("Stem export writes one WAV for the mix and one for each group", "[mml][modern][song]")
{
	const auto r = renderText(DUCK_SONG);
	const auto dir = std::filesystem::temp_directory_path() / "mitiru_mml_stem_test";
	std::filesystem::remove_all(dir);
	const int written = ModernSong::exportStems(r, dir, "song");
	CHECK(written == 3);
	for (const char* name : {"song_mix.wav", "song_drums.wav", "song_bass.wav"})
	{
		const auto path = dir / name;
		REQUIRE(std::filesystem::exists(path));
		CHECK(std::filesystem::file_size(path) == 44 + r.mix.samples.size() * 2);
	}
	std::filesystem::remove_all(dir);
}

TEST_CASE("The tempo directive is only the starting tempo and the MML T wins", "[mml][modern][song]")
{
	const auto onset = [&](const std::string& text)
	{
		const auto r = renderText(text);
		return firstAbove(channelOf(r.mix, 0), 0.01, 0);
	};
	const std::string head = R"(
#limiter off
#tail 0.1
#tempo 100
)";
	const std::string kick = R"(
#track kick chip=drums drum=kick group=drums
O4 R4 C4
)";
	const std::string tempoTrack = R"(
#track t chip=drums drum=snare group=none
T150 R1
)";
	// 2 拍目の頭: 100 BPM なら 0.6 秒 = 26460、150 BPM なら 0.4 秒 = 17640 サンプル
	const std::size_t slow = onset(head + kick);
	const std::size_t fast = onset(head + tempoTrack + kick);
	INFO("onset with #tempo 100: " << slow << ", with an MML T150 in another track: " << fast);
	CHECK(slow >= 26460 - 5);
	CHECK(slow < 26460 + 200);
	CHECK(fast >= 17640 - 5);
	CHECK(fast < 17640 + 200);
}
