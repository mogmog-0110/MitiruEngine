/// @file TestFoundation.cpp
/// @brief チップの口 (Device と Driver)、整数の時間軸、イベントの並び、複数チップの検査

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "DummyChip.hpp"
#include "Sha256.hpp"

#include <mitiru_mml/BoxDecimator.hpp>
#include <mitiru_mml/MmlEventBuilder.hpp>
#include <mitiru_mml/MultiChipSequencer.hpp>
#include <mitiru_mml/Timeline.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace mitiru_mml;
using mitiru_mml_test::DummySineDriver;

namespace
{

/// @brief 従来の OpnaSequencer::resample と同じ式 (浮動小数点で境界を求め、long で平均する)
PcmBuffer legacyBoxResample(const PcmBuffer& in, std::uint32_t inRate, std::uint32_t outRate)
{
	const double ratio = static_cast<double>(inRate) / static_cast<double>(outRate);
	const auto outLen = static_cast<std::size_t>(static_cast<double>(in.size()) / ratio);
	PcmBuffer out;
	for (std::size_t i = 0; i < outLen; ++i)
	{
		std::size_t s = static_cast<std::size_t>(static_cast<double>(i) * ratio);
		std::size_t e = static_cast<std::size_t>(static_cast<double>(i + 1) * ratio);
		if (e <= s) e = s + 1;
		if (e > in.size()) e = in.size();
		long acc = 0;
		std::size_t n = 0;
		for (std::size_t k = s; k < e; ++k) { acc += in[k]; ++n; }
		out.push_back(static_cast<std::int16_t>(n ? acc / static_cast<long>(n) : 0));
	}
	return out;
}

/// @brief ゴーツェル法で、モノラルに畳んだ信号の指定周波数の振幅を測る
double toneLevel(const StereoBuffer& buf, double hz)
{
	const std::size_t n = buf.frames();
	double re = 0.0;
	double im = 0.0;
	for (std::size_t i = 0; i < n; ++i)
	{
		const double mono = 0.5 * (buf.samples[i * 2] + buf.samples[i * 2 + 1]);
		const double angle = 2.0 * 3.14159265358979323846 * hz * static_cast<double>(i)
			/ static_cast<double>(buf.sampleRate);
		re += mono * std::cos(angle);
		im += mono * std::sin(angle);
	}
	return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(n);
}

/// @brief 最初に |値| が threshold を超えるフレーム番号 (無ければ -1)
long firstOnset(const StereoBuffer& buf, float threshold)
{
	for (std::size_t i = 0; i < buf.frames(); ++i)
	{
		if (std::abs(buf.samples[i * 2]) > threshold) return static_cast<long>(i);
	}
	return -1;
}

} // namespace

// ============================================================================
// イベントの並び
// ============================================================================

TEST_CASE("Timeline events are totally ordered by tick track and order", "[mml][foundation][order]")
{
	std::vector<SeqEvent> events;
	// 同じ tick に 3 トラックぶんのイベントがあり、入力の並びは逆順
	for (int track = 2; track >= 0; --track)
	{
		for (int order = 3; order >= 0; --order)
		{
			SeqEvent ev;
			ev.tick = 10;
			ev.track = static_cast<std::uint32_t>(track);
			ev.order = static_cast<std::uint32_t>(order);
			events.push_back(ev);
		}
	}
	SeqEvent early;
	early.tick = 3;
	early.track = 9;
	events.push_back(early);

	sortSeqEvents(events);

	REQUIRE(events.front().tick == 3);
	for (std::size_t i = 2; i < events.size(); ++i)
	{
		REQUIRE(seqEventBefore(events[i - 1], events[i]));
	}
	REQUIRE(events[1].track == 0);
	REQUIRE(events[1].order == 0);
	REQUIRE(events.back().track == 2);
	REQUIRE(events.back().order == 3);
}

// ============================================================================
// 整数の時間軸
// ============================================================================

TEST_CASE("TickClock truncating mode gives floor of rate*60/(bpm*48) for every legal tempo", "[mml][foundation][clock]")
{
	for (int bpm = 20; bpm <= 300; ++bpm)
	{
		TickClock clock(998400, TickClock::Rounding::TruncatePerTick, bpm);
		const std::uint64_t expected = (998400ull * 60ull) / (static_cast<std::uint64_t>(bpm) * 48ull);
		REQUIRE(clock.advance() == expected);
	}
}

TEST_CASE("TickClock carry mode keeps total length exact", "[mml][foundation][clock]")
{
	// 140 BPM、48 kHz: 1 tick = 428.571... サンプル。切り捨てだけだと 1000 tick で 571 サンプルずれる
	TickClock carry(48000, TickClock::Rounding::CarryRemainder, 140);
	TickClock trunc(48000, TickClock::Rounding::TruncatePerTick, 140);
	std::uint64_t carried = 0;
	std::uint64_t truncated = 0;
	for (int i = 0; i < 1000; ++i)
	{
		carried += carry.advance();
		truncated += trunc.advance();
	}
	REQUIRE(carried == (1000ull * 48000ull * 60ull) / (140ull * 48ull));
	REQUIRE(carry.position() == carried);
	REQUIRE(truncated == 428ull * 1000ull);
}

TEST_CASE("TickClock carry mode survives tempo changes without drifting", "[mml][foundation][clock]")
{
	TickClock clock(49716, TickClock::Rounding::CarryRemainder, 120);
	std::uint64_t total = 0;
	for (int i = 0; i < 480; ++i) total += clock.advance();
	clock.setTempo(173);
	for (int i = 0; i < 480; ++i) total += clock.advance();
	clock.setTempo(120);
	for (int i = 0; i < 480; ++i) total += clock.advance();

	const double exact = 49716.0 * 60.0 * 480.0 * (1.0 / (120.0 * 48.0) + 1.0 / (173.0 * 48.0) + 1.0 / (120.0 * 48.0));
	REQUIRE(std::abs(static_cast<double>(total) - exact) < 2.0);
}

// ============================================================================
// 箱型デシメーター
// ============================================================================

TEST_CASE("BoxDecimatorPcm16 matches the legacy resample for any chunking", "[mml][foundation][decimator]")
{
	PcmBuffer input;
	std::uint32_t lcg = 12345;
	for (int i = 0; i < 200000; ++i)
	{
		lcg = lcg * 1664525u + 1013904223u;
		input.push_back(static_cast<std::int16_t>(static_cast<std::int32_t>(lcg >> 16) - 32768));
	}

	const PcmBuffer expected = legacyBoxResample(input, 998400, 44100);

	for (const std::size_t chunk : {std::size_t{1}, std::size_t{7}, std::size_t{4096}, std::size_t{200000}})
	{
		BoxDecimatorPcm16 decimator(998400, 44100);
		PcmBuffer out;
		for (std::size_t pos = 0; pos < input.size(); pos += chunk)
		{
			const std::size_t n = std::min(chunk, input.size() - pos);
			decimator.push(input.data() + pos, n, out);
		}
		decimator.finish(out);
		REQUIRE(out == expected);
	}
}

TEST_CASE("BoxDecimatorStereo averages a constant signal to the same constant", "[mml][foundation][decimator]")
{
	std::vector<float> input(20000 * 2, 0.25f);
	BoxDecimatorStereo decimator(55930, 44100);
	std::vector<float> out;
	decimator.push(input.data(), 20000, out);
	decimator.finish(out);
	REQUIRE(out.size() / 2 == 20000ull * 44100ull / 55930ull);
	for (const float v : out)
	{
		REQUIRE_THAT(v, Catch::Matchers::WithinAbs(0.25, 1.0e-6));
	}
}

// ============================================================================
// MML からイベントへ (チップに依存しない)
// ============================================================================

TEST_CASE("MmlEventBuilder uses channel caps to choose legato or retrigger", "[mml][foundation][events]")
{
	const auto cmds = MmlEventBuilder::expandLoops(MmlParser::parse("T120 O4 L4 C D&E"));

	TrackTarget slide;
	slide.channel = 2;
	slide.caps.valid = true;
	slide.caps.pitchSlide = true;
	const auto slid = MmlEventBuilder::fromMelodic(cmds, slide);

	TrackTarget plain = slide;
	plain.caps.pitchSlide = false;
	const auto retriggered = MmlEventBuilder::fromMelodic(cmds, plain);

	auto count = [](const std::vector<SeqEvent>& events, SeqEventType type)
	{
		return std::count_if(events.begin(), events.end(),
			[type](const SeqEvent& e) { return e.type == type; });
	};
	// タイの付いた D は、前の音があるチャンネルでは音程だけを変える
	REQUIRE(count(slid, SeqEventType::Pitch) == 1);
	REQUIRE(count(slid, SeqEventType::NoteOn) == 2);
	REQUIRE(count(retriggered, SeqEventType::Pitch) == 0);
	REQUIRE(count(retriggered, SeqEventType::NoteOn) == 3);
	for (const auto& e : slid)
	{
		REQUIRE(e.channel == 2 * (e.type == SeqEventType::Tempo ? 0 : 1));
	}
}

TEST_CASE("MmlEventBuilder emits envelope parameter only for channels that have one", "[mml][foundation][events]")
{
	const auto cmds = MmlEventBuilder::expandLoops(MmlParser::parse("O5 L8 SE9,500 CD"));
	TrackTarget with;
	with.caps.valid = true;
	with.caps.hardwareEnvelope = true;
	TrackTarget without;
	without.caps.valid = true;

	const auto a = MmlEventBuilder::fromMelodic(cmds, with);
	const auto b = MmlEventBuilder::fromMelodic(cmds, without);
	const auto isParam = [](const SeqEvent& e) { return e.type == SeqEventType::Param; };
	REQUIRE(std::count_if(a.begin(), a.end(), isParam) == 2);
	REQUIRE(std::count_if(b.begin(), b.end(), isParam) == 0);
	REQUIRE(a.front().type == SeqEventType::Param);
	REQUIRE(a.front().a == chip_param::ENVELOPE);
	REQUIRE(a.front().b == 9);
	REQUIRE(a.front().c == 500);
}

TEST_CASE("MmlEventBuilder rhythm maps each drum to its own channel", "[mml][foundation][events]")
{
	TrackTarget target;
	const auto events = MmlEventBuilder::fromRhythm("L8 BHSF", target, 9);
	std::vector<int> onChannels;
	for (const auto& e : events)
	{
		if (e.type == SeqEventType::NoteOn) onChannels.push_back(e.channel);
	}
	REQUIRE(onChannels == std::vector<int>{9, 11, 10, 12});
}

// ============================================================================
// 複数チップを同じ時間軸で鳴らす (ダミーのチップ)
// ============================================================================

TEST_CASE("MultiChipSequencer plays two chips with different native rates on one timeline", "[mml][foundation][multichip]")
{
	// 48000 Hz と 96000 Hz のチップ。出力は 44100 Hz
	MultiChipSequencer seq;
	const int a = seq.addChip(std::make_unique<DummySineDriver>(48000));
	const int b = seq.addChip(std::make_unique<DummySineDriver>(96000));
	seq.addTrack(a, 0, "T120 O4 L1 V15 A");    // A4 = 440 Hz
	seq.addTrack(b, 0, "T120 O5 L1 V15 E");    // E5 = 659.3 Hz

	const StereoBuffer mix = seq.render();
	REQUIRE(mix.sampleRate == 44100);
	REQUIRE(mix.frames() > 80000);

	const double at440 = toneLevel(mix, 440.0);
	const double at659 = toneLevel(mix, 659.26);
	const double at500 = toneLevel(mix, 500.0);
	REQUIRE(at440 > 0.05);
	REQUIRE(at659 > 0.05);
	REQUIRE(at500 < at440 * 0.1);
}

TEST_CASE("MultiChipSequencer notes at the same tick start within one output sample on every chip", "[mml][foundation][multichip]")
{
	auto render = [](std::uint32_t rateA, std::uint32_t rateB, bool both, int which)
	{
		MultiChipSequencer seq;
		const int a = seq.addChip(std::make_unique<DummySineDriver>(rateA));
		const int b = seq.addChip(std::make_unique<DummySineDriver>(rateB));
		if (both || which == 0) seq.addTrack(a, 0, "T137 O4 L4 R V15 A");
		if (both || which == 1) seq.addTrack(b, 0, "T137 O4 L4 R V15 A");
		return seq.render();
	};

	const StereoBuffer onlyA = render(49716, 55930, false, 0);
	const StereoBuffer onlyB = render(49716, 55930, false, 1);
	const long onsetA = firstOnset(onlyA, 0.01f);
	const long onsetB = firstOnset(onlyB, 0.01f);
	REQUIRE(onsetA > 0);
	REQUIRE(onsetB > 0);
	REQUIRE(std::abs(onsetA - onsetB) <= 1);
	// 1 拍 (137 BPM) の長さ: 60 / 137 * 44100 = 19313 サンプル付近
	REQUIRE(std::abs(onsetA - 19313) <= 3);
}

TEST_CASE("MultiChipSequencer chip below the output rate is repeated up to the output rate", "[mml][foundation][multichip]")
{
	MultiChipSequencer seq;
	const int a = seq.addChip(std::make_unique<DummySineDriver>(22050));
	seq.addTrack(a, 0, "T120 O3 L1 V15 A");  // 220 Hz
	const StereoBuffer mix = seq.render();
	REQUIRE(mix.frames() > 80000);
	REQUIRE(toneLevel(mix, 220.0) > 0.05);
}

TEST_CASE("MultiChipSequencer output is identical across runs and across thread settings", "[mml][foundation][multichip][determinism]")
{
	auto build = [](bool parallel)
	{
		MultiChipSequencer::Options options;
		options.parallelChips = parallel;
		MultiChipSequencer seq(options);
		const int a = seq.addChip(std::make_unique<DummySineDriver>(49716), 0.7f);
		const int b = seq.addChip(std::make_unique<DummySineDriver>(55930), 0.5f);
		const int c = seq.addChip(std::make_unique<DummySineDriver>(96000));
		seq.addTrack(a, 0, "T150 O4 L8 CDEFGAB>C T97 <CDEF");
		seq.addTrack(b, 0, "T150 O3 L4 CEGC [EG]3");
		seq.addTrack(c, 0, "O5 L16 V9 CEGEC&D _E R8 G");
		return seq.render(2);
	};

	const StereoBuffer serial1 = build(false);
	const StereoBuffer serial2 = build(false);
	const StereoBuffer threaded = build(true);
	REQUIRE(serial1.samples == serial2.samples);
	REQUIRE(serial1.samples == threaded.samples);
	REQUIRE_FALSE(serial1.empty());
}

TEST_CASE("MultiChipSequencer rejects a track that points to a missing chip or channel", "[mml][foundation][multichip]")
{
	MultiChipSequencer seq;
	const int a = seq.addChip(std::make_unique<DummySineDriver>(49716));
	REQUIRE_THROWS_AS(seq.addTrack(a + 1, 0, "C"), std::invalid_argument);
	REQUIRE_THROWS_AS(seq.addTrack(a, 1, "C"), std::invalid_argument);
	REQUIRE_THROWS_AS(seq.addChip(nullptr), std::invalid_argument);
	REQUIRE(seq.trackCount() == 0);
}

TEST_CASE("Dummy chip state round trip reproduces the same samples", "[mml][foundation][device]")
{
	DummySineDriver driver(48000);
	driver.noteOn(0, 69, 15);
	std::vector<float> warm(2000);
	driver.device().generate(warm.data(), 1000);

	const auto saved = driver.device().saveState();
	std::vector<float> first(2000);
	driver.device().generate(first.data(), 1000);

	REQUIRE(driver.device().loadState(saved));
	std::vector<float> second(2000);
	driver.device().generate(second.data(), 1000);
	REQUIRE(first == second);
}
