/// @file TestStreamModern.cpp
/// @brief ModernSongStream の検査。塊の切り方によらず、ModernSong::render() の mix とビットまで同じになること、層の切り替え

#include <catch2/catch_test_macros.hpp>

#include "IntegratedExamples.hpp"
#include "ModernExamples.hpp"
#include "StreamTestUtil.hpp"
#include "SyntheticSf2.hpp"

#include <mitiru_mml/MitiruMML.hpp>

#include <chrono>
#include <cmath>
#include <iostream>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
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

/// @brief テンポの変化、ループ (#loop 2 と $)、スウィング、両方のダッキング、グループのゲイン、
///        効いているリミッター、ステレオ幅、尾、複数のチップ (直接の経路と、箱型フィルターを通る経路の両方) を含む短い曲
std::string coverageSong(const std::string& extraDirectives = "")
{
	return std::string(
		"#tempo 140\n"
		"#loop 2\n"
		"#swing 0.56 grid=8\n"
		"#limiter ceiling=-9 release=60\n"
		"#master gain=7 width=1.3\n"
		"#tail 0.35\n"
		"#duck target=bass by=kick depth=-9 attack=3 release=140\n"
		"#duck target=pad,retro by=drums mode=signal threshold=-30 ratio=6 lowpass=200 attack=2 release=120\n"
		"#group kick gain=2\n"
		"#group bass gain=-1\n"
		"#group pad gain=-4\n")
		+ extraDirectives +
		"#track kick chip=drums drum=kick group=kick\n"
		"O4 L4 [CCCC]2\n"
		"#track hat chip=drums drum=hat group=drums\n"
		"O4 L8 [RC]8\n"
		"#track bass chip=synth voice=saw-bass group=bass\n"
		"O2 L8 [C C >C< C]2 $ T170 [A- A- >A-< A-]2\n"
		"#track pad chip=synth voice=supersaw group=pad\n"
		"O4 'CEG'1 'ACE'1\n"
		"#track lead chip=opm voice=lead pan=left group=retro\n"
		"O5 L8 E G A B >C4 <B A G E4 R4\n"
		"#track beep chip=sn76489 pan=right group=retro\n"
		"O4 L16 [C E G >C< G E]4\n";
}

ModernSong::Result renderOffline(ModernSong& song)
{
	return song.render();
}

PcmBuffer drainPcm(ModernSongStream& stream, std::size_t chunk)
{
	PcmBuffer pcm;
	std::vector<std::int16_t> buffer(chunk * 2);
	for (;;)
	{
		const std::size_t n = stream.readPcm16(buffer.data(), chunk);
		pcm.insert(pcm.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(n * 2));
		if (n < chunk)
		{
			break;
		}
	}
	return pcm;
}

} // namespace

TEST_CASE("ModernSongStream equals render() for the example songs", "[stream][modern]")
{
	for (const auto& example : allExamples())
	{
		CAPTURE(example.name);
		auto parsed = ModernSongScript::parse(example.script, example.needsSoundFont ? testSoundFont() : nullptr);
		REQUIRE(parsed.ok());
		const ModernSong::Result batch = renderOffline(parsed.song);
		REQUIRE(batch.mix.frames() > 44100);

		ModernSongStream stream(parsed.song);
		REQUIRE(sameBits(drain(stream, fixedChunk(512)), batch.mix.samples));
		REQUIRE(stream.finished());
		REQUIRE(stream.limiterReductionDb() == batch.limiterReductionDb);

		ModernSongStream pcmStream(parsed.song);
		REQUIRE(drainPcm(pcmStream, 512) == batch.toPcm16(parsed.song.options().dither));
	}
}

TEST_CASE("ModernSongStream equals render() for any chunk size", "[stream][modern]")
{
	struct Variant
	{
		const char* name;
		std::string extra;
	};
	const std::vector<Variant> variants = {
		{"44.1 kHz", ""},
		{"32 kHz", "#rate 32000\n"},
		{"48 kHz, no limiter, no dither", "#rate 48000\n#limiter off\n#dither off\n"},
	};
	for (const auto& variant : variants)
	{
		CAPTURE(variant.name);
		auto parsed = ModernSongScript::parse(coverageSong(variant.extra));
		REQUIRE(parsed.ok());
		const ModernSong::Result batch = renderOffline(parsed.song);
		REQUIRE(batch.mix.frames() > 100000);
		if (parsed.song.options().limiter)
		{
			REQUIRE(batch.limiterReductionDb < -0.5f);  // リミッターが実際に効いている曲で確かめる
		}

		const std::vector<std::pair<std::string, ChunkPlan>> plans = {
			{"1 frame", fixedChunk(1)},
			{"3 frames", fixedChunk(3)},
			{"511", fixedChunk(511)},
			{"512", fixedChunk(512)},
			{"4096", fixedChunk(4096)},
			{"whole", fixedChunk(WHOLE_SONG)},
			{"random A", randomChunks(11, 3000)},
			{"random B", randomChunks(77, 50)},
		};
		for (const auto& [planName, plan] : plans)
		{
			CAPTURE(planName);
			ModernSongStream stream(parsed.song);
			REQUIRE(sameBits(drain(stream, plan), batch.mix.samples));
			REQUIRE(stream.limiterReductionDb() == batch.limiterReductionDb);
		}

		ModernSongStream pcmStream(parsed.song);
		REQUIRE(drainPcm(pcmStream, 333) == batch.toPcm16(parsed.song.options().dither));
	}
}

namespace
{

/// @brief coverageSong の各トラックに send= を足したもの。ダッキングされるグループ (bass、pad)、直接の経路と箱型フィルターを通る経路の
///        両方のチップ (synth、drums、opm、sn76489)、リバーブとディレイとコーラスの全部を通す
std::string sendCoverageSong(const std::string& extraDirectives = "")
{
	std::string text = coverageSong(extraDirectives);
	const auto addSend = [&text](const std::string& header, const std::string& send)
	{
		const std::size_t at = text.find(header);
		REQUIRE(at != std::string::npos);
		const std::size_t end = text.find('\n', at);
		text.insert(end, " " + send);
	};
	addSend("#track kick ", "send=reverb:10");
	addSend("#track bass ", "send=reverb:35,delay:12");
	addSend("#track pad ", "send=reverb:60,chorus:50");
	addSend("#track lead ", "send=delay:30,chorus:20");
	addSend("#track beep ", "send=reverb:15");
	return text;
}

} // namespace

TEST_CASE("ModernSongStream equals render() for a song with sends", "[stream][send][modern]")
{
	auto parsed = ModernSongScript::parse(sendCoverageSong());
	REQUIRE(parsed.ok());
	const ModernSong::Result batch = renderOffline(parsed.song);
	REQUIRE(batch.mix.frames() > 100000);
	REQUIRE(batch.stems.size() == 8);   // 5 つのグループと、reverb、delay、chorus の戻り
	REQUIRE(batch.limiterReductionDb < -0.5f);

	// 送りのない同じ曲とは別の音になっている
	auto dry = ModernSongScript::parse(coverageSong());
	REQUIRE(dry.ok());
	REQUIRE_FALSE(sameBits(renderOffline(dry.song).mix.samples, batch.mix.samples));

	const std::vector<std::pair<std::string, ChunkPlan>> plans = {
		{"1 frame", fixedChunk(1)},
		{"3 frames", fixedChunk(3)},
		{"511", fixedChunk(511)},
		{"512", fixedChunk(512)},
		{"4096", fixedChunk(4096)},
		{"whole", fixedChunk(WHOLE_SONG)},
		{"random A", randomChunks(11, 3000)},
		{"random B", randomChunks(77, 50)},
	};
	for (const auto& [planName, plan] : plans)
	{
		CAPTURE(planName);
		ModernSongStream stream(parsed.song);
		REQUIRE(sameBits(drain(stream, plan), batch.mix.samples));
		REQUIRE(stream.finished());
		REQUIRE(stream.limiterReductionDb() == batch.limiterReductionDb);
	}

	ModernSongStream pcmStream(parsed.song);
	REQUIRE(drainPcm(pcmStream, 333) == batch.toPcm16(parsed.song.options().dither));

	// 別のサンプルレートでも、リミッターとディザを切っても同じ
	auto other = ModernSongScript::parse(sendCoverageSong("#rate 48000\n#limiter off\n#dither off\n"));
	REQUIRE(other.ok());
	const ModernSong::Result otherBatch = renderOffline(other.song);
	ModernSongStream otherStream(other.song);
	REQUIRE(sameBits(drain(otherStream, randomChunks(5, 1500)), otherBatch.mix.samples));
}

TEST_CASE("A muted layer stops its sends on ModernSongStream", "[stream][send][layer][modern]")
{
	const std::string head = "#tempo 120\n#limiter off\n#master gain=0 width=1\n#dither off\n#tail 1.5\n";
	auto both = ModernSongScript::parse(head
		+ "#track a chip=synth voice=pad group=a send=reverb:80,delay:40\n" + "O4 L4 C D E F\n"
		+ "#track b chip=synth voice=saw-bass group=b\n" + "O3 L4 G A B >C<\n");
	auto onlyB = ModernSongScript::parse(head + "#track b chip=synth voice=saw-bass group=b\n" + "O3 L4 G A B >C<\n");
	REQUIRE(both.ok());
	REQUIRE(onlyB.ok());

	ModernSongStream stream(both.song);
	stream.scheduleLayerGain(stream.findLayer("a"), 0, 0.0f, 0);
	const std::vector<float> muted = drain(stream, fixedChunk(777));
	const std::vector<float> reference = onlyB.song.render().mix.samples;
	REQUIRE(muted.size() == reference.size());
	for (std::size_t i = 0; i < muted.size(); ++i)
	{
		REQUIRE(muted[i] == reference[i]);   // 0 と -0 の違いは見ない
	}
}

TEST_CASE("ModernSongStream does not depend on the song it was made from", "[stream][modern]")
{
	auto parsed = ModernSongScript::parse(coverageSong());
	REQUIRE(parsed.ok());
	const ModernSong::Result batch = parsed.song.render();
	auto stream = std::make_unique<ModernSongStream>(parsed.song);
	parsed.song.options().masterGainDb = -30.0f;
	ModernSong::TrackSpec extra;
	extra.name = "extra";
	extra.group = "kick";
	extra.chip = ModernSong::ChipKind::Drums;
	extra.drum = 0;
	extra.mml = "O4 [C4]8";
	parsed.song.addTrack(extra);
	REQUIRE(sameBits(drain(*stream, fixedChunk(512)), batch.mix.samples));
}

TEST_CASE("ModernSongStream reports the same errors as render()", "[stream][modern]")
{
	ModernSong song;
	ModernSong::TrackSpec track;
	track.name = "a";
	track.voice = 0;
	track.mml = "C";
	song.addTrack(track);
	ModernSong::DuckRule rule;
	rule.targets = {"missing"};
	rule.by = "a";
	song.addDuck(rule);
	REQUIRE_THROWS_AS(song.render(), std::invalid_argument);
	REQUIRE_THROWS_AS(ModernSongStream(song), std::invalid_argument);
}

TEST_CASE("Layer gains on ModernSongStream follow the stems", "[stream][layer][modern]")
{
	// 層のゲインは、ダッキングのあと、グループを足す手前で掛かる。ステム (ダッキングまで済んだ音) にゲインを掛けて足したものが、
	// そのまま期待値になる。マスターのゲインは 0 dB、幅は 1 にして、リミッターは一括版の applyLimiter を通す
	const std::string base = coverageSong("#master gain=0 width=1\n#limiter ceiling=-9 release=60\n");
	auto parsed = ModernSongScript::parse(base);
	REQUIRE(parsed.ok());
	const ModernSong::Result batch = parsed.song.render();
	REQUIRE(batch.stems.size() == 5);

	const auto expectedMix = [&](std::vector<LayerGainTrack> tracks, bool limiter)
	{
		StereoBuffer mix;
		mix.sampleRate = batch.mix.sampleRate;
		mix.samples.assign(batch.mix.samples.size(), 0.0f);
		for (std::size_t g = 0; g < batch.stems.size(); ++g)
		{
			for (std::size_t i = 0; i < batch.stems[g].audio.frames(); ++i)
			{
				const float lg = tracks[g].next();
				float l = batch.stems[g].audio.samples[i * 2];
				float r = batch.stems[g].audio.samples[i * 2 + 1];
				if (lg != 1.0f)
				{
					l *= lg;
					r *= lg;
				}
				mix.samples[i * 2] += l;
				mix.samples[i * 2 + 1] += r;
			}
		}
		for (float& s : mix.samples)
		{
			s *= 1.0f;
		}
		if (limiter)
		{
			applyLimiter(mix, parsed.song.options().limit);
		}
		return mix.samples;
	};

	ModernSongStream probe(parsed.song);
	const std::size_t pad = probe.findLayer("pad");
	const std::size_t drums = probe.findLayer("drums");
	REQUIRE(pad < probe.layerCount());
	REQUIRE(drums < probe.layerCount());
	REQUIRE(probe.findLayer("nonexistent") == probe.layerCount());

	SECTION("no commands equals the plain mix")
	{
		ModernSongStream stream(parsed.song);
		REQUIRE(sameBits(drain(stream, fixedChunk(700)), batch.mix.samples));
	}

	SECTION("muting a group from the start equals the sum of the other stems")
	{
		std::vector<LayerGainTrack> model(batch.stems.size());
		model[pad].schedule(0, 0.0f, 0);
		const std::vector<float> expected = expectedMix(model, true);
		REQUIRE_FALSE(sameBits(expected, batch.mix.samples));
		ModernSongStream stream(parsed.song);
		stream.scheduleLayerGain(pad, 0, 0.0f, 0);
		REQUIRE(sameBits(drain(stream, fixedChunk(512)), expected));
	}

	SECTION("fades are identical for every chunk size")
	{
		std::vector<LayerGainTrack> model(batch.stems.size());
		model[pad].schedule(30000, 0.0f, 4000, FadeCurve::EqualPower);
		model[pad].schedule(120000, 1.0f, 4000, FadeCurve::Linear);
		model[drums].schedule(60000, 0.0f, 100, FadeCurve::Linear);
		const std::vector<float> expected = expectedMix(model, true);

		for (const auto& plan : {fixedChunk(512), fixedChunk(1), fixedChunk(1023), fixedChunk(1025), randomChunks(3, 4000), fixedChunk(WHOLE_SONG)})
		{
			ModernSongStream stream(parsed.song);
			stream.scheduleLayerGain(pad, 30000, 0.0f, 4000, FadeCurve::EqualPower);
			stream.scheduleLayerGain(pad, 120000, 1.0f, 4000, FadeCurve::Linear);
			stream.scheduleLayerGain(drums, 60000, 0.0f, 100, FadeCurve::Linear);
			REQUIRE(sameBits(drain(stream, plan), expected));
		}
	}

	SECTION("commands issued between reads at bar lines give the same PCM as issuing them up front")
	{
		ModernSongStream stream(parsed.song);
		std::vector<float> got;
		std::vector<float> chunk(735 * 2);
		std::vector<std::uint64_t> starts;
		for (int gameFrame = 0;; ++gameFrame)
		{
			if (gameFrame == 30)
			{
				starts.push_back(stream.scheduleLayerGainAtBar(pad, stream.position() + 22050, 0.0f, 3000));
			}
			if (gameFrame == 90)
			{
				starts.push_back(stream.scheduleLayerGainAtBar(pad, stream.position() + 22050, 1.0f, 3000));
			}
			const std::size_t n = stream.read(chunk.data(), 735);
			got.insert(got.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(n * 2));
			if (n < 735)
			{
				break;
			}
		}
		REQUIRE(starts.size() == 2);
		REQUIRE(starts[0] > 0);
		REQUIRE(starts[1] > starts[0]);
		// 曲の途中のテンポの変化を通っても、小節の頭に乗っている
		for (const std::uint64_t start : starts)
		{
			REQUIRE(stream.nextBarFrame(start) == start);
		}

		std::vector<LayerGainTrack> model(batch.stems.size());
		model[pad].schedule(starts[0], 0.0f, 3000, FadeCurve::EqualPower);
		model[pad].schedule(starts[1], 1.0f, 3000, FadeCurve::EqualPower);
		REQUIRE(sameBits(got, expectedMix(model, true)));
	}

	SECTION("a command that points before the processed frames starts at the next frame")
	{
		ModernSongStream stream(parsed.song);
		std::vector<float> got(2048 * 2);
		REQUIRE(stream.read(got.data(), 2048) == 2048);
		const std::uint64_t processed = stream.processedFrames();
		REQUIRE(processed >= 2048);
		stream.scheduleLayerGain(pad, 0, 0.0f, 0);
		const std::vector<float> rest = drain(stream, fixedChunk(512));
		got.insert(got.end(), rest.begin(), rest.end());

		std::vector<LayerGainTrack> model(batch.stems.size());
		model[pad].schedule(processed, 0.0f, 0);
		REQUIRE(sameBits(got, expectedMix(model, true)));
	}

	SECTION("a bad layer number is reported")
	{
		ModernSongStream stream(parsed.song);
		REQUIRE_THROWS_AS(stream.scheduleLayerGain(stream.layerCount(), 0, 0.0f, 0), std::out_of_range);
	}
}

TEST_CASE("Streaming cost per 512 frames (Release only; prints the numbers)", "[.stream-bench]")
{
	const auto measure = [](const char* name, auto& stream)
	{
		using Clock = std::chrono::steady_clock;
		std::vector<float> chunk(512 * 2);
		double total = 0.0;
		double worst = 0.0;
		std::size_t reads = 0;
		for (;;)
		{
			const auto t0 = Clock::now();
			const std::size_t n = stream.read(chunk.data(), 512);
			const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
			total += ms;
			worst = std::max(worst, ms);
			++reads;
			if (n < 512)
			{
				break;
			}
		}
		const double budget = 512.0 / 44100.0 * 1000.0;
		std::cout << name << ": " << reads << " reads, mean " << total / static_cast<double>(reads) << " ms, worst " << worst
			<< " ms per 512 frames (budget " << budget << " ms), " << budget / (total / static_cast<double>(reads)) << "x real time\n";
	};
	{
		auto parsed = ModernSongScript::parse(coverageSong());
		REQUIRE(parsed.ok());
		ModernSongStream stream(parsed.song);
		measure("ModernSongStream (drums, synth, OPM, SN76489)", stream);
	}
	{
		auto built = ChipSong::build(
			"#track chip=opna voice=piano\nT140 O4 L8 V12 E G A B >C4 <B A G E4 R4\n"
			"#track chip=opna voice=strings\nO3 L1 V9 C G\n"
			"#track chip=opna-ssg\nO3 L4 V9 [EEGE]2\n"
			"#track chip=opna-rhythm\nL8 [BHSH]4\n");
		REQUIRE(built.ok());
		MultiChipStream stream(*built.sequencer, 1);
		measure("MultiChipStream (OPNA FM x2, SSG, rhythm)", stream);
	}
}

TEST_CASE("MultiChipStream with synth, drums and sampler equals render() and restarts identically", "[stream][multichip][modern]")
{
	MultiChipSequencer seq;
	const int synth = seq.addChip(std::make_unique<SynthDriver>(44100));
	const int drums = seq.addChip(std::make_unique<DrumSynthDriver>(44100));
	auto sampler = std::make_unique<SamplerDriver>(44100);
	sampler->setSoundFont(testSoundFont());
	const int keys = seq.addChip(std::move(sampler));
	const int opna = seq.addChip(std::make_unique<OpnaDriver>());
	seq.addTrack(synth, 0, "T132 O3 L8 Xcutoff=900>6000/2 'CEG'4 C E G >C< $ G E C R", 2);
	seq.addTrack(synth, 1, "O2 L8 Xarp=up Xarprate=16 'CEG'2 C C >C< C", 0);
	seq.addRhythmTrack(drums, 0, "B8 S8 H8 H8 B8 S8 H8 H8 B8 S8 H8 H8");
	seq.addTrack(keys, 0, "O4 L4 C E G C", 0);
	seq.addTrack(opna, 0, "O4 L8 V11 E G A B >C4", 0);

	const StereoBuffer first = seq.render(2);
	REQUIRE(first.frames() > 44100);
	// 2 回目の描画と、そのあとのストリームが、1 回目と同じビットになる (reset() が音ごとの通し番号まで戻す)
	REQUIRE(sameBits(seq.render(2).samples, first.samples));
	for (const auto& plan : {fixedChunk(512), randomChunks(21, 2500), fixedChunk(1)})
	{
		MultiChipStream stream(seq, 2);
		REQUIRE(sameBits(drain(stream, plan), first.samples));
	}
}
