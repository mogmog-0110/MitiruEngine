/// @file TestStreamMulti.cpp
/// @brief ストリーミング (小さな塊ずつ PCM を出す) の土台と、MultiChipStream の検査。主な検査は、塊の切り方によらず一括と同じビットになること

#include <catch2/catch_test_macros.hpp>

#include "DummyChip.hpp"
#include "StreamTestUtil.hpp"

#include <mitiru_mml/MitiruMML.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace mitiru_mml;
using mitiru_mml_test::DummySineDriver;
using namespace mitiru_mml_test;

namespace
{

/// @brief 48 kHz、22.05 kHz、11.025 kHz の 3 チップ (小数の比のデシメーションと、同じフレームの繰り返しの両方を通る)。テンポの変化とループを含む
std::unique_ptr<MultiChipSequencer> dummySong()
{
	auto seq = std::make_unique<MultiChipSequencer>();
	const int a = seq->addChip(std::make_unique<DummySineDriver>(48000), 0.9f);
	const int b = seq->addChip(std::make_unique<DummySineDriver>(22050));
	const int c = seq->addChip(std::make_unique<DummySineDriver>(11025), 0.6f);
	seq->addTrack(a, 0, "T150 O4 L8 V12 C D E F $ G A B >C T90 D E F G [CDE]2", 0);
	seq->addTrack(b, 0, "O3 L4 V13 C E G >C <G E C R", 0);
	seq->addTrack(c, 0, "O5 L16 V10 [CRER]6 T200 G4 R4", 0);
	return seq;
}

/// @brief 実チップ (OPNA は約 998 kHz) を混ぜたテキストの曲
std::unique_ptr<MultiChipSequencer> realChipSong()
{
	auto built = ChipSong::build(
		"#track chip=opna voice=piano\nT140 O4 L8 V12 E G A B >C4 <B A G E4 R4\n"
		"#track chip=opna-ssg\nO3 L4 V9 [EEGE]2\n"
		"#track chip=opm voice=lead pan=left\nO4 L8 V11 R4 C E G >C4 <G E C4\n"
		"#track chip=sn76489\nO4 L8 V10 $ G B >D4 <B G4 R4\n"
		"#track chip=sn76489 ch=noise\nO4 L16 V8 [CRRR]6\n"
		"#track chip=nes ch=triangle\nO3 L4 V12 [CGCG]2\n");
	REQUIRE(built.ok());
	return std::move(built.sequencer);
}

} // namespace

TEST_CASE("TimelineCursor steps the same ticks as playTimeline", "[stream][timeline]")
{
	TrackTarget target;
	target.track = 0;
	target.chip = 0;
	target.caps.valid = true;
	const auto events = MmlEventBuilder::fromMelodic(
		MmlEventBuilder::expandLoops(MmlParser::parse("T100 O4 L8 C D E $ F G T160 A B >C T60 D")), target);
	auto sorted = events;
	sortSeqEvents(sorted);

	for (const int loops : {1, 2, 3})
	{
		TickClock refClock(44100, TickClock::Rounding::CarryRemainder, 120);
		std::vector<std::uint32_t> refSamples;
		std::vector<int> refEvents;
		playTimeline(sorted, loops, refClock,
			[&](const SeqEvent& ev) { refEvents.push_back(static_cast<int>(ev.order) * 100 + ev.a); },
			[&](std::uint32_t n) { refSamples.push_back(n); });

		TickClock clock(44100, TickClock::Rounding::CarryRemainder, 120);
		TimelineCursor cursor(sorted, loops);
		std::vector<std::uint32_t> samples;
		std::vector<int> applied;
		while (!cursor.finished())
		{
			const std::uint32_t n = cursor.step(clock, [&](const SeqEvent& ev) { applied.push_back(static_cast<int>(ev.order) * 100 + ev.a); });
			if (n > 0)
			{
				samples.push_back(n);
			}
		}
		REQUIRE(samples == refSamples);
		REQUIRE(applied == refEvents);
		REQUIRE(clock.position() == refClock.position());
	}
}

TEST_CASE("TickGrid finds the bar lines through tempo changes and loops", "[stream][timeline]")
{
	TrackTarget target;
	target.track = 0;
	target.chip = 0;
	target.caps.valid = true;
	// 120 BPM で 2 小節 (4 秒)、そのあと 60 BPM
	auto events = MmlEventBuilder::fromMelodic(
		MmlEventBuilder::expandLoops(MmlParser::parse("T120 O4 L1 C C T60 C C")), target);
	sortSeqEvents(events);
	const TickGrid grid(events, 1, 44100, 120);

	REQUIRE(grid.nextBoundary(0) == 0);
	REQUIRE(grid.nextBoundary(1) == 88200);
	REQUIRE(grid.nextBoundary(88200) == 88200);
	REQUIRE(grid.nextBoundary(88201) == 176400);
	// 60 BPM の 1 小節は 4 秒
	REQUIRE(grid.nextBoundary(176401) == 176400 + 176400);
	REQUIRE(grid.nextBoundary(100, TickGrid::TICKS_PER_BEAT) == 22050);
	REQUIRE(grid.nextBoundary(999999999) == 999999999);
	// 120 BPM で 4 秒、60 BPM で 8 秒。最後の tick のぶんだけ前後する
	REQUIRE(grid.endFrame() > 529200 - 4410);
	REQUIRE(grid.endFrame() <= 529200 + 4410);
}

TEST_CASE("LayerGainTrack ramps by output frame", "[stream][layer]")
{
	LayerGainTrack track;
	track.schedule(10, 0.0f, 4, FadeCurve::Linear);
	std::vector<float> g;
	for (int i = 0; i < 20; ++i)
	{
		g.push_back(track.next());
	}
	for (int i = 0; i < 10; ++i)
	{
		REQUIRE(g[static_cast<std::size_t>(i)] == 1.0f);
	}
	REQUIRE(g[10] == 0.75f);
	REQUIRE(g[11] == 0.5f);
	REQUIRE(g[12] == 0.25f);
	REQUIRE(g[13] == 0.0f);
	REQUIRE(g[19] == 0.0f);

	// 即座の切り替えと、途中からの続き
	LayerGainTrack jump;
	jump.schedule(2, 0.0f, 0);
	jump.schedule(5, 1.0f, 2, FadeCurve::Linear);
	std::vector<float> j;
	for (int i = 0; i < 9; ++i)
	{
		j.push_back(jump.next());
	}
	REQUIRE(j[1] == 1.0f);
	REQUIRE(j[2] == 0.0f);
	REQUIRE(j[4] == 0.0f);
	REQUIRE(j[5] == 0.5f);
	REQUIRE(j[6] == 1.0f);

	// 0〜1 の外と NaN は受け付けない
	LayerGainTrack strict;
	REQUIRE_THROWS_AS(strict.schedule(0, -0.1f, 0), std::invalid_argument);
	REQUIRE_THROWS_AS(strict.schedule(0, 1.5f, 0), std::invalid_argument);
	REQUIRE_THROWS_AS(strict.schedule(0, std::nanf(""), 0), std::invalid_argument);
	REQUIRE_NOTHROW(strict.schedule(0, 0.0f, 0));
	REQUIRE_NOTHROW(strict.schedule(0, 1.0f, 0));

	// 処理済みのフレームより前を指す命令は、次のフレームから始まる
	LayerGainTrack late;
	for (int i = 0; i < 100; ++i)
	{
		static_cast<void>(late.next());
	}
	late.schedule(3, 0.0f, 0);
	REQUIRE(late.next() == 0.0f);
}

TEST_CASE("Equal-power fades keep the power of a swap constant", "[stream][layer]")
{
	LayerGainTrack out;
	out.schedule(0, 0.0f, 1000, FadeCurve::EqualPower);
	// 入る層は 0 から始める
	LayerGainTrack up;
	up.schedule(0, 0.0f, 0);
	up.schedule(1, 1.0f, 1000, FadeCurve::EqualPower);
	static_cast<void>(up.next());
	for (int i = 0; i < 999; ++i)
	{
		const float a = out.next();
		const float b = up.next();
		// 1 フレームずれた cos と sin なので、二乗の和はほぼ 1
		REQUIRE(std::fabs(a * a + b * b - 1.0f) < 0.01f);
	}
}

TEST_CASE("Streaming limiter, duck curve and sidechain match the offline processing bit for bit", "[stream][dynamics]")
{
	constexpr std::size_t FRAMES = 30000;
	StereoBuffer source;
	source.sampleRate = 44100;
	source.samples.resize(FRAMES * 2);
	ModernRng rng(12345);
	for (std::size_t i = 0; i < FRAMES; ++i)
	{
		// 周期的に大きなバーストを入れて、リミッターとサイドチェーンを動かす
		const float burst = ((i / 3000) % 3 == 1) ? 3.0f : 0.4f;
		source.samples[i * 2] = burst * 0.5f * std::sin(static_cast<float>(i) * 0.05f) + 0.1f * rng.nextSigned();
		source.samples[i * 2 + 1] = burst * 0.5f * std::sin(static_cast<float>(i) * 0.031f) + 0.1f * rng.nextSigned();
	}

	SECTION("limiter, with chunk sizes 1, 7, 1000 and random")
	{
		LimiterSettings settings;
		settings.ceilingDb = -3.0f;
		StereoBuffer offline = source;
		const float reduction = applyLimiter(offline, settings);
		REQUIRE(reduction < -1.0f);

		for (const std::size_t chunk : {std::size_t{1}, std::size_t{7}, std::size_t{1000}, FRAMES})
		{
			StreamingLimiter limiter(settings, 44100);
			std::vector<float> got;
			for (std::size_t at = 0; at < FRAMES; at += chunk)
			{
				const std::size_t n = std::min(chunk, FRAMES - at);
				limiter.push(source.samples.data() + at * 2, n);
				got.insert(got.end(), limiter.peek(), limiter.peek() + limiter.available() * 2);
				limiter.consume(limiter.available());
			}
			limiter.finish();
			got.insert(got.end(), limiter.peek(), limiter.peek() + limiter.available() * 2);
			REQUIRE(sameBits(got, offline.samples));
			REQUIRE(limiter.reductionDb() == reduction);
		}
	}

	SECTION("duck curve")
	{
		DuckSettings settings;
		settings.holdMs = 20.0f;
		const std::vector<std::uint64_t> triggers = {0, 5000, 5100, 12000, 29999};
		const auto curve = buildDuckCurve(FRAMES, triggers, 44100, settings);
		StreamingDuckCurve streaming(triggers, 44100, settings);
		for (std::size_t i = 0; i < FRAMES; ++i)
		{
			REQUIRE(streaming.next() == curve[i]);
		}
	}

	SECTION("sidechain compressor")
	{
		StereoBuffer key = source;
		for (float& s : key.samples)
		{
			s *= 0.5f;
		}
		SidechainSettings settings;
		settings.keyLowPassHz = 160.0f;
		settings.thresholdDb = -20.0f;
		StereoBuffer target = source;
		StereoBuffer expected = source;
		applySidechainCompressor(expected, key, settings);
		StreamingSidechain streaming(44100, settings);
		for (std::size_t i = 0; i < FRAMES; ++i)
		{
			const float gain = streaming.next(key.samples[i * 2], key.samples[i * 2 + 1]);
			target.samples[i * 2] *= gain;
			target.samples[i * 2 + 1] *= gain;
		}
		REQUIRE(sameBits(target.samples, expected.samples));
	}
}

TEST_CASE("MultiChipStream equals render() for any chunk size", "[stream][multichip]")
{
	for (const int loops : {1, 2})
	{
		auto seq = dummySong();
		const std::vector<float> batch = seq->render(loops).samples;
		REQUIRE(batch.size() > 44100);

		const std::vector<std::pair<std::string, ChunkPlan>> plans = {
			{"1 frame", fixedChunk(1)},
			{"3 frames", fixedChunk(3)},
			{"511", fixedChunk(511)},
			{"512", fixedChunk(512)},
			{"4096", fixedChunk(4096)},
			{"whole", fixedChunk(WHOLE_SONG)},
			{"random A", randomChunks(1, 2000)},
			{"random B", randomChunks(99, 40)},
		};
		for (const auto& [name, plan] : plans)
		{
			CAPTURE(loops, name);
			MultiChipStream stream(*seq, loops);
			const std::vector<float> got = drain(stream, plan);
			REQUIRE(sameBits(got, batch));
			REQUIRE(stream.position() == batch.size() / 2);
			REQUIRE(stream.finished());
		}
	}
}

TEST_CASE("MultiChipStream with real chips equals render()", "[stream][multichip]")
{
	auto seq = realChipSong();
	const StereoBuffer batch = seq->render(1);
	REQUIRE(sameBits(seq->render(1).samples, batch.samples));
	REQUIRE(batch.frames() > 44100);
	const PcmBuffer batchPcm = batch.toPcm16Stereo();

	for (const auto& plan : {fixedChunk(512), fixedChunk(97), randomChunks(7, 3000)})
	{
		MultiChipStream stream(*seq, 1);
		REQUIRE(sameBits(drain(stream, plan), batch.samples));
	}

	// 16bit の取り出しは toPcm16Stereo と同じ
	MultiChipStream stream(*seq, 1);
	PcmBuffer pcm;
	std::vector<std::int16_t> chunk(512 * 2);
	for (;;)
	{
		const std::size_t n = stream.readPcm16(chunk.data(), 512);
		pcm.insert(pcm.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(n * 2));
		if (n < 512)
		{
			break;
		}
	}
	REQUIRE(pcm == batchPcm);
}

TEST_CASE("MultiChipStream ends cleanly", "[stream][multichip]")
{
	auto seq = dummySong();
	const std::size_t total = seq->render(1).frames();

	// 曲の長さがちょうど塊の倍数のときは、次の read が 0 を返す
	MultiChipStream stream(*seq, 1);
	std::vector<float> chunk(total * 2);
	REQUIRE(stream.read(chunk.data(), total) == total);
	REQUIRE(stream.finished());
	REQUIRE(stream.read(chunk.data(), 16) == 0);
	REQUIRE(stream.read(chunk.data(), 16) == 0);

	MultiChipSequencer empty;
	empty.addChip(std::make_unique<DummySineDriver>(48000));
	MultiChipStream emptyStream(empty, 1);
	REQUIRE(emptyStream.finished());
	REQUIRE(emptyStream.read(chunk.data(), 16) == 0);

	// 同じシーケンサーから、続けて何度でも同じ音が出る
	MultiChipStream again(*seq, 1);
	const std::vector<float> streamed = drain(again, fixedChunk(777));
	REQUIRE(sameBits(streamed, seq->render(1).samples));
}

TEST_CASE("Layer gains on MultiChipStream match the mix of the separate chips", "[stream][layer][multichip]")
{
	MultiChipSequencer::Options options;
	options.masterGain = 1.0f;
	options.softClip = false;
	const auto build = [&](bool withA, bool withB)
	{
		auto seq = std::make_unique<MultiChipSequencer>(options);
		const int a = seq->addChip(std::make_unique<DummySineDriver>(48000));
		const int b = seq->addChip(std::make_unique<DummySineDriver>(22050));
		if (withA)
		{
			seq->addTrack(a, 0, "T120 O4 L8 V12 [CDEFGABC]4", 0);
		}
		if (withB)
		{
			seq->addTrack(b, 0, "T120 O3 L4 V13 [CEGC]4", 0);
		}
		return seq;
	};
	auto both = build(true, true);
	const std::vector<float> refA = build(true, false)->render(1).samples;
	const std::vector<float> refB = build(false, true)->render(1).samples;
	const std::vector<float> full = both->render(1).samples;
	REQUIRE(refA.size() == full.size());
	REQUIRE(refB.size() == full.size());

	const auto expectedWith = [&](LayerGainTrack track)
	{
		std::vector<float> out(full.size());
		for (std::size_t i = 0; i < full.size() / 2; ++i)
		{
			const float g = track.next();
			float l = refB[i * 2];
			float r = refB[i * 2 + 1];
			if (g != 1.0f)
			{
				l *= g;
				r *= g;
			}
			out[i * 2] = refA[i * 2] + l;
			out[i * 2 + 1] = refA[i * 2 + 1] + r;
		}
		return out;
	};

	SECTION("muted from the start equals the other chip alone")
	{
		MultiChipStream stream(*both, 1);
		stream.scheduleLayerGain(1, 0, 0.0f, 0);
		const std::vector<float> got = drain(stream, fixedChunk(512));
		REQUIRE(sameBits(got, refA));
	}

	SECTION("a fade out then back in, identical for every chunk size")
	{
		LayerGainTrack model;
		model.schedule(20000, 0.0f, 3000, FadeCurve::EqualPower);
		model.schedule(60000, 1.0f, 3000, FadeCurve::Linear);
		const std::vector<float> expected = expectedWith(model);
		REQUIRE_FALSE(sameBits(expected, full));

		for (const auto& plan : {fixedChunk(512), fixedChunk(1), fixedChunk(333), randomChunks(5, 1500), fixedChunk(WHOLE_SONG)})
		{
			MultiChipStream stream(*both, 1);
			stream.scheduleLayerGain(1, 20000, 0.0f, 3000, FadeCurve::EqualPower);
			stream.scheduleLayerGain(1, 60000, 1.0f, 3000, FadeCurve::Linear);
			REQUIRE(sameBits(drain(stream, plan), expected));
		}
	}

	SECTION("commands issued between reads, as a game loop does, give the same PCM")
	{
		// 735 フレーム (60 fps) ごとに読み、ゲームのフレーム 10 で消し、フレーム 160 で戻す。命令は 0.5 秒先のフレーム番号で出す
		MultiChipStream stream(*both, 1);
		std::vector<float> got;
		std::vector<float> chunk(735 * 2);
		std::uint64_t offStart = 0;
		std::uint64_t onStart = 0;
		for (int gameFrame = 0;; ++gameFrame)
		{
			if (gameFrame == 10)
			{
				offStart = stream.scheduleLayerGainAtBar(1, stream.position() + 22050, 0.0f, 2000);
			}
			if (gameFrame == 160)
			{
				onStart = stream.scheduleLayerGainAtBar(1, stream.position() + 22050, 1.0f, 2000, FadeCurve::Linear, TickGrid::TICKS_PER_BEAT);
			}
			const std::size_t n = stream.read(chunk.data(), 735);
			got.insert(got.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(n * 2));
			if (n < 735)
			{
				break;
			}
		}
		REQUIRE(offStart > 0);
		REQUIRE(offStart % 88200 == 0);  // 120 BPM の 4/4 は 2 秒
		REQUIRE(onStart % 22050 == 0);   // 拍の頭
		REQUIRE(onStart > offStart);

		LayerGainTrack model;
		model.schedule(offStart, 0.0f, 2000, FadeCurve::EqualPower);
		model.schedule(onStart, 1.0f, 2000, FadeCurve::Linear);
		REQUIRE(sameBits(got, expectedWith(model)));

		// 同じ命令を最初に全部出して、全体を 1 度に読んでも同じ
		MultiChipStream upFront(*both, 1);
		upFront.scheduleLayerGain(1, offStart, 0.0f, 2000);
		upFront.scheduleLayerGain(1, onStart, 1.0f, 2000, FadeCurve::Linear);
		REQUIRE(sameBits(drain(upFront, fixedChunk(WHOLE_SONG)), got));
	}

	SECTION("bad layer numbers are reported")
	{
		MultiChipStream stream(*both, 1);
		REQUIRE(stream.layerCount() == 2);
		REQUIRE_THROWS_AS(stream.scheduleLayerGain(2, 0, 0.0f, 0), std::out_of_range);
	}
}

TEST_CASE("Every chip renders the same PCM when its sequencer is played a second time", "[stream][multichip]")
{
	// ストリームの頭出しや、同じ曲の 2 回目の再生が、1 回目と同じ PCM になるために、reset() が電源投入直後へ戻すこと
	const std::vector<std::string> texts = {
		"#track chip=opna voice=piano\nT140 O4 L8 V12 E G A B >C4 <B A G E4 R4\n",
		"#track chip=opna-ssg\nT140 O3 L4 V9 [EEGE]2\n",
		"#track chip=opna-rhythm\nT140 L8 [BHSH]2\n",
		"#track chip=opm voice=lead pan=left\nT140 O4 L8 V11 R4 C E G >C4 <G E C4\n",
		"#track chip=opl voice=pluck\nT140 O4 L8 V11 C E G >C4\n",
		"#track chip=opl2 voice=pluck\nT140 O4 L8 V11 C E G >C4\n",
		"#track chip=opl3 voice=pluck\nT140 O4 L8 V11 C E G >C4\n",
		"#track chip=sn76489\nT140 O4 L8 V10 G B >D4 <B G4 R4\n",
		"#track chip=ay8910\nT140 O4 L8 V10 G B >D4 <B G4 R4\n",
		"#track chip=nes ch=triangle\nT140 O3 L4 V12 [CGCG]2\n",
	};
	for (const auto& text : texts)
	{
		INFO(text);
		auto built = ChipSong::build(text);
		REQUIRE(built.ok());
		const std::vector<float> first = built.sequencer->render(1).samples;
		const std::vector<float> second = built.sequencer->render(1).samples;
		REQUIRE(first.size() > 1000);
		REQUIRE(sameBits(first, second));
	}
}
