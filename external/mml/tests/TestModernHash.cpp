/// @file TestModernHash.cpp
/// @brief 音源ごとの出力 PCM のハッシュ (同じ MML から同じ PCM であること)

#include <catch2/catch_test_macros.hpp>

#include "Analysis.hpp"
#include "ModernExamples.hpp"
#include "IntegratedExamples.hpp"
#include "ModernHashes.hpp"
#include "SyntheticSf2.hpp"

#include <mitiru_mml/ModernSongScript.hpp>

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

/// @brief 音源 1 つを 1 トラックで鳴らす短い文書
struct SourceCase
{
	std::string name;
	std::string track;
	std::string mml;
};

std::vector<SourceCase> sourceCases()
{
	std::vector<SourceCase> cases;
	for (int i = 0; i < SYNTH_PRESET_COUNT; ++i)
	{
		const std::string preset = synthPreset(i).name;
		cases.push_back({"synth_" + preset, "chip=synth voice=" + preset, "O3 L4 'CEG' R4 C L8 Xcutoff=400>6000/4 E G"});
	}
	for (const char* drum : {"kick", "snare", "hat", "tom", "cymbal", "rim", "clap", "openhat"})
	{
		cases.push_back({std::string("drum_") + drum, std::string("chip=drums drum=") + drum, "O4 L8 C R C C R C C"});
	}
	cases.push_back({"sampler_keys", "chip=sampler voice=0", "O4 L4 'CEG' R4 'FAC' R4"});
	cases.push_back({"sampler_bass", "chip=sampler voice=33", "O2 L8 C C G C A- C G E-"});
	cases.push_back({"sampler_kit", "chip=sampler drum=kit", "O2 L8 C F+ D F+ C C D F+"});
	cases.push_back({"opna_fm", "chip=opna voice=2", "O4 L8 C D E F G A B > C"});
	return cases;
}

} // namespace

TEST_CASE("Every sound source renders the recorded PCM", "[mml][modern][hash]")
{
	const auto bytes = buildSyntheticSf2();
	auto soundFont = std::make_shared<Sf2File>();
	REQUIRE(soundFont->loadFromMemory(bytes.data(), bytes.size()));

	for (const auto& c : sourceCases())
	{
		const std::string text = "#tempo 120\n#limiter off\n#tail 0.5\n#track t " + c.track + " group=g\n" + c.mml + "\n";
		auto parsed = ModernSongScript::parse(text, soundFont);
		INFO("source " << c.name);
		REQUIRE(parsed.ok());
		const auto first = parsed.song.render();
		const auto second = parsed.song.render();
		const std::string hash = hashPcm16(first.toPcm16(false));
		CHECK(hash == hashPcm16(second.toPcm16(false)));
		std::cout << "source " << c.name << " " << first.mix.frames() << " " << hash << std::endl;
		const ModernHashEntry* expected = findSourceHash(c.name);
		if (expected == nullptr)
		{
			FAIL_CHECK("no recorded hash for " << c.name);
			continue;
		}
		CHECK(expected->frames == first.mix.frames());
		CHECK(expected->sha256 == hash);
		CHECK(peakOfStereo(first.mix) > 0.0);
	}
}

TEST_CASE("Example songs render the recorded PCM", "[mml][modern][song][hash]")
{
	for (const auto& example : allExamples())
	{
		auto parsed = ModernSongScript::parse(example.script, example.needsSoundFont ? testSoundFont() : nullptr);
		INFO("example " << example.name);
		REQUIRE(parsed.ok());
		const auto result = parsed.song.render();
		const auto pcm = result.toPcm16(true);
		const std::string hash = hashPcm16(pcm);
		std::cout << "example " << example.name << " " << pcm.size() / 2 << " " << hash << std::endl;
		const ModernHashEntry* expected = findModernHash(example.name);
		if (expected == nullptr)
		{
			FAIL_CHECK("no recorded hash for " << example.name);
			continue;
		}
		CHECK(expected->frames == pcm.size() / 2);
		CHECK(expected->sha256 == hash);
		CHECK(peakOfStereo(result.mix) <= 0.9999);
	}
}
