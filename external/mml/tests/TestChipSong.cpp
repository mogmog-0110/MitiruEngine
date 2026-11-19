/// @file TestChipSong.cpp
/// @brief トラックごとにチップを選べる曲のテキスト (ChipSong、#track の行) の検査

#include <catch2/catch_test_macros.hpp>

#include "ChipHashes.hpp"
#include "ChipSongs.hpp"

#include <mitiru_mml/ChipSong.hpp>
#include <mitiru_mml/OpnaSequencer.hpp>

#include <algorithm>
#include <iostream>
#include <string>

using namespace mitiru_mml;
using namespace mitiru_mml_test;

namespace
{

/// @brief SongTrackDef の並びを、ch= と voice= を明示した #track 形式のテキストにする
template <std::size_t N>
std::string toText(const char* chip, const SongTrackDef (&tracks)[N])
{
	std::string text;
	for (const auto& t : tracks)
	{
		text += std::string("#track chip=") + chip + " ch=" + std::to_string(t.channel)
			+ " voice=" + std::to_string(t.voice) + "\n" + t.mml + "\n";
	}
	return text;
}

bool sameSamples(const StereoBuffer& a, const StereoBuffer& b)
{
	return a.samples == b.samples;
}

/// @brief 3 つのチップを 1 曲に載せた例
const char* const MULTI_CHIP_SONG =
	"#title multi chip example\n"
	"#swing 0.58\n"
	"#track chip=opm voice=lead pan=left\n"
	"T140 O5 L8 V13 E G A B >C4 <B A G E4 R4 [EG]2 A4 B4 >C2\n"
	"#track chip=opm voice=bass\n"
	"O2 V12 L4 [EEGE]2 [AABB]2\n"
	"#track chip=sn76489 pan=right\n"
	"O4 V8 L2 E A E A\n"
	"#track chip=sn76489 ch=noise\n"
	"O4 V9 L16 [CRRR]16\n"
	"#track chip=opna-ssg\n"
	"O3 V9 L8 [EBEB]8\n";

} // namespace

TEST_CASE("ChipSong reads #track lines with their options and joins the MML lines that follow", "[mml][song]")
{
	const auto parsed = ChipSong::parse(
		"#track chip=opm voice=lead ch=3 pan=right vol=9\n"
		"T120 O4 CDEF\n"
		"GAB>C\n"
		"\n"
		"#track chip=ym3812\n"
		"O3 C\n");
	REQUIRE(parsed.errors.empty());
	REQUIRE(parsed.tracks.size() == 2);
	REQUIRE(parsed.tracks[0].kind == ChipKind::Opm);
	REQUIRE(parsed.tracks[0].voice == 4);
	REQUIRE(parsed.tracks[0].channel == 3);
	REQUIRE(parsed.tracks[0].pan == 2);
	REQUIRE(parsed.tracks[0].volume == 9);
	REQUIRE(parsed.tracks[0].mml == "T120 O4 CDEF\nGAB>C");
	REQUIRE(parsed.tracks[0].line == 1);
	REQUIRE(parsed.tracks[1].kind == ChipKind::Opl2);
	REQUIRE(parsed.tracks[1].channel == -1);
	REQUIRE(parsed.tracks[1].mml == "O3 C");
	REQUIRE(parsed.tracks[1].line == 5);
}

TEST_CASE("ChipSong ignores unknown # lines and remembers them", "[mml][song]")
{
	const auto parsed = ChipSong::parse("#swing 0.58\n#duck target=bass\n#track chip=nes\nO4 C\n");
	REQUIRE(parsed.errors.empty());
	REQUIRE(parsed.ignored.size() == 2);
	REQUIRE(parsed.ignored[0] == "#swing 0.58");
	REQUIRE(parsed.tracks.size() == 1);
}

TEST_CASE("ChipSong accepts chip and voice aliases and names", "[mml][song]")
{
	const auto parsed = ChipSong::parse(
		"#track chip=YM2151 voice=Bell\nC\n"
		"#track chip=psg\nC\n"
		"#track chip=ay voice=noise ch=c\nC\n"
		"#track chip=apu ch=triangle\nC\n"
		"#track chip=opl3 voice=pluck\nC\n"
		"#track chip=opna voice=koto\nC\n");
	REQUIRE(parsed.errors.empty());
	REQUIRE(parsed.tracks[0].kind == ChipKind::Opm);
	REQUIRE(parsed.tracks[0].voice == 5);
	REQUIRE(parsed.tracks[1].kind == ChipKind::Sn76489);
	REQUIRE(parsed.tracks[2].kind == ChipKind::Ay8910);
	REQUIRE(parsed.tracks[2].voice == 1);
	REQUIRE(parsed.tracks[2].channel == 2);
	REQUIRE(parsed.tracks[3].kind == ChipKind::Nes);
	REQUIRE(parsed.tracks[3].channel == NesApuDriver::TRIANGLE);
	REQUIRE(parsed.tracks[4].kind == ChipKind::Opl3);
	REQUIRE(parsed.tracks[4].voice == 7);
	REQUIRE(parsed.tracks[5].voice == 8);
}

TEST_CASE("ChipSong reports every unreadable line with its line number and builds nothing", "[mml][song]")
{
	const auto result = ChipSong::build(
		"#track chip=sid\nC\n"
		"#track chip=opm voice=nothing\nC\n"
		"#track chip=opm bogus=1\nC\n"
		"#track chip=opm pan=up\nC\n"
		"#track chip=opm vol=99\nC\n"
		"#track chip=opm ch=x\nC\n"
		"#track chip=opm novalue\nC\n");
	REQUIRE_FALSE(result.ok());
	REQUIRE(result.sequencer == nullptr);
	REQUIRE(result.errors.size() >= 7);
	REQUIRE(result.errors[0].find("1 行目") != std::string::npos);
	REQUIRE(result.errors[0].find("sid") != std::string::npos);
}

TEST_CASE("ChipSong assigns channels in declaration order and refuses to overflow a chip", "[mml][song]")
{
	// SN76489 の矩形波は 3 本。4 本目は自動では割り当てず、ノイズは名前で指定する
	auto ok = ChipSong::build("#track chip=sn76489\nC\n#track chip=sn76489\nD\n#track chip=sn76489\nE\n#track chip=sn76489 ch=noise\nC\n");
	REQUIRE(ok.ok());

	const auto overflow = ChipSong::build("#track chip=sn76489\nC\n#track chip=sn76489\nD\n#track chip=sn76489\nE\n#track chip=sn76489\nF\n");
	REQUIRE_FALSE(overflow.ok());
	REQUIRE(overflow.errors.size() == 1);
	REQUIRE(overflow.errors[0].find("7 行目") != std::string::npos);

	std::string nine;
	for (int i = 0; i < 9; ++i)
	{
		nine += "#track chip=opm\nC\n";
	}
	REQUIRE_FALSE(ChipSong::build(nine).ok());

	const auto dup = ChipSong::build("#track chip=opm ch=2\nC\n#track chip=opm ch=2\nD\n");
	REQUIRE_FALSE(dup.ok());
	REQUIRE(dup.errors[0].find("すでに使われています") != std::string::npos);

	const auto outOfRange = ChipSong::build("#track chip=opm ch=8\nC\n");
	REQUIRE_FALSE(outOfRange.ok());
}

TEST_CASE("ChipSong leaves FM channels 3 to 5 to the drums when an OPNA rhythm track exists", "[mml][song]")
{
	const auto fine = ChipSong::build("#track chip=opna\nC\n#track chip=opna\nD\n#track chip=opna\nE\n#track chip=opna-rhythm\nB8 S8\n");
	REQUIRE(fine.ok());
	const auto tooMany = ChipSong::build("#track chip=opna\nC\n#track chip=opna\nD\n#track chip=opna\nE\n#track chip=opna\nF\n#track chip=opna-rhythm\nB8\n");
	REQUIRE_FALSE(tooMany.ok());
}

TEST_CASE("ChipSong rejects voice numbers beyond the chip and FM channels the drums use", "[mml][song]")
{
	REQUIRE(ChipSong::build("#track chip=opm voice=7\nC\n").ok());
	REQUIRE_FALSE(ChipSong::build("#track chip=opm voice=8\nC\n").ok());
	REQUIRE_FALSE(ChipSong::build("#track chip=opna voice=12\nC\n").ok());
	REQUIRE_FALSE(ChipSong::build("#track chip=sn76489 voice=2\nC\n").ok());
	REQUIRE_FALSE(ChipSong::build("#track chip=nes voice=4\nC\n").ok());

	const auto clash = ChipSong::build("#track chip=opna ch=4\nC\n#track chip=opna-rhythm\nB8\n");
	REQUIRE_FALSE(clash.ok());
	REQUIRE(clash.errors[0].find("リズム") != std::string::npos);
	REQUIRE(ChipSong::build("#track chip=opna ch=4\nC\n").ok());
}

TEST_CASE("A song without any header plays on OPNA exactly like the existing OpnaSequencer", "[mml][song]")
{
	const std::string mml = "T120 O4 L8 CDEFGAB>C C4 R4";

	OpnaSequencer legacy;
	legacy.addFmTrack(mml, 0);
	const StereoBuffer expected = legacy.renderStereo();

	const auto result = ChipSong::build(mml);
	REQUIRE(result.ok());
	REQUIRE(sameSamples(result.sequencer->render(), expected));

	// 見出しを付けて同じことを書いても同じ
	const auto explicitHeader = ChipSong::build("#track chip=opna voice=piano\n" + mml + "\n");
	REQUIRE(explicitHeader.ok());
	REQUIRE(sameSamples(explicitHeader.sequencer->render(), expected));
}

TEST_CASE("A multi-track OPNA song through #track matches OpnaSequencer for FM, SSG and rhythm", "[mml][song]")
{
	const std::string melody = "T130 O4 L8 CDEFGAB>C";
	const std::string bass = "O2 L4 CGCG";
	const std::string ssg = "O4 L8 V9 EGEG";
	const std::string drums = "L8 B S B S";

	OpnaSequencer legacy;
	legacy.addFmTrack(melody, 0);
	legacy.addFmTrack(bass, 6);
	legacy.addSsgTrack(ssg);
	legacy.addRhythmTrack(drums);
	const StereoBuffer expected = legacy.renderStereo();

	const std::string text =
		"#track chip=opna voice=piano\n" + melody + "\n"
		"#track chip=opna voice=bass\n" + bass + "\n"
		"#track chip=opna-ssg\n" + ssg + "\n"
		"#track chip=opna-rhythm\n" + drums + "\n";
	const auto result = ChipSong::build(text);
	REQUIRE(result.ok());
	REQUIRE(sameSamples(result.sequencer->render(), expected));
}

TEST_CASE("Per-track pan and vol options are the same as writing P and V at the start of the MML", "[mml][song]")
{
	const auto viaOptions = ChipSong::build("#track chip=opm voice=1 pan=left vol=7\nO4 CDEF\n");
	const auto viaMml = ChipSong::build("#track chip=opm voice=1\nP0 V7 O4 CDEF\n");
	REQUIRE(viaOptions.ok());
	REQUIRE(viaMml.ok());
	REQUIRE(sameSamples(viaOptions.sequencer->render(), viaMml.sequencer->render()));
}

TEST_CASE("Voice names and numbers select the same voice", "[mml][song]")
{
	const auto byName = ChipSong::build("#track chip=opm voice=lead\nO4 CDEF\n");
	const auto byNumber = ChipSong::build("#track chip=opm voice=4\nO4 CDEF\n");
	REQUIRE(byName.ok());
	REQUIRE(byNumber.ok());
	REQUIRE(sameSamples(byName.sequencer->render(), byNumber.sequencer->render()));
}

TEST_CASE("Chip songs written as #track text render the same PCM as building the tracks directly", "[mml][song][hash]")
{
	struct Case
	{
		const char* chip;
		const char* hashName;
		std::string text;
	};
	std::vector<Case> cases;
	cases.push_back({"opm", "opm_song", toText("opm", OPM_SONG)});
	cases.push_back({"opl", "opl_song", toText("opl", OPL_SONG)});
	cases.push_back({"opl2", "opl2_song", toText("opl2", OPL_SONG)});
	cases.push_back({"sn76489", "sn76489_song", toText("sn76489", SN76489_SONG)});
	cases.push_back({"ay8910", "ay8910_song", toText("ay8910", AY8910_SONG)});
	cases.push_back({"nes", "nes_song", toText("nes", NES_SONG)});
	for (const auto& c : cases)
	{
		const auto result = ChipSong::build(c.text);
		INFO(c.chip);
		REQUIRE(result.ok());
		const RenderedHash h = hashRender(*result.sequencer);
		const ChipHashEntry* expected = findChipHash(c.hashName);
		REQUIRE(expected != nullptr);
		REQUIRE(h.frames == expected->frames);
		REQUIRE(h.sha256 == expected->sha256);
	}
}

TEST_CASE("A song that uses several chips renders the same PCM every time, with or without threads", "[mml][song][hash]")
{
	MultiChipSequencer::Options threaded;
	threaded.parallelChips = true;
	const auto serial = ChipSong::build(MULTI_CHIP_SONG);
	const auto parallel = ChipSong::build(MULTI_CHIP_SONG, threaded);
	REQUIRE(serial.ok());
	REQUIRE(parallel.ok());
	REQUIRE(serial.ignored.size() == 2);

	const RenderedHash a = hashRender(*serial.sequencer);
	const RenderedHash b = hashRender(*parallel.sequencer);
	REQUIRE(a.frames > 44100u * 3u);
	REQUIRE(a.sha256 == b.sha256);

	const ChipHashEntry* expected = findChipHash("multi_chip_song");
	REQUIRE(expected != nullptr);
	INFO("frames " << a.frames << " sha256 " << a.sha256);
	REQUIRE(a.frames == expected->frames);
	REQUIRE(a.sha256 == expected->sha256);
}

TEST_CASE("Print song hashes", "[.emit]")
{
	const auto result = ChipSong::build(MULTI_CHIP_SONG);
	const RenderedHash h = hashRender(*result.sequencer);
	std::cout << "EMIT {\"multi_chip_song\", " << h.frames << "u, \"" << h.sha256 << "\"},\n";
}
