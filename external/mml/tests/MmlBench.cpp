/// @file MmlBench.cpp
/// @brief OpnaSequencer::render の時間と出力 PCM のハッシュを測る道具
/// @details 使い方 (Release でビルドして測る):
///   mml_bench                       全曲の中央値の時間とハッシュを表で出す
///   mml_bench --runs 9              繰り返し回数 (既定 5。最初の 1 回は測らずに捨てる)
///   mml_bench --only five_track_16bar
///   mml_bench --dump DIR            各曲の PCM を DIR/<曲名>.pcm (int16 リトルエンディアン) に書く
///   mml_bench --compare DIR         DIR の PCM を基準にして、差を数字で出す
///   mml_bench --legacy-order --legacy-ssg  既定を従来の挙動に戻して測る (--compare と組み合わせる)
///   mml_bench --analysis            renderWithAnalysis をスレッドあり/なしで測る
///   mml_bench --emit-table          BaselineHashes.hpp に貼る表を出す
///   mml_bench --profile             1 サンプルあたりの時間を、処理の段ごとに分けて測る
///   mml_bench --pitch               FM の A と C (O3-O6) とプリセット音色の高さと明るさを FFT で測る

#include "BaselineSongs.hpp"
#include "PcmCompare.hpp"
#include "PitchMeasure.hpp"
#include "Sha256.hpp"

#include <mitiru_mml/BoxDecimator.hpp>
#include <mitiru_mml/OpnaDriver.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{

using Clock = std::chrono::steady_clock;

struct Options
{
	int runs = 5;
	std::string only;
	std::string dumpDir;
	std::string compareDir;
	bool emitTable = false;
	bool profile = false;
	bool pitch = false;
	bool analysis = false;
	bool legacyOrder = false;
	bool legacySsg = false;
};

std::vector<std::int16_t> readPcmFile(const std::string& path)
{
	std::ifstream ifs(path, std::ios::binary);
	if (!ifs)
	{
		return {};
	}
	ifs.seekg(0, std::ios::end);
	const auto bytes = static_cast<std::size_t>(ifs.tellg());
	ifs.seekg(0, std::ios::beg);
	std::vector<std::int16_t> pcm(bytes / 2);
	ifs.read(reinterpret_cast<char*>(pcm.data()), static_cast<std::streamsize>(pcm.size() * 2));
	return pcm;
}

void writePcmFile(const std::string& path, const std::vector<std::int16_t>& pcm)
{
	std::ofstream ofs(path, std::ios::binary);
	ofs.write(reinterpret_cast<const char*>(pcm.data()), static_cast<std::streamsize>(pcm.size() * 2));
}

double median(std::vector<double> v)
{
	std::sort(v.begin(), v.end());
	return v[v.size() / 2];
}

/// @brief FM 3 声と SSG 3 声を鳴らした状態の OpnaDriver を作る
mitiru_mml::OpnaDriver makeBusyDriver(mitiru_mml::OpnaDevice::Fidelity fidelity)
{
	mitiru_mml::OpnaDriver driver;
	driver.opnaDevice().setFidelity(fidelity);
	driver.setSsgEnable(true, true, true);
	for (int ch = 0; ch < 3; ++ch)
	{
		driver.setFmVoice(ch, mitiru_mml::opna_presets::getPreset(2 + ch));
		driver.fmNoteOn(ch, 60 + ch * 4);
		driver.setSsgTone(ch, 440.0f * static_cast<float>(ch + 1), 10);
	}
	return driver;
}

/// @brief fn を 3 回流して、最小の 1 サンプルあたりの ns を返す
template <class Fn>
double nsPerSample(std::uint64_t samples, Fn&& fn)
{
	double best = 1.0e30;
	for (int r = 0; r < 3; ++r)
	{
		const auto t0 = Clock::now();
		fn();
		const auto t1 = Clock::now();
		best = std::min(best, std::chrono::duration<double, std::nano>(t1 - t0).count() / static_cast<double>(samples));
	}
	return best;
}

/// @brief 処理の段ごとの時間を出す。1 サンプル = ネイティブレートの 1 サンプル
void runProfile()
{
	using mitiru_mml::OpnaDevice;
	constexpr std::uint32_t BLOCK = 4096;
	const std::uint64_t total = 998400ull * 4;
	std::vector<OpnaDevice::RawFrame> raw(BLOCK);
	std::vector<std::int16_t> mono(BLOCK);
	volatile std::int64_t sink = 0;

	std::printf("| stage | fidelity | ns/native-sample | native rate Hz | ms per audio second |\n|---|---|---|---|---|\n");
	const struct { const char* name; OpnaDevice::Fidelity f; } fidelities[] = {
		{"Max", OpnaDevice::Fidelity::Max}, {"Medium", OpnaDevice::Fidelity::Medium}, {"Minimum", OpnaDevice::Fidelity::Minimum}};
	for (const auto& fid : fidelities)
	{
		auto driver = makeBusyDriver(fid.f);
		const std::uint64_t n = (total / BLOCK) * BLOCK;
		const double rate = static_cast<double>(driver.sampleRate());
		auto row = [&](const char* stage, double ns)
		{
			std::printf("| %s | %s | %.2f | %.0f | %.2f |\n", stage, fid.name, ns, rate, ns * rate / 1.0e6);
		};

		row("ymfm generate only", nsPerSample(n, [&] {
			for (std::uint64_t i = 0; i < n; i += BLOCK) { driver.opnaDevice().generateRaw(raw.data(), BLOCK); sink = sink + raw[0].data[0]; }
		}));
		row("generate + mix every sample", nsPerSample(n, [&] {
			for (std::uint64_t i = 0; i < n; i += BLOCK)
			{
				driver.opnaDevice().generateRaw(raw.data(), BLOCK);
				for (std::uint32_t k = 0; k < BLOCK; ++k) mono[k] = OpnaDevice::legacyMonoSample(raw[k]);
				sink = sink + mono[0];
			}
		}));
		row("renderInto (mix cached)", nsPerSample(n, [&] {
			for (std::uint64_t i = 0; i < n; i += BLOCK) { driver.renderInto(mono.data(), BLOCK); sink = sink + mono[0]; }
		}));
		row("renderInto + box decimation", nsPerSample(n, [&] {
			mitiru_mml::BoxDecimatorPcm16 dec(driver.sampleRate(), 44100);
			mitiru_mml::PcmBuffer out;
			for (std::uint64_t i = 0; i < n; i += BLOCK) { driver.renderInto(mono.data(), BLOCK); dec.push(mono.data(), BLOCK, out); }
			sink = sink + static_cast<std::int64_t>(out.size());
		}));
	}
}

/// @brief FM の高さを FFT で測る
void runPitch()
{
	using namespace mitiru_mml_test;
	std::printf("| note | key | expected Hz | measured Hz | error cents |\n|---|---|---|---|---|\n");
	const char* names[] = {"C", "A"};
	for (int octave = 3; octave <= 6; ++octave)
	{
		for (int n = 0; n < 2; ++n)
		{
			const int key = (octave + 1) * 12 + (n == 0 ? 0 : 9);
			const double expected = expectedHz(key);
			const auto m = measureFmNote(pureSineVoice(), key);
			std::printf("| %s%d | %d | %.2f | %.2f | %+.2f |\n", names[n], octave, key, expected, m.peakHz,
				centsOff(m.peakHz, expected));
		}
	}
	static const char* presetNames[] = {"Piano", "Bell", "Brass", "Strings", "Organ", "E.Piano",
		"Bass", "Flute", "Koto", "SynthLead", "Vibes", "DistGuitar"};
	std::printf("\n| preset | key | measured peak Hz | RMS dBFS | centroid Hz |\n|---|---|---|---|---|\n");
	for (int preset = 0; preset < 12; ++preset)
	{
		for (const int key : {57, 69, 81})
		{
			const auto m = measureFmNote(mitiru_mml::opna_presets::getPreset(preset), key);
			std::printf("| %s | %d | %.1f | %.1f | %.0f |\n", presetNames[preset], key, m.peakHz, m.rmsDb, m.centroidHz);
		}
	}
}

/// @brief renderWithAnalysis をスレッドあり/なしで測り、結果が同じかも確かめる
void runAnalysis(mitiru_mml::OpnaSequencer seq, const mitiru_mml_test::BaselineSong& song, int runs)
{
	double msSerial = 0.0;
	double msThreaded = 0.0;
	bool same = true;
	for (int r = 0; r < runs; ++r)
	{
		auto options = seq.renderOptions();
		options.parallelAnalysis = false;
		seq.setRenderOptions(options);
		const auto t0 = Clock::now();
		const auto serial = seq.renderWithAnalysis();
		const auto t1 = Clock::now();
		options.parallelAnalysis = true;
		seq.setRenderOptions(options);
		const auto threaded = seq.renderWithAnalysis();
		const auto t2 = Clock::now();
		msSerial += std::chrono::duration<double, std::milli>(t1 - t0).count();
		msThreaded += std::chrono::duration<double, std::milli>(t2 - t1).count();
		same = same && serial.mixed == threaded.mixed && serial.trackPcm == threaded.trackPcm;
	}
	std::printf("| %s | %zu | %.1f | %.1f | %d |\n", song.name.c_str(), song.tracks.size(),
		msSerial / runs, msThreaded / runs, same ? 1 : 0);
}

} // namespace

int main(int argc, char** argv)
{
	Options opt;
	for (int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];
		if (arg == "--runs" && i + 1 < argc) opt.runs = std::max(1, std::atoi(argv[++i]));
		else if (arg == "--only" && i + 1 < argc) opt.only = argv[++i];
		else if (arg == "--dump" && i + 1 < argc) opt.dumpDir = argv[++i];
		else if (arg == "--compare" && i + 1 < argc) opt.compareDir = argv[++i];
		else if (arg == "--emit-table") opt.emitTable = true;
		else if (arg == "--profile") opt.profile = true;
		else if (arg == "--pitch") opt.pitch = true;
		else if (arg == "--analysis") opt.analysis = true;
		else if (arg == "--legacy-order") opt.legacyOrder = true;
		else if (arg == "--legacy-ssg") opt.legacySsg = true;
		else
		{
			std::fprintf(stderr, "unknown option: %s\n", arg.c_str());
			return 2;
		}
	}

	if (opt.pitch)
	{
		runPitch();
		return 0;
	}
	if (opt.profile)
	{
		runProfile();
		return 0;
	}
	if (opt.analysis)
	{
		std::printf("| song | tracks | serial ms | threaded ms | identical |\n|---|---|---|---|---|\n");
	}
	else if (!opt.emitTable)
	{
		std::printf("| song | samples | median ms | min ms | sha256 |\n|---|---|---|---|---|\n");
	}
	for (const auto& song : mitiru_mml_test::baselineSongs())
	{
		if (!opt.only.empty() && song.name != opt.only)
		{
			continue;
		}
		auto seq = mitiru_mml_test::buildSequencer(song);
		auto options = seq.renderOptions();
		if (opt.legacyOrder) options.stableEventOrder = false;
		if (opt.legacySsg) options.isolateSsgControl = false;
		seq.setRenderOptions(options);

		if (opt.analysis)
		{
			runAnalysis(seq, song, opt.runs);
			continue;
		}

		std::vector<std::int16_t> pcm = seq.render(song.loopCount); // 最初の 1 回は測らない
		std::vector<double> ms;
		for (int r = 0; r < opt.runs; ++r)
		{
			const auto t0 = Clock::now();
			pcm = seq.render(song.loopCount);
			const auto t1 = Clock::now();
			ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
		}
		const std::string hash = mitiru_mml_test::Sha256::ofPcm(pcm);

		if (opt.emitTable)
		{
			std::printf("\t{\"%s\", %zuu, \"%s\"},\n", song.name.c_str(), pcm.size(), hash.c_str());
		}
		else
		{
			std::printf("| %s | %zu | %.1f | %.1f | %s |\n", song.name.c_str(), pcm.size(),
				median(ms), *std::min_element(ms.begin(), ms.end()), hash.c_str());
		}
		if (!opt.dumpDir.empty())
		{
			writePcmFile(opt.dumpDir + "/" + song.name + ".pcm", pcm);
		}
		if (!opt.compareDir.empty())
		{
			const auto ref = readPcmFile(opt.compareDir + "/" + song.name + ".pcm");
			const auto d = mitiru_mml_test::comparePcm(ref, pcm);
			std::printf("  compare %s: identical=%d len %zu/%zu maxDiff=%d rmsDiff=%.3f rmsRef=%.1f "
				"diff/signal=%.1f dB diff=%.1f dBFS worstBand=%.1f dB bands(dB):",
				song.name.c_str(), d.identical ? 1 : 0, d.lengthA, d.lengthB, d.maxAbsDiff,
				d.rmsDiff, d.rmsRef, d.diffToSignalDb, d.diffFullScaleDb, d.worstBandDb);
			for (const double b : d.bandDiffToSignalDb)
			{
				std::printf(" %.0f", b);
			}
			std::printf("\n");
		}
		std::fflush(stdout);
	}
	return 0;
}
