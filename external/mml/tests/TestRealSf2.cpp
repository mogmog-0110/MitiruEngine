/// @file TestRealSf2.cpp
/// @brief 本物の GM のサウンドフォント (GeneralUser GS) で、SF2 サンプラーの音高、定位、ループ、ドラムの検査
/// @details 環境変数 MITIRU_MML_TEST_SF2 に SF2 のパスを渡したときだけ動く。無ければ全部スキップする (CI で落ちない)。

#include <catch2/catch_test_macros.hpp>

#include "Analysis.hpp"
#include "RealSf2Examples.hpp"

#include <mitiru_mml/ModernSongScript.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace mitiru_mml;
using namespace mitiru_mml_test;

namespace
{

/// @brief 本物の SF2 を 1 度だけ読む。パスが無い、または読めなければ nullptr
std::shared_ptr<const Sf2File> realSoundFont()
{
	static std::shared_ptr<const Sf2File> cached;
	static bool tried = false;
	if (!tried)
	{
		tried = true;
		const std::string path = realSf2PathFromEnvironment();
		if (!path.empty())
		{
			auto file = std::make_shared<Sf2File>();
			if (file->loadFile(path))
			{
				cached = file;
			}
			else
			{
				std::cout << "MITIRU_MML_TEST_SF2 could not be loaded: " << file->error() << std::endl;
			}
		}
	}
	return cached;
}

ModernSong::Result renderOnRealSf2(const std::string& tracks, double tailSeconds = 1.0)
{
	const std::string text = "#tempo 120\n#limiter off\n#tail " + std::to_string(tailSeconds) + "\n" + tracks;
	auto parsed = ModernSongScript::parse(text, realSoundFont());
	REQUIRE(parsed.ok());
	return parsed.song.render();
}

/// @brief 正規化した自己相関で基本周波数を求める (expectedHz の 0.7〜1.4 倍の範囲で最大の山)
double autocorrelationHz(const std::vector<float>& x, std::size_t start, std::size_t count, double rate, double expectedHz)
{
	const std::size_t minLag = static_cast<std::size_t>(rate / (expectedHz * 1.4));
	const std::size_t maxLag = static_cast<std::size_t>(rate / (expectedHz * 0.7));
	std::vector<double> corr(maxLag + 2, 0.0);
	for (std::size_t lag = minLag; lag <= maxLag + 1; ++lag)
	{
		double sum = 0.0;
		double a = 0.0;
		double b = 0.0;
		for (std::size_t i = 0; i + lag < count; ++i)
		{
			const double u = x[start + i];
			const double v = x[start + i + lag];
			sum += u * v;
			a += u * u;
			b += v * v;
		}
		corr[lag] = sum / (std::sqrt(a * b) + 1.0e-12);
	}
	std::size_t best = minLag;
	for (std::size_t lag = minLag; lag <= maxLag; ++lag)
	{
		if (corr[lag] > corr[best])
		{
			best = lag;
		}
	}
	const double y0 = corr[best - 1];
	const double y1 = corr[best];
	const double y2 = corr[best + 1];
	const double denom = y0 - 2.0 * y1 + y2;
	const double shift = (std::fabs(denom) > 1.0e-12) ? 0.5 * (y0 - y2) / denom : 0.0;
	return rate / (static_cast<double>(best) + shift);
}

} // namespace

TEST_CASE("The real SF2 loads and has the GM presets and the standard drum kit", "[mml][realsf2]")
{
	const auto sf = realSoundFont();
	if (!sf)
	{
		SKIP("MITIRU_MML_TEST_SF2 is not set");
	}
	CHECK(sf->valid());
	CHECK(sf->presetCount() > 200);
	for (const int program : {0, 4, 33, 40, 48, 56, 61, 80, 81})
	{
		INFO("program " << program);
		CHECK(sf->findPreset(0, program) >= 0);
	}
	CHECK(sf->findPreset(128, 0) >= 0);
}

TEST_CASE("GM instruments play the written pitch", "[mml][realsf2]")
{
	const auto sf = realSoundFont();
	if (!sf)
	{
		SKIP("MITIRU_MML_TEST_SF2 is not set");
	}
	struct Case
	{
		const char* name;
		int program;
		const char* octave;
		double hz;
	};
	// O3 の A は 220 Hz、O2 は 110 Hz、O4 は 440 Hz
	const Case cases[] = {{"piano", 0, "O4", 440.0}, {"e.piano", 4, "O4", 440.0}, {"finger bass", 33, "O2", 110.0},
		{"violin", 40, "O4", 440.0}, {"strings", 48, "O3", 220.0}, {"trumpet", 56, "O4", 440.0},
		{"brass section", 61, "O3", 220.0}, {"saw lead", 81, "O4", 440.0}};
	for (const auto& c : cases)
	{
		const std::string track = "#track t chip=sampler voice=" + std::to_string(c.program) + " group=g\n" + c.octave + " L2 A\n";
		const auto result = renderOnRealSf2(track);
		const auto mono = monoOf(result.mix);
		const double rate = static_cast<double>(result.mix.sampleRate);
		const double hz = autocorrelationHz(mono, static_cast<std::size_t>(0.25 * rate), 8192, rate, c.hz);
		const double cents = centsBetween(hz, c.hz);
		std::cout << "pitch " << c.name << " expected " << c.hz << " measured " << hz << " cents " << cents << std::endl;
		INFO(c.name << " measured " << hz << " Hz");
		CHECK(std::fabs(cents) < 25.0);
		CHECK(peakOfStereo(result.mix) > 0.01);
	}
}

TEST_CASE("The grand piano zones are panned by their SF2 pan generators", "[mml][realsf2]")
{
	const auto sf = realSoundFont();
	if (!sf)
	{
		SKIP("MITIRU_MML_TEST_SF2 is not set");
	}
	const auto low = renderOnRealSf2("#track t chip=sampler voice=0 group=g\nO1 L2 C\n");
	const auto high = renderOnRealSf2("#track t chip=sampler voice=0 group=g\nO6 L2 C\n");
	const double lowLeft = rmsOf(channelOf(low.mix, 0), 0, low.mix.frames());
	const double lowRight = rmsOf(channelOf(low.mix, 1), 0, low.mix.frames());
	const double highLeft = rmsOf(channelOf(high.mix, 0), 0, high.mix.frames());
	const double highRight = rmsOf(channelOf(high.mix, 1), 0, high.mix.frames());
	std::cout << "piano pan low L/R dB " << toDb(lowLeft) << "/" << toDb(lowRight)
		<< " high L/R dB " << toDb(highLeft) << "/" << toDb(highRight) << std::endl;
	CHECK(lowLeft > lowRight * 1.2);
	CHECK(highRight > highLeft * 1.2);
}

TEST_CASE("A held string note keeps sounding through the loop and stops after the release", "[mml][realsf2]")
{
	const auto sf = realSoundFont();
	if (!sf)
	{
		SKIP("MITIRU_MML_TEST_SF2 is not set");
	}
	const auto result = renderOnRealSf2("#track t chip=sampler voice=48 group=g\nO3 L1 A L1 R\n", 1.5);
	const auto mono = monoOf(result.mix);
	const double rate = static_cast<double>(result.mix.sampleRate);
	const auto at = [&](double seconds, double length)
	{
		return rmsOf(mono, static_cast<std::size_t>(seconds * rate), static_cast<std::size_t>(length * rate));
	};
	// 120 BPM の全音符は 2 秒。1 つ目の音は 2 秒鳴り、その後ろがリリース
	const double early = at(0.6, 0.3);
	const double late = at(1.6, 0.3);
	const double afterRelease = at(3.6, 0.3);
	std::cout << "strings rms dB early " << toDb(early) << " late " << toDb(late) << " after release " << toDb(afterRelease) << std::endl;
	CHECK(late > early * 0.25);
	CHECK(afterRelease < late * 0.05);
	CHECK(peakOfStereo(result.mix) < 1.0);
}

TEST_CASE("The standard drum kit has distinct kick, snare and hi-hat, and the closed hat chokes the open hat", "[mml][realsf2]")
{
	const auto sf = realSoundFont();
	if (!sf)
	{
		SKIP("MITIRU_MML_TEST_SF2 is not set");
	}
	const auto render = [&](const char* mml) { return renderOnRealSf2(std::string("#track t chip=sampler drum=kit group=g\n") + mml + "\n", 1.5); };
	const auto centroid = [](const ModernSong::Result& r)
	{
		const auto mono = monoOf(r.mix);
		return centroidHzOf(spectrumOf(mono, 0, std::min<std::size_t>(32768, mono.size()), static_cast<double>(r.mix.sampleRate)));
	};
	const auto kick = render("O2 L4 C");
	const auto snare = render("O2 L4 D");
	const auto hat = render("O2 L4 F+");
	std::cout << "kit centroid Hz kick " << centroid(kick) << " snare " << centroid(snare) << " hat " << centroid(hat) << std::endl;
	for (const auto* r : {&kick, &snare, &hat})
	{
		CHECK(peakOfStereo(r->mix) > 0.01);
	}
	CHECK(centroid(kick) < centroid(snare));
	CHECK(centroid(snare) < centroid(hat));

	// 開いたハイハット (A#2 = 46) のあとに、閉じたハイハット (F#2 = 42) が重なると、開いた音は止まる
	const auto open = render("O2 L16 A+ R1");
	const auto choked = render("O2 L16 A+ L16 F+ R1");
	const double rate = static_cast<double>(open.mix.sampleRate);
	const std::size_t from = static_cast<std::size_t>(0.6 * rate);
	const double openTail = rmsOf(monoOf(open.mix), from, static_cast<std::size_t>(0.3 * rate));
	const double chokedTail = rmsOf(monoOf(choked.mix), from, static_cast<std::size_t>(0.3 * rate));
	std::cout << "open hat tail rms dB " << toDb(openTail) << " after the closed hat " << toDb(chokedTail) << std::endl;
	CHECK(chokedTail < openTail * 0.5);
}

TEST_CASE("The GM showcase song renders without clipping and with every group audible", "[mml][realsf2]")
{
	const auto sf = realSoundFont();
	if (!sf)
	{
		SKIP("MITIRU_MML_TEST_SF2 is not set");
	}
	auto parsed = ModernSongScript::parse(GM_SHOWCASE, sf);
	REQUIRE(parsed.ok());
	const auto result = parsed.song.render();
	CHECK(peakOfStereo(result.mix) <= 0.9999);
	CHECK(peakOfStereo(result.mix) > 0.1);
	REQUIRE(result.stems.size() == 6);
	for (const auto& stem : result.stems)
	{
		INFO(stem.name);
		CHECK(peakOfStereo(stem.audio) > 0.001);
	}
	const auto second = parsed.song.render();
	CHECK(second.mix.samples == result.mix.samples);
}
