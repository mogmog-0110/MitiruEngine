/// @file ModernDemo.cpp
/// @brief 現代的な曲の例 3 本を WAV (ミックスとステム) に書き、波形の数字とハッシュを表で出す道具
/// @details 使い方 (Release でビルドする)
///   mml_modern_demo --out DIR            DIR に <曲名>_mix.wav と <曲名>_<グループ名>.wav を書く
///   mml_modern_demo --only neon_drive    1 曲だけ
///   mml_modern_demo --time               描画の時間も出す
///   mml_modern_demo --script FILE        FILE の文書 (# の指示と MML) を描く。SF2 は #sf2 の行で読む

#include "Analysis.hpp"
#include "IntegratedExamples.hpp"
#include "RealSf2Examples.hpp"
#include "SyntheticSf2.hpp"

#include <mitiru_mml/ModernSongScript.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using namespace mitiru_mml;
using namespace mitiru_mml_test;

namespace
{

struct Options
{
	std::string outDir;
	std::string only;
	std::string scriptFile;
	std::string sf2File;   ///< 本物の SF2。指定すると GM のデモ曲を足す
	bool time = false;
};

void printStats(const std::string& name, const ModernSong::Result& r, double renderMs, bool time)
{
	const auto mono = monoOf(r.mix);
	const double seconds = static_cast<double>(r.mix.frames()) / static_cast<double>(r.mix.sampleRate);
	const double peak = peakOfStereo(r.mix);
	const double rms = rmsOf(mono, 0, mono.size());
	const double corr = stereoCorrelation(r.mix, 0, r.mix.frames());
	const double l = rmsOf(channelOf(r.mix, 0), 0, r.mix.frames());
	const double rr = rmsOf(channelOf(r.mix, 1), 0, r.mix.frames());
	const auto spec = spectrumOf(mono, 0, std::min<std::size_t>(262144, mono.size()), static_cast<double>(r.mix.sampleRate));
	std::printf("%s: %.2f s, peak %.2f dBFS, rms %.2f dBFS (crest %.1f dB), L/R rms %.2f/%.2f dB, L/R correlation %.3f, "
		"limiter %.2f dB, centroid %.0f Hz",
		name.c_str(), seconds, toDb(peak), toDb(rms), toDb(peak) - toDb(rms), toDb(l), toDb(rr), corr,
		static_cast<double>(r.limiterReductionDb), centroidHzOf(spec));
	if (time)
	{
		std::printf(", render %.0f ms (%.1fx realtime)", renderMs, seconds * 1000.0 / renderMs);
	}
	std::printf("\n");
	for (const auto& stem : r.stems)
	{
		const auto sm = monoOf(stem.audio);
		std::printf("    stem %-8s peak %7.2f dBFS, rms %7.2f dBFS\n", stem.name.c_str(),
			toDb(peakOfStereo(stem.audio)), toDb(rmsOf(sm, 0, sm.size())));
	}
}

} // namespace

int main(int argc, char** argv)
{
	Options opt;
	for (int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];
		if (arg == "--out" && i + 1 < argc) opt.outDir = argv[++i];
		else if (arg == "--only" && i + 1 < argc) opt.only = argv[++i];
		else if (arg == "--script" && i + 1 < argc) opt.scriptFile = argv[++i];
		else if (arg == "--sf2" && i + 1 < argc) opt.sf2File = argv[++i];
		else if (arg == "--time") opt.time = true;
		else
		{
			std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
			return 2;
		}
	}

	const auto bytes = buildSyntheticSf2();
	auto soundFont = std::make_shared<Sf2File>();
	if (!soundFont->loadFromMemory(bytes.data(), bytes.size()))
	{
		std::fprintf(stderr, "the synthetic SF2 did not load\n");
		return 1;
	}

	struct Job
	{
		std::string name;
		std::string text;
		int sf2;   ///< 0 は SF2 なし、1 は合成の SF2、2 は --sf2 で渡した SF2
	};
	std::vector<Job> jobs;
	if (!opt.scriptFile.empty())
	{
		std::ifstream ifs(opt.scriptFile, std::ios::binary);
		if (!ifs)
		{
			std::fprintf(stderr, "cannot open %s\n", opt.scriptFile.c_str());
			return 1;
		}
		std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
		jobs.push_back({std::filesystem::path(opt.scriptFile).stem().string(), text, opt.sf2File.empty() ? 0 : 2});
	}
	else
	{
		for (const auto& e : allExamples())
		{
			if (opt.only.empty() || opt.only == e.name)
			{
				jobs.push_back({e.name, e.script, e.needsSoundFont ? 1 : 0});
			}
		}
		if (!opt.sf2File.empty() && (opt.only.empty() || opt.only == "gm_showcase"))
		{
			jobs.push_back({"gm_showcase", GM_SHOWCASE, 2});
		}
	}

	std::shared_ptr<Sf2File> realSoundFont;
	if (!opt.sf2File.empty())
	{
		realSoundFont = std::make_shared<Sf2File>();
		if (!realSoundFont->loadFile(opt.sf2File))
		{
			std::fprintf(stderr, "cannot load %s (%s)\n", opt.sf2File.c_str(), realSoundFont->error().c_str());
			return 1;
		}
		std::printf("SF2 %s: %d presets\n", opt.sf2File.c_str(), realSoundFont->presetCount());
	}

	int failures = 0;
	for (const auto& job : jobs)
	{
		auto parsed = ModernSongScript::parse(job.text, job.sf2 == 2 ? std::shared_ptr<const Sf2File>(realSoundFont)
			: (job.sf2 == 1 ? std::shared_ptr<const Sf2File>(soundFont) : nullptr));
		if (!parsed.ok())
		{
			for (const auto& e : parsed.errors) std::fprintf(stderr, "%s: %s\n", job.name.c_str(), e.c_str());
			++failures;
			continue;
		}
		const auto t0 = std::chrono::steady_clock::now();
		const auto result = parsed.song.render();
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		printStats(job.name, result, ms, opt.time);
		const auto pcm = result.toPcm16(result.ditherDefault);
		std::printf("    HASH %s %zu %s\n", job.name.c_str(), pcm.size() / 2, hashPcm16(pcm).c_str());
		if (!opt.outDir.empty())
		{
			const int n = ModernSong::exportStems(result, opt.outDir, job.name, result.ditherDefault);
			std::printf("    wrote %d files to %s\n", n, opt.outDir.c_str());
		}
	}
	return failures == 0 ? 0 : 1;
}
