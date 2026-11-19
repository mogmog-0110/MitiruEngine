/// @file TestGrammar.cpp
/// @brief 曲のテキストの文法拡張（#macro、#let、send=）と SongLint の検査

#include <catch2/catch_test_macros.hpp>

#include "Analysis.hpp"
#include "IntegratedExamples.hpp"
#include "ModernExamples.hpp"

#include <mitiru_mml/MitiruMML.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef MITIRU_MML_EXAMPLES_DIR
#define MITIRU_MML_EXAMPLES_DIR "."
#endif

using namespace mitiru_mml;
using namespace mitiru_mml_test;

namespace
{

std::vector<std::string> splitLinesOf(const std::string& text)
{
	std::vector<std::string> lines;
	std::istringstream in(text);
	std::string line;
	while (std::getline(in, line))
	{
		lines.push_back(line);
	}
	return lines;
}

bool contains(const std::string& text, const std::string& part)
{
	return text.find(part) != std::string::npos;
}

bool anyContains(const std::vector<std::string>& lines, const std::string& part)
{
	for (const auto& line : lines)
	{
		if (contains(line, part)) return true;
	}
	return false;
}

/// @brief 展開されない MML の各文字について、出どころが入力の同じ文字を指すか調べる
bool originsPointAtSource(const std::string& text, const SongTextResult& song)
{
	const std::vector<std::string> lines = splitLinesOf(text);
	for (const auto& track : song.tracks)
	{
		if (track.origins.size() != track.mml.size())
		{
			return false;
		}
		for (std::size_t i = 0; i < track.mml.size(); ++i)
		{
			const SourceOrigin& o = track.origins[i];
			if (o.macro >= 0 || track.mml[i] == '\n')
			{
				continue;
			}
			if (o.line < 1 || static_cast<std::size_t>(o.line) > lines.size())
			{
				return false;
			}
			const std::string& line = lines[static_cast<std::size_t>(o.line - 1)];
			if (o.column < 1 || static_cast<std::size_t>(o.column) > line.size()
				|| line[static_cast<std::size_t>(o.column - 1)] != track.mml[i])
			{
				return false;
			}
		}
	}
	return true;
}

std::string readFile(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

std::vector<SeqEvent> eventsOf(const std::string& mml)
{
	TrackTarget target;
	target.channel = 0;
	return MmlEventBuilder::fromMelodic(MmlEventBuilder::expandLoops(MmlParser::parse(mml)), target);
}

/// @brief MML の末尾に $ を足し、最終 LoopPoint の tick を MML 全体の長さとして返す
long lengthByBuilder(const std::string& mml)
{
	long tick = -1;
	for (const auto& ev : eventsOf(mml + " $"))
	{
		if (ev.type == SeqEventType::LoopPoint)
		{
			tick = ev.tick;
		}
	}
	return tick;
}

} // namespace

TEST_CASE("Text without definitions is read exactly as before", "[mml][grammar]")
{
	std::vector<std::string> scripts;
	for (const auto& example : allExamples())
	{
		scripts.push_back(example.script);
	}
	scripts.push_back("T120 O4 L8 CDEFGAB>C\n; comment\n[CDEF]4 $ R4");
	scripts.push_back("#tempo 100\n#track a chip=opm\n  O5 L8 CDE  \r\n\tFGA\r\n#track b chip=opna part=ssg\nO3 C1\n");
	for (const auto& script : scripts)
	{
		const SongTextResult song = SongText::parse(script);
		CHECK(song.errors.empty());
		CHECK(song.definitions.empty());
		CHECK(originsPointAtSource(script, song));
		for (const auto& track : song.tracks)
		{
			CHECK(track.sends.empty());
		}
	}

	const SongTextResult song = SongText::parse("#track a chip=opm\n  O5 L8 CDE  \r\n\tFGA\r\n");
	REQUIRE(song.tracks.size() == 1);
	CHECK(song.tracks[0].mml == "O5 L8 CDE\nFGA");
}

TEST_CASE("Macros and variables expand as plain text", "[mml][grammar]")
{
	const std::string text =
		"#let lead = 12\n"
		"#let bpm = 132\n"
		"#macro riff = O4 L8 CDEG\n"
		"#macro tail\n"
		"  [!riff]2 R4 ; comment\n"
		"\n"
		"  O3 L4 C\n"
		"#endmacro\n"
		"#tempo %bpm\n"
		"#track a chip=opna voice=piano vol=%lead\n"
		"V%lead !tail\n";
	const SongTextResult song = SongText::parse(text);
	REQUIRE(song.errors.empty());
	REQUIRE(song.tracks.size() == 1);
	CHECK(song.tracks[0].mml == "V12 [O4 L8 CDEG]2 R4 O3 L4 C");
	CHECK(song.tracks[0].volume == 12);
	REQUIRE(song.directives.size() == 1);
	CHECK(song.directives[0].text == "#tempo 132");

	CHECK(song.definitions.findMacro("riff")->uses == 1);
	CHECK(song.definitions.findMacro("tail")->uses == 1);
	CHECK(song.definitions.findVariable("lead")->uses == 2); // #track の vol= と MML の V
	CHECK(song.definitions.findVariable("bpm")->uses == 1);
}

TEST_CASE("Definitions apply wherever they are written", "[mml][grammar]")
{
	const SongTextResult song = SongText::parse(
		"#track a\n"
		"!late %x\n"
		"#macro late = C%x\n"
		"#let x = -3\n");
	REQUIRE(song.errors.empty());
	REQUIRE(song.tracks.size() == 1);
	CHECK(song.tracks[0].mml == "C-3 -3");
}

TEST_CASE("Braces end a name before letters or digits", "[mml][grammar]")
{
	const SongTextResult song = SongText::parse(
		"#let oct = 5\n"
		"#macro kick4 = O4 L4 [CCCC]\n"
		"#track a\n"
		"O%{oct}4 !{kick4}8\n");
	REQUIRE(song.errors.empty());
	CHECK(song.tracks[0].mml == "O54 O4 L4 [CCCC]8");
}

TEST_CASE("Macros call macros and variables", "[mml][grammar]")
{
	const SongTextResult song = SongText::parse(
		"#let v = 9\n"
		"#macro a = V%v C\n"
		"#macro b = !a D !a\n"
		"#track t\n"
		"!b E\n");
	REQUIRE(song.errors.empty());
	CHECK(song.tracks[0].mml == "V9 C D V9 C E");
	// 入れ子の中の文字は、一番外の呼び出し位置を指し、一番内側のマクロ名を持つ
	const auto& origins = song.tracks[0].origins;
	REQUIRE(origins.size() == song.tracks[0].mml.size());
	CHECK(origins[0].line == 5);
	CHECK(origins[0].column == 1);
	CHECK(song.definitions.macros()[static_cast<std::size_t>(origins[0].macro)].name == "a");
	CHECK(origins.back().macro == -1);
	CHECK(origins.back().column == 4);
}

TEST_CASE("Directive lines take variables", "[mml][grammar]")
{
	auto parsed = ModernSongScript::parse(
		"#let bpm = 150\n"
		"#let tailsec = 3\n"
		"#tempo %bpm\n"
		"#tail %tailsec\n"
		"#track a chip=synth\nC\n");
	REQUIRE(parsed.ok());
	CHECK(parsed.song.options().tempo == 150);
	CHECK(parsed.song.options().tailSeconds == 3.0);
}

TEST_CASE("Reading problems carry a line and a column", "[mml][grammar]")
{
	const SongTextResult song = SongText::parse(
		"#let a = 1\n"
		"#track t\n"
		"C !nosuch D %nothing\n");
	REQUIRE(song.errors.size() == 2);
	CHECK(song.errors[0] == "3 行目 3 列: マクロ 'nosuch' は定義されていません (#macro nosuch = ... で定義します)");
	CHECK(contains(song.errors[1], "3 行目 13 列: 変数 'nothing'"));
	CHECK(song.problems[0].line == 3);
	CHECK(song.problems[0].column == 3);

	// 定義がなくても名前の呼び出しは未定義エラーになり、! は MML の音符として読まれない
	const SongTextResult bare = SongText::parse("#track t\nC !riff D\n");
	REQUIRE(bare.errors.size() == 1);
	CHECK(contains(bare.errors[0], "2 行目 3 列"));
	CHECK(contains(bare.errors[0], "'riff'"));

	// インデントと CRLF があっても位置はずれない
	const SongTextResult indented = SongText::parse("#track t\r\n\t  C !x\r\n");
	REQUIRE(indented.errors.size() == 1);
	CHECK(contains(indented.errors[0], "2 行目 6 列"));
}

TEST_CASE("Bad definitions are reported", "[mml][grammar]")
{
	const auto errorsOf = [](const std::string& text)
	{
		return SongText::parse(text).errors;
	};

	CHECK(anyContains(errorsOf("#let x\n"), "= と整数"));
	CHECK(anyContains(errorsOf("#let x = 1.5\n"), "整数"));
	CHECK(anyContains(errorsOf("#let 9x = 1\n"), "名前"));
	CHECK(anyContains(errorsOf("#let x = 1\n#let x = 2\n"), "1 行目ですでに定義"));
	CHECK(anyContains(errorsOf("#macro m = C\n#macro m = D\n"), "1 行目ですでに定義"));
	CHECK(anyContains(errorsOf("#macro m =\n"), "本体が空"));
	CHECK(anyContains(errorsOf("#macro m\nC D\n"), "#endmacro がありません"));
	CHECK(anyContains(errorsOf("#macro m\nC\n#tempo 120\n#endmacro\n"), "#endmacro がありません"));
	CHECK(anyContains(errorsOf("#endmacro\n"), "対応する #macro がありません"));
	CHECK(anyContains(errorsOf("#macro m C\n"), "= と MML"));
	CHECK(anyContains(errorsOf("#macro a = !b\n#macro b = !a\n#track t\n!a\n"), "自分を呼んでいます"));
	CHECK(anyContains(errorsOf("#macro a = !a\n"), "自分を呼んでいます"));
	CHECK(anyContains(errorsOf("#macro a = !missing\n#track t\n!a\n"), "'missing' は定義されていません"));
	CHECK(anyContains(errorsOf("#macro a = V%missing\n#track t\n!a\n"), "'missing' は定義されていません"));
}

TEST_CASE("Macro definitions do not turn into tracks or directives", "[mml][grammar]")
{
	const SongTextResult song = SongText::parse(
		"#macro intro\nO4 C D E\n#endmacro\n"
		"#let n = 3\n"
		"#track a chip=opm\n!intro\n"
		"#macro outro\nG\n#endmacro\n");
	REQUIRE(song.errors.empty());
	REQUIRE(song.tracks.size() == 1);
	CHECK(song.directives.empty());
	CHECK(song.tracks[0].mml == "O4 C D E");

	auto script = ModernSongScript::parse("#macro m = C\n#let n = 1\n#track a chip=synth\n!m\n");
	CHECK(script.ok());
	CHECK(script.unknownDirectives.empty());
}

TEST_CASE("Odd definitions and references do not break the reader", "[mml][grammar]")
{
	for (const char* text : {"!", "%", "!{", "%{x", "!{}", "#macro", "#let", "#macro a =", "#macro a", "#macro a\n",
		"#track\n!a", "#let a = 1 ; note\n#track t\nV%a", "#macro a = C ; note\n#track t\n!a", "#track t\n!!a %%a !{a", "#let a = 99999999999\n"})
	{
		INFO(text);
		const SongTextResult song = SongText::parse(text);
		CHECK(song.errors.size() == song.problems.size());
		for (const auto& track : song.tracks)
		{
			CHECK(track.origins.size() == track.mml.size());
		}
	}

	// 注釈内の ! と % は呼び出しとして読まない
	const SongTextResult comment = SongText::parse("#track t\nC ; !nosuch %nothing\n");
	CHECK(comment.errors.empty());
	CHECK(comment.tracks[0].mml == "C ; !nosuch %nothing");

	const SongTextResult tail = SongText::parse("#let a = 1 ; note\n#macro m = C%a ; note\n#track t\n!m\n");
	REQUIRE(tail.errors.empty());
	CHECK(tail.tracks[0].mml == "C1");
}

TEST_CASE("Expanded songs render the same PCM as the hand-expanded text", "[mml][grammar][hash]")
{
	const std::string withMacros =
		"#let bpm = 120\n"
		"#let lead = 12\n"
		"#let pad = 8\n"
		"#macro riff = O4 L8 CDEG\n"
		"#macro bar\n"
		"[!riff]2 ; two bars\n"
		"#endmacro\n"
		"#tempo %bpm\n"
		"#limiter off\n"
		"#tail 0.4\n"
		"#track a chip=opna voice=piano vol=%lead\n"
		"!bar R4 O3 L4 C\n"
		"#track b chip=synth voice=pad group=pad vol=%pad\n"
		"O3 'CEG'1 !{riff}\n";
	const std::string byHand =
		"#tempo 120\n"
		"#limiter off\n"
		"#tail 0.4\n"
		"#track a chip=opna voice=piano vol=12\n"
		"[O4 L8 CDEG]2 R4 O3 L4 C\n"
		"#track b chip=synth voice=pad group=pad vol=8\n"
		"O3 'CEG'1 O4 L8 CDEG\n";

	auto macro = ModernSongScript::parse(withMacros);
	auto plain = ModernSongScript::parse(byHand);
	REQUIRE(macro.ok());
	REQUIRE(plain.ok());
	const std::string macroHash = hashPcm16(macro.song.render().toPcm16(true));
	const std::string plainHash = hashPcm16(plain.song.render().toPcm16(true));
	CHECK(macroHash == plainHash);
	CHECK(!macroHash.empty());
}

TEST_CASE("send= is read and checked", "[mml][grammar]")
{
	const SongTextResult song = SongText::parse(
		"#let amount = 35\n"
		"#track a chip=opm send=reverb:%amount,Delay:12.5\n"
		"C\n"
		"#track b chip=sn76489\n"
		"C\n");
	REQUIRE(song.errors.empty());
	REQUIRE(song.tracks[0].sends.size() == 2);
	CHECK(song.tracks[0].sends[0].effect == "reverb");
	CHECK(song.tracks[0].sends[0].percent == 35.0f);
	CHECK(song.tracks[0].sends[1].effect == "delay");
	CHECK(song.tracks[0].sends[1].percent == 12.5f);
	CHECK(song.tracks[1].sends.empty());

	const auto errorsOf = [](const std::string& line)
	{
		return SongText::parse("#track a " + line + "\nC\n").errors;
	};
	CHECK(anyContains(errorsOf("send=flanger:10"), "'flanger' は使えません"));
	CHECK(anyContains(errorsOf("send=reverb"), "名前:量"));
	CHECK(anyContains(errorsOf("send=reverb:abc"), "数で書きます"));
	CHECK(anyContains(errorsOf("send=reverb:120"), "0〜100 (%) の外"));
	CHECK(anyContains(errorsOf("send=reverb:-1"), "0〜100 (%) の外"));
	CHECK(anyContains(errorsOf("send=reverb:10,reverb:20"), "2 回書かれています"));
	// 列は send= の先頭を指す
	CHECK(contains(errorsOf("send=flanger:10")[0], "1 行目 10 列"));

	auto parsed = ModernSongScript::parse("#track a chip=synth send=chorus:20\nC\n#track b chip=synth\nC\n");
	REQUIRE(parsed.ok());
	REQUIRE(parsed.sends.size() == 1);
	CHECK(parsed.sends[0].track == "a");
	CHECK(parsed.sends[0].effect == "chorus");
	CHECK(parsed.sends[0].percent == 20.0f);
}

TEST_CASE("send= changes the rendered sound and leaves the dry stem alone", "[mml][grammar][hash]")
{
	auto without = ModernSongScript::parse("#limiter off\n#tail 0.3\n#track a chip=synth\nO4 L4 CDEF\n");
	auto with = ModernSongScript::parse("#limiter off\n#tail 0.3\n#track a chip=synth send=reverb:80\nO4 L4 CDEF\n");
	REQUIRE(without.ok());
	REQUIRE(with.ok());
	const auto dry = without.song.render();
	const auto wet = with.song.render();
	CHECK(hashPcm16(dry.toPcm16(false)) != hashPcm16(wet.toPcm16(false)));
	REQUIRE(wet.stems.size() == 2);
	CHECK(wet.stems[1].name == "send_reverb");
	CHECK(hashPcm16(dry.stems[0].audio.toPcm16Stereo()) == hashPcm16(wet.stems[0].audio.toPcm16Stereo()));

	// send=reverb:0 は書かないのと同じ。出力は 1 ビットも変わらない
	auto zero = ModernSongScript::parse("#limiter off\n#tail 0.3\n#track a chip=synth send=reverb:0\nO4 L4 CDEF\n");
	REQUIRE(zero.ok());
	CHECK(hashPcm16(zero.song.render().toPcm16(false)) == hashPcm16(dry.toPcm16(false)));
}

TEST_CASE("The parser records where each command starts", "[mml][grammar]")
{
	const std::string mml = "T120 O4 L8 C D&D _E [F]2 $ 'CE'4";
	const CommandList commands = MmlParser::parse(mml);
	REQUIRE(!commands.empty());
	for (const auto& cmd : commands)
	{
		REQUIRE(cmd.offset >= 0);
		REQUIRE(static_cast<std::size_t>(cmd.offset) < mml.size());
	}
	CHECK(commands[0].type == CommandType::Tempo);
	CHECK(commands[0].offset == 0);
	CHECK(commands[1].offset == 5);   // O4
	CHECK(mml[static_cast<std::size_t>(commands.back().offset)] == '\'');
}

namespace
{

/// @brief 検査し、重さごとの報告を文字列で返す
struct Lint
{
	SongValidation result;
	explicit Lint(const std::string& text) : result(MmlValidator::validateSong(text)) {}
	std::vector<std::string> warnings() const { return result.texts(DiagnosticSeverity::Warning); }
	std::vector<std::string> errors() const { return result.texts(DiagnosticSeverity::Error); }
};

} // namespace

TEST_CASE("Lint reports notes outside the chip range", "[mml][grammar][lint]")
{
	// SN76489 の周期は 10 bit のため、低い音は止まる。OPNA の FM は十分に広い
	const Lint low("#track a chip=sn76489\nO1 C\n");
	REQUIRE(low.warnings().size() == 1);
	CHECK(contains(low.warnings()[0], "2 行目 4 列"));
	CHECK(contains(low.warnings()[0], "O1 C"));
	CHECK(contains(low.warnings()[0], "音域"));
	CHECK(low.errors().empty());

	const Lint fine("#track a chip=sn76489\nO4 C D E\n#track b chip=opna\nO1 C O7 B\n#track c chip=synth\nO1 C\n");
	CHECK(fine.warnings().empty());

	// OPNA の FM の上限は O8 の途中にある
	const Lint high("#track a chip=opm\nO8 B\n#track b chip=opna\nO8 B\n");
	CHECK(!high.warnings().empty());

	// ノイズチャンネルとノイズ専用音源は音程がないため検査しない
	const Lint noise("#track a chip=sn76489 ch=noise\nO1 C\n#track b chip=nes ch=noise\nO1 C\n");
	CHECK(noise.warnings().empty());
}

TEST_CASE("Lint reports octaves, volumes and tempo that the builder clamps", "[mml][grammar][lint]")
{
	const Lint octave("#track a chip=opna\nO9 C O0 C O8 > C O1 < C\n");
	REQUIRE(octave.warnings().size() == 4);
	CHECK(contains(octave.warnings()[0], "2 行目 1 列"));
	CHECK(contains(octave.warnings()[0], "O9"));
	CHECK(contains(octave.warnings()[1], "O0"));
	CHECK(contains(octave.warnings()[2], "オクターブ 8 より上"));
	CHECK(contains(octave.warnings()[3], "オクターブ 1 より下"));

	const Lint volume("#track a chip=opna\nV16 C v20 D V15 E v0 F T400 G T10 A\n");
	REQUIRE(volume.warnings().size() == 4);
	CHECK(contains(volume.warnings()[0], "V16"));
	CHECK(contains(volume.warnings()[0], "飽和"));
	CHECK(contains(volume.warnings()[1], "v20"));
	CHECK(contains(volume.warnings()[2], "T400"));
	CHECK(contains(volume.warnings()[3], "T10"));
}

TEST_CASE("Lint reports loop mistakes", "[mml][grammar][lint]")
{
	const Lint unmatched("#track a chip=opna\nC [D E\nF G\n#track b chip=opna\nA ] B\n");
	REQUIRE(unmatched.errors().size() == 2);
	CHECK(contains(unmatched.errors()[0], "2 行目 3 列"));
	CHECK(contains(unmatched.errors()[0], "[ に対応する ] がありません"));
	CHECK(contains(unmatched.errors()[1], "5 行目 3 列"));
	CHECK(contains(unmatched.errors()[1], "] に対応する [ がありません"));

	const Lint count("#track a chip=opna\n[C]1 [D]0 [E]2\n");
	REQUIRE(count.warnings().size() == 2);
	CHECK(contains(count.warnings()[0], "]1"));
	CHECK(contains(count.warnings()[1], "]0"));

	const Lint point("#track a chip=opna\nC $ D $ E [F $ G]2\n");
	REQUIRE(point.warnings().size() == 3);
	CHECK(contains(point.warnings()[0], "2 つ目"));
	CHECK(contains(point.warnings()[1], "[ ] の中"));
	CHECK(contains(point.warnings()[2], "2 つ目"));
}

TEST_CASE("Lint follows the octave through loop repeats", "[mml][grammar][lint]")
{
	// 1 周目は O3 の C、2 周目は O2 の C で、SN76489 の音域を外れる
	const Lint down("#track a chip=sn76489\nO4 [<C]2\n");
	REQUIRE(down.warnings().size() == 1);
	CHECK(contains(down.warnings()[0], "2 行目 6 列"));
	CHECK(contains(down.warnings()[0], "O2 C"));

	// 3 周目の > でオクターブ 8 を超える
	const Lint up("#track a chip=opm\nO6 [>C]3 C\n");
	REQUIRE(up.warnings().size() == 1);
	CHECK(contains(up.warnings()[0], "オクターブ 8 より上"));

	// 状態が変わらなくなったら、残りの繰り返しは長さだけを掛ける
	const Lint many("#track a chip=opna\nL4 [C [D]1000 E]1000\n");
	REQUIRE(many.result.tracks.size() == 1);
	CHECK(many.result.tracks[0].totalTicks == 1000L * (48 + 1000 * 48 + 48));
}

TEST_CASE("Runaway macros are stopped", "[mml][grammar]")
{
	// 倍々に増えるマクロは上限で止めて報告する
	std::string doubling = "#macro a0 = C\n";
	for (int i = 1; i <= 25; ++i)
	{
		doubling += "#macro a" + std::to_string(i) + " = !a" + std::to_string(i - 1) + " !a" + std::to_string(i - 1) + "\n";
	}
	doubling += "#track t\n!a25\n";
	const SongTextResult big = SongText::parse(doubling);
	CHECK(anyContains(big.errors, "文字を超えました"));
	CHECK(big.tracks[0].mml.size() < 3000000);

	// 入れ子が深すぎるマクロは展開前に報告する
	std::string chain = "#macro m40 = C\n";
	for (int i = 39; i >= 0; --i)
	{
		chain += "#macro m" + std::to_string(i) + " = !m" + std::to_string(i + 1) + "\n";
	}
	chain += "#track t\n!m0\n";
	CHECK(anyContains(SongText::parse(chain).errors, "入れ子が 32 段を超えています"));
}

TEST_CASE("Lint compares loop points across tracks", "[mml][grammar][lint]")
{
	const Lint same("#track a chip=opna\nL4 C $ D E F\n#track b chip=opm\nL4 C $ D E F\n");
	CHECK(same.warnings().empty());
	REQUIRE(same.result.tracks.size() == 2);
	CHECK(same.result.tracks[0].loopTick == 48);
	CHECK(same.result.tracks[0].totalTicks == 192);

	const Lint moved("#track a chip=opna\nL4 C $ D E F\n#track b chip=opm\nL4 C D $ E F\n");
	REQUIRE(moved.warnings().size() == 1);
	CHECK(contains(moved.warnings()[0], "'b'"));
	CHECK(contains(moved.warnings()[0], "96 tick"));
	CHECK(contains(moved.warnings()[0], "48 tick"));

	// 基準にするのは、トラックの並びの先頭ではなく、$ が一番早いトラック
	const Lint earlier("#track a chip=opna\nL4 C D $ E F\n#track b chip=opm\nL4 C $ D E F\n");
	REQUIRE(earlier.warnings().size() == 1);
	CHECK(contains(earlier.warnings()[0], "トラック 'a' の $ は 96 tick で、トラック 'b' の 48 tick と違います"));

	const Lint missing("#track a chip=opna\nL4 C $ D E F\n#track b chip=opm\nL4 C D E F\n");
	REQUIRE(missing.warnings().size() == 1);
	CHECK(contains(missing.warnings()[0], "'b' に $ がありません"));

	const Lint longer("#track a chip=opna\nL4 $ C D E F\n#track b chip=opm\nL4 $ C D E F G\n");
	REQUIRE(longer.warnings().size() == 1);
	CHECK(contains(longer.warnings()[0], "長さ"));

	// $ がない曲はループを検査しない
	const Lint none("#track a chip=opna\nL4 C D\n#track b chip=opm\nL4 C D E F\n");
	CHECK(none.warnings().empty());
}

TEST_CASE("Lint tick counting matches the event builder", "[mml][grammar][lint]")
{
	const std::vector<std::string> samples = {
		"L4 C D E F",
		"L8 [C D]4 R4 E2.",
		"L4 C&C D. {CD}E [F [G A]2]3",
		"O4 L16 [CDEF GAB>C <BAGF EDC R]2 L2 C",
		"L4 C ~ D _E [C]1 [D]0",
		"L8 $ C [D $ E]2 F",
	};
	for (const auto& mml : samples)
	{
		const Lint lint("#track a chip=opna\n" + mml + "\n");
		REQUIRE(lint.result.tracks.size() == 1);
		INFO(mml);
		CHECK(lint.result.tracks[0].totalTicks == lengthByBuilder(mml));
	}

	// ループ点の tick は、ビルダーが作る最初の LoopPoint と一致する
	for (const auto& mml : {std::string("L4 C D $ E F"), std::string("L8 [C D]3 $ E"), std::string("L4 C {CD}E $ F")})
	{
		const Lint lint("#track a chip=opna\n" + mml + "\n");
		long first = -1;
		for (const auto& ev : eventsOf(mml))
		{
			if (ev.type == SeqEventType::LoopPoint)
			{
				first = ev.tick;
				break;
			}
		}
		INFO(mml);
		CHECK(lint.result.tracks[0].loopTick == first);
	}
}

TEST_CASE("Lint reports X commands and chords that the chip drops", "[mml][grammar][lint]")
{
	const Lint dropped("#track a chip=opna\nXcutoff=500 C 'CEG'4\n#track b chip=synth\nXcutoff=500 'CEG'4\n");
	REQUIRE(dropped.warnings().size() == 2);
	CHECK(contains(dropped.warnings()[0], "2 行目 1 列"));
	CHECK(contains(dropped.warnings()[0], "X のコマンド"));
	CHECK(contains(dropped.warnings()[1], "和音"));

	const Lint range("#track a chip=synth\nXcutoff=50000 C Xres=50 C\n");
	REQUIRE(range.warnings().size() == 1);
	CHECK(contains(range.warnings()[0], "Xcutoff の値 50000"));
	CHECK(contains(range.warnings()[0], "20〜20000"));
}

TEST_CASE("Lint points into macros and reports unused definitions", "[mml][grammar][lint]")
{
	const Lint lint(
		"#let unused = 1\n"
		"#macro loud = V20 C\n"
		"#macro idle = D\n"
		"#track a chip=opna\n"
		"E !loud\n");
	const auto warnings = lint.warnings();
	REQUIRE(warnings.size() == 3);
	CHECK(contains(warnings[0], "1 行目 1 列: 変数 'unused' は使われていません"));
	CHECK(contains(warnings[1], "3 行目 1 列: マクロ 'idle' は使われていません"));
	CHECK(contains(warnings[2], "5 行目 3 列"));
	CHECK(contains(warnings[2], "V20"));
	CHECK(contains(warnings[2], "(マクロ 'loud' の中)"));
}

TEST_CASE("Lint includes the reading errors", "[mml][grammar][lint]")
{
	const Lint lint("#track a chip=opna\nC !nosuch\n#track b chip=nowhere\nC\n");
	REQUIRE(lint.errors().size() == 2);
	CHECK(contains(lint.errors()[0], "2 行目 3 列"));
	CHECK(contains(lint.errors()[1], "3 行目"));
	CHECK(lint.result.hasErrors());
}

TEST_CASE("Lint finds no errors in the example songs", "[mml][grammar][lint]")
{
	for (const auto& example : allExamples())
	{
		const Lint lint(example.script);
		INFO(example.name);
		CHECK(lint.errors().empty());
	}
}

TEST_CASE("Example songs in examples/ parse, lint clean and render the same twice", "[mml][grammar][examples]")
{
	for (const char* name : {"macro_chiptune", "variables_synth", "sends_and_macros", "macro_arguments"})
	{
		const std::string text = readFile(std::string(MITIRU_MML_EXAMPLES_DIR) + "/" + name + ".mml");
		INFO(name);
		REQUIRE(!text.empty());

		const SongTextResult song = SongText::parse(text);
		CHECK(song.errors.empty());
		CHECK(!song.definitions.empty());
		for (const auto& m : song.definitions.macros())
		{
			CHECK(m.uses > 0);
		}

		const SongValidation lint = MmlValidator::validateSong(text);
		for (const auto& d : lint.diagnostics)
		{
			FAIL_CHECK(d.text());
		}

		auto parsed = ModernSongScript::parse(text);
		REQUIRE(parsed.ok());
		const auto first = parsed.song.render();
		const auto second = parsed.song.render();
		CHECK(hashPcm16(first.toPcm16(true)) == hashPcm16(second.toPcm16(true)));
		CHECK(peakOfStereo(first.mix) > 0.01);
		CHECK(peakOfStereo(first.mix) <= 0.9999);
	}
}
