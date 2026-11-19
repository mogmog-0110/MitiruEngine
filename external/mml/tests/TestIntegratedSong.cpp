/// @file TestIntegratedSong.cpp
/// @brief 曲のテキストの統合 (ChipSong と ModernSongScript が同じ #track の行を読む) と、OPNA の MML の読み取りの検査

#include <catch2/catch_test_macros.hpp>

#include "Analysis.hpp"
#include "SyntheticSf2.hpp"

#include <mitiru_mml/MitiruMML.hpp>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mitiru_mml;
using namespace mitiru_mml_test;

namespace
{

/// @brief 全部のチップを 1 本ずつ書いたテキスト
const char* ALL_CHIPS_TEXT =
	"#tempo 120\n"
	"#track a chip=opna voice=piano\nC\n"
	"#track b chip=opna part=ssg\nC\n"
	"#track c chip=opna rhythm\nB8\n"
	"#track d chip=opm voice=lead pan=left vol=9 ch=3\nC\n"
	"#track e chip=opl3 voice=pluck\nC\n"
	"#track f chip=sn76489 ch=noise\nC\n"
	"#track g chip=ay8910 voice=noise\nC\n"
	"#track h chip=nes ch=triangle\nC\n"
	"#track i chip=synth voice=supersaw group=lead\nC\n"
	"#track j chip=drums drum=kick group=drums\nC\n"
	"#track k chip=sampler voice=5 group=keys\nC\n";

std::shared_ptr<const Sf2File> testSoundFont()
{
	const auto bytes = buildSyntheticSf2();
	auto sf = std::make_shared<Sf2File>();
	REQUIRE(sf->loadFromMemory(bytes.data(), bytes.size()));
	return sf;
}

/// @brief 検査用の短い曲を描く。リミッターとディザは切って、数字がそのまま出るようにする
ModernSong::Result renderText(const std::string& body)
{
	auto parsed = ModernSongScript::parse("#tempo 120\n#limiter off\n#tail 0.3\n" + body);
	REQUIRE(parsed.ok());
	return parsed.song.render();
}

bool sameEvents(const std::vector<SeqEvent>& a, const std::vector<SeqEvent>& b)
{
	if (a.size() != b.size())
	{
		return false;
	}
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		if (a[i].tick != b[i].tick || a[i].type != b[i].type || a[i].channel != b[i].channel
			|| a[i].a != b[i].a || a[i].b != b[i].b || a[i].c != b[i].c)
		{
			return false;
		}
	}
	return true;
}

/// @brief OPNA の FM チャンネル 0 に載せた MML のイベント (拡張文法は無効)
std::vector<SeqEvent> opnaEvents(const std::string& mml)
{
	const OpnaDriver probe;
	TrackTarget target;
	target.channel = 0;
	target.caps = probe.channelCaps(0);
	return MmlEventBuilder::fromMelodic(MmlEventBuilder::expandLoops(MmlParser::parse(mml)), target);
}

} // namespace

TEST_CASE("One #track line selects every chip, and both readers see the same tracks", "[mml][integrated]")
{
	const auto read = SongText::parse(ALL_CHIPS_TEXT);
	REQUIRE(read.errors.empty());
	REQUIRE(read.tracks.size() == 11);
	const ChipKind expectedKinds[] = {ChipKind::OpnaFm, ChipKind::OpnaSsg, ChipKind::OpnaRhythm, ChipKind::Opm,
		ChipKind::Opl3, ChipKind::Sn76489, ChipKind::Ay8910, ChipKind::Nes, ChipKind::Synth, ChipKind::Drums,
		ChipKind::Sampler};
	for (std::size_t i = 0; i < 11; ++i)
	{
		INFO("track " << i);
		CHECK(read.tracks[i].kind == expectedKinds[i]);
	}
	CHECK(read.tracks[3].channel == 3);
	CHECK(read.tracks[3].pan == 0);
	CHECK(read.tracks[3].volume == 9);
	CHECK(read.tracks[8].group == "lead");
	CHECK(read.tracks[9].drum == 0);
	CHECK(read.tracks[2].rhythm);
	REQUIRE(read.directives.size() == 1);
	CHECK(read.directives[0].text == "#tempo 120");

	const auto script = ModernSongScript::parse(ALL_CHIPS_TEXT);
	CHECK(script.ok());
	CHECK(script.song.trackCount() == 11);
	CHECK(script.song.options().tempo == 120);

	const auto chipSong = ChipSong::parse(ALL_CHIPS_TEXT);
	CHECK(chipSong.errors.empty());
	CHECK(chipSong.tracks.size() == 11);
	REQUIRE(chipSong.ignored.size() == 1);
	CHECK(chipSong.ignored[0] == "#tempo 120");
}

TEST_CASE("Classic chips play inside a ModernSong group with pan and vol equal to P and V", "[mml][integrated]")
{
	const auto viaOptions = renderText("#track t chip=opm voice=1 pan=left vol=7 group=g\nO4 CDEF\n");
	const auto viaMml = renderText("#track t chip=opm voice=1 group=g\nP0 V7 O4 CDEF\n");
	REQUIRE(viaOptions.mix.samples.size() == viaMml.mix.samples.size());
	CHECK(viaOptions.mix.samples == viaMml.mix.samples);
	CHECK(peakOfStereo(viaOptions.mix) > 0.01);

	const std::vector<float> left = channelOf(viaOptions.mix, 0);
	const std::vector<float> right = channelOf(viaOptions.mix, 1);
	CHECK(peakOf(left) > 4.0 * peakOf(right));
}

TEST_CASE("Every classic chip makes sound on its own track and the stems add up to the mix", "[mml][integrated]")
{
	struct Case
	{
		const char* chip;
		const char* extra;
	};
	const Case cases[] = {{"opna", ""}, {"opna", "part=ssg"}, {"opm", ""}, {"opl", ""}, {"opl2", ""}, {"opl3", ""},
		{"sn76489", ""}, {"ay8910", ""}, {"nes", ""}};
	for (const auto& c : cases)
	{
		const std::string line = std::string("#track t chip=") + c.chip + " " + c.extra + " group=g\nO4 L4 CEGC\n";
		INFO(line);
		const auto result = renderText(line);
		CHECK(peakOfStereo(result.mix) > 0.01);
		REQUIRE(result.stems.size() == 1);
		CHECK(result.stems[0].audio.samples == result.mix.samples);
	}
}

TEST_CASE("A mixed song renders the same PCM every time, with or without threads", "[mml][integrated][determinism]")
{
	const std::string text = std::string("#limiter off\n#tail 0.3\n") +
		"#track lead chip=opm voice=lead pan=left group=a\nO5 L8 CDEFGAB>C\n"
		"#track bass chip=nes ch=triangle group=b\nO2 L4 CGCG\n"
		"#track arp chip=sn76489 group=c\nO4 L16 [CEGE]4\n"
		"#track pad chip=synth voice=pad group=d\nO3 L1 'CEG'\n"
		"#track kick chip=drums drum=kick group=e\nO4 L4 [CCCC]1\n";
	auto first = ModernSongScript::parse(text);
	auto second = ModernSongScript::parse(text);
	REQUIRE(first.ok());
	REQUIRE(second.ok());
	second.song.options().parallelGroups = true;
	const auto a = first.song.render();
	const auto b = second.song.render();
	REQUIRE(a.mix.samples.size() > 44100u);
	CHECK(a.mix.samples == b.mix.samples);
	CHECK(a.mix.samples == first.song.render().mix.samples);
}

TEST_CASE("ch= picks the channel and the automatic channels step around it", "[mml][integrated]")
{
	// 8 本のうち 1 本が ch=3 で、残り 7 本が自動 (0〜2、4〜7)。9 本目は溢れる
	std::string eight = "#track t0 chip=opm ch=3 group=g\nC\n";
	for (int i = 1; i < 8; ++i)
	{
		eight += "#track t" + std::to_string(i) + " chip=opm group=g\nC\n";
	}
	auto ok = ModernSongScript::parse(eight);
	REQUIRE(ok.ok());
	CHECK_NOTHROW(ok.song.render());

	auto nine = ModernSongScript::parse(eight + "#track t8 chip=opm group=g\nC\n");
	REQUIRE(nine.ok());
	CHECK_THROWS_AS(nine.song.render(), std::invalid_argument);

	auto dup = ModernSongScript::parse("#track a chip=opm ch=2 group=g\nC\n#track b chip=opm ch=2 group=g\nD\n");
	REQUIRE(dup.ok());
	CHECK_THROWS_AS(dup.song.render(), std::invalid_argument);

	auto missing = ModernSongScript::parse("#track a chip=opm ch=8 group=g\nC\n");
	REQUIRE(missing.ok());
	CHECK_THROWS_AS(missing.song.render(), std::invalid_argument);

	// 別のグループは別のチップなので、同じチャンネルを使える
	auto separate = ModernSongScript::parse("#track a chip=opm ch=2 group=g\nC\n#track b chip=opm ch=2 group=h\nD\n");
	REQUIRE(separate.ok());
	CHECK_NOTHROW(separate.song.render());
}

TEST_CASE("ChipSong refuses the modern chips with the line number and keeps the directives", "[mml][integrated]")
{
	const auto result = ChipSong::build(
		"#tempo 124\n"
		"#duck target=a by=b\n"
		"#track chip=opm\nC\n"
		"#track chip=synth voice=pad\nC\n");
	REQUIRE_FALSE(result.ok());
	REQUIRE(result.errors.size() == 1);
	CHECK(result.errors[0].find("5 行目") != std::string::npos);
	CHECK(result.errors[0].find("ModernSongScript") != std::string::npos);
	CHECK(result.ignored.size() == 2);

	// 現代的なチップが無ければ、同じ指示の入ったテキストも鳴る
	const auto classic = ChipSong::build("#tempo 124\n#swing 0.58\n#track a chip=opm\nO4 CDEF\n#track b chip=sn76489 group=x\nO4 CDEF\n");
	CHECK(classic.ok());
	CHECK(classic.ignored.size() == 2);
}

TEST_CASE("Text without a #track line is one OPNA FM track, and stray MML before a #track is an error", "[mml][integrated]")
{
	const auto plain = SongText::parse("T120 O4 CDEF\n");
	REQUIRE(plain.errors.empty());
	REQUIRE(plain.tracks.size() == 1);
	CHECK(plain.tracks[0].kind == ChipKind::OpnaFm);

	const auto script = ModernSongScript::parse("T120 O4 CDEF\n");
	CHECK(script.ok());
	CHECK(script.song.trackCount() == 1);

	const auto stray = SongText::parse("O4 C\n#track chip=opm\nD\n");
	REQUIRE(stray.errors.size() == 1);
	CHECK(stray.errors[0].find("1 行目") != std::string::npos);
	CHECK(stray.tracks.size() == 1);
}

TEST_CASE("A track name is the first word after #track and any other bare word is an error", "[mml][integrated]")
{
	const auto named = SongText::parse("#track kick chip=drums drum=kick noswing\nC\n#track chip=opm\nC\n");
	REQUIRE(named.errors.empty());
	CHECK(named.tracks[0].name == "kick");
	CHECK_FALSE(named.tracks[0].swing);
	CHECK(named.tracks[1].name == "track2");

	const auto late = SongText::parse("#track chip=opm novalue\nC\n");
	REQUIRE(late.errors.size() == 1);
	CHECK(late.errors[0].find("novalue") != std::string::npos);

	const auto many = SongText::parse("#track a b chip=opm\nC\n");
	CHECK(many.errors.size() == 1);
}

TEST_CASE("The same bad #track lines are reported by both readers", "[mml][integrated]")
{
	const std::string bad =
		"#track a chip=sid\nC\n"
		"#track b chip=opm voice=nothing\nC\n"
		"#track c chip=drums voice=2\nC\n"
		"#track d chip=opm part=ssg\nC\n"
		"#track e chip=synth rhythm\nC\n"
		"#track f chip=drums drum=wrong\nC\n";
	const auto script = ModernSongScript::parse(bad);
	const auto chips = ChipSong::parse(bad);
	CHECK_FALSE(script.ok());
	CHECK(script.errors.size() == 6);
	CHECK(chips.errors == script.errors);
}

TEST_CASE("The sampler takes SF2 voices and drums through the same line", "[mml][integrated]")
{
	const std::string text = std::string("#limiter off\n#tail 0.3\n") +
		"#track keys chip=sampler voice=0 ch=2 pan=right vol=11 group=k\nO4 L4 CEGC\n"
		"#track kit chip=sampler drum=kit group=d\nO2 L4 C D C D\n";
	auto parsed = ModernSongScript::parse(text, testSoundFont());
	REQUIRE(parsed.ok());
	const auto result = parsed.song.render();
	CHECK(peakOfStereo(result.mix) > 0.01);
	REQUIRE(result.stems.size() == 2);
	CHECK(peakOfStereo(result.stems[0].audio) > 0.01);
	CHECK(peakOfStereo(result.stems[1].audio) > 0.01);
}

TEST_CASE("OPNA tracks read an X command as one command and drop it", "[mml][integrated][opna]")
{
	CHECK(sameEvents(opnaEvents("O4 C Xcutoff=2000 D E"), opnaEvents("O4 C D E")));
	CHECK(sameEvents(opnaEvents("O4 C xres=40 D"), opnaEvents("O4 C D")));
	CHECK(sameEvents(opnaEvents("O4 C Xcutoff=500>4000/8 D"), opnaEvents("O4 C D")));
	CHECK(sameEvents(opnaEvents("O4 C Xnothing=3 D"), opnaEvents("O4 C D")));
}

TEST_CASE("OPNA tracks play only the first tone of a chord", "[mml][integrated][opna]")
{
	CHECK(sameEvents(opnaEvents("O4 'CEG'4 D"), opnaEvents("O4 C4 D")));
	CHECK(sameEvents(opnaEvents("O4 L8 'C>EG'. D"), opnaEvents("O4 L8 C. D")));
}

TEST_CASE("MML without X, x or a quote makes no parameter commands", "[mml][integrated][opna]")
{
	const std::string plain = "T140 O4 L8 V12 [CDE F+G A-B]2 >C4. <B4 &B4 R4 @FM3 P1 Q6 D4.";
	const auto commands = MmlParser::parse(plain);
	CHECK_FALSE(commands.empty());
	for (const auto& cmd : commands)
	{
		CHECK(cmd.type != CommandType::Param);
	}
}

TEST_CASE("An OPNA rhythm track keeps FM channels 3 to 5 for the drums", "[mml][integrated]")
{
	const std::string rhythm = "#track r chip=opna rhythm group=g\nB8 S8\n";
	std::string three = rhythm;
	for (int i = 0; i < 3; ++i)
	{
		three += "#track f" + std::to_string(i) + " chip=opna group=g\nC\n";
	}
	auto ok = ModernSongScript::parse(three);
	REQUIRE(ok.ok());
	CHECK_NOTHROW(ok.song.render());

	auto four = ModernSongScript::parse(three + "#track f3 chip=opna group=g\nC\n");
	REQUIRE(four.ok());
	CHECK_THROWS_AS(four.song.render(), std::invalid_argument);

	auto explicitChannel = ModernSongScript::parse(rhythm + "#track f chip=opna ch=4 group=g\nC\n");
	REQUIRE(explicitChannel.ok());
	CHECK_THROWS_AS(explicitChannel.song.render(), std::invalid_argument);

	// リズムが別のグループにあれば、FM は 6 本使える
	auto separate = ModernSongScript::parse(rhythm + "#track f chip=opna ch=4 group=h\nC\n");
	REQUIRE(separate.ok());
	CHECK_NOTHROW(separate.song.render());
}

TEST_CASE("A melodic sampler track cannot take the GM percussion channel", "[mml][integrated]")
{
	auto melodic = ModernSongScript::parse("#track a chip=sampler voice=0 ch=9 group=g\nC\n", testSoundFont());
	REQUIRE(melodic.ok());
	CHECK_THROWS_AS(melodic.song.render(), std::invalid_argument);

	auto drum = ModernSongScript::parse("#track a chip=sampler drum=kit group=g\nO2 C\n", testSoundFont());
	REQUIRE(drum.ok());
	CHECK_NOTHROW(drum.song.render());
}
