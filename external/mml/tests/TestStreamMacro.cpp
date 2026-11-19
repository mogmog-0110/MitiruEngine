/// @file TestStreamMacro.cpp
/// @brief #let と #macro を使う曲を ModernSongStream で出しても、一括の render() とビットまで同じになること

#include <catch2/catch_test_macros.hpp>

#include "StreamTestUtil.hpp"

#include <mitiru_mml/MitiruMML.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace mitiru_mml;
using namespace mitiru_mml_test;

namespace
{

// マクロの中でマクロと変数を呼び、ループの中でも使い、$ と複数のグループ、ダッキング、リミッターを通す
const char* const MACRO_SONG =
	"#let bpm = 132\n"
	"#let lead = 12\n"
	"#let oct = 4\n"
	"#tempo %bpm\n"
	"#loop 2\n"
	"#limiter ceiling=-9 release=60\n"
	"#tail 0.3\n"
	"#duck target=bass by=kick depth=-9 attack=3 release=140\n"
	"#macro hit = C4 C4\n"
	"#macro riff = O%{oct} L8 CDEG\n"
	"#macro bassline\n"
	"O2 L8 V%lead [C C >C< C]2 $ [A- A-]2\n"
	"#endmacro\n"
	"#track kick chip=drums drum=kick group=kick\n"
	"O4 [!hit]4\n"
	"#track bass chip=synth voice=saw-bass group=bass\n"
	"!bassline\n"
	"#track lead chip=opm voice=lead group=retro vol=%lead\n"
	"[!riff]3 R4 !{riff}\n"
	"#track beep chip=sn76489 group=retro\n"
	"V%lead O5 L16 [!{riff} C E G]2\n";

// MACRO_SONG のマクロと変数を手で展開したもの
const char* const EXPANDED_SONG =
	"#tempo 132\n"
	"#loop 2\n"
	"#limiter ceiling=-9 release=60\n"
	"#tail 0.3\n"
	"#duck target=bass by=kick depth=-9 attack=3 release=140\n"
	"#track kick chip=drums drum=kick group=kick\n"
	"O4 [C4 C4]4\n"
	"#track bass chip=synth voice=saw-bass group=bass\n"
	"O2 L8 V12 [C C >C< C]2 $ [A- A-]2\n"
	"#track lead chip=opm voice=lead group=retro vol=12\n"
	"[O4 L8 CDEG]3 R4 O4 L8 CDEG\n"
	"#track beep chip=sn76489 group=retro\n"
	"V12 O5 L16 [O4 L8 CDEG C E G]2\n";

} // namespace

TEST_CASE("ModernSongStream equals render() for a song with macros and variables", "[stream][grammar][modern]")
{
	auto parsed = ModernSongScript::parse(MACRO_SONG);
	REQUIRE(parsed.ok());
	const ModernSong::Result batch = parsed.song.render();
	REQUIRE(batch.mix.frames() > 44100);

	const std::vector<ChunkPlan> plans = {fixedChunk(1), fixedChunk(511), fixedChunk(4096), fixedChunk(WHOLE_SONG), randomChunks(5, 3000)};
	for (const auto& plan : plans)
	{
		ModernSongStream stream(parsed.song);
		REQUIRE(sameBits(drain(stream, plan), batch.mix.samples));
		REQUIRE(stream.finished());
		REQUIRE(stream.limiterReductionDb() == batch.limiterReductionDb);
	}

	// 手で展開した曲を一括で出したものとも同じビットになる (マクロの展開がストリームの手前で済んでいることの確認)
	auto expanded = ModernSongScript::parse(EXPANDED_SONG);
	REQUIRE(expanded.ok());
	REQUIRE(sameBits(expanded.song.render().mix.samples, batch.mix.samples));
	ModernSongStream expandedStream(expanded.song);
	REQUIRE(sameBits(drain(expandedStream, fixedChunk(512)), batch.mix.samples));
}

TEST_CASE("ModernSongStream with macros reads the same 16-bit PCM as toPcm16", "[stream][grammar][modern]")
{
	auto parsed = ModernSongScript::parse(MACRO_SONG);
	REQUIRE(parsed.ok());
	const auto expected = parsed.song.render().toPcm16(parsed.song.options().dither);

	ModernSongStream stream(parsed.song);
	std::vector<std::int16_t> pcm;
	std::vector<std::int16_t> buffer(333 * 2);
	for (;;)
	{
		const std::size_t n = stream.readPcm16(buffer.data(), 333);
		pcm.insert(pcm.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(n * 2));
		if (n < 333)
		{
			break;
		}
	}
	REQUIRE(pcm == expected);
}
