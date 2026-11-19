/// @file TestMacroArgs.cpp
/// @brief マクロ引数と整数式の検査

#include <catch2/catch_test_macros.hpp>

#include "Analysis.hpp"

#include <mitiru_mml/MitiruMML.hpp>

#include <string>
#include <vector>

using namespace mitiru_mml;
using namespace mitiru_mml_test;

namespace
{

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

/// @brief 1 トラックの曲を読み、展開後の MML を返す。読み取りエラーは検査を失敗にする
std::string expandOne(const std::string& definitions, const std::string& mml)
{
	const SongTextResult song = SongText::parse(definitions + "#track t\n" + mml + "\n");
	INFO(definitions << mml);
	for (const auto& error : song.errors)
	{
		FAIL_CHECK(error);
	}
	REQUIRE(song.tracks.size() == 1);
	return song.tracks[0].mml;
}

std::vector<std::string> errorsOf(const std::string& text)
{
	return SongText::parse(text).errors;
}

struct Lint
{
	SongValidation result;
	explicit Lint(const std::string& text) : result(MmlValidator::validateSong(text)) {}
	std::vector<std::string> warnings() const { return result.texts(DiagnosticSeverity::Warning); }
	std::vector<std::string> errors() const { return result.texts(DiagnosticSeverity::Error); }
};

} // namespace

TEST_CASE("Macro arguments replace %name in the body", "[mml][grammar][args]")
{
	CHECK(expandOne("#macro riff(o, len) = O%o L%len CDEG\n", "!riff(4, 8) !riff(5,16)") == "O4 L8 CDEG O5 L16 CDEG");
	CHECK(expandOne("#macro twice(x) = %x %x\n", "!twice(CDE) !twice(O4 [CD]2)") == "CDE CDE O4 [CD]2 O4 [CD]2");
	// 括弧の中のコンマは、引数を区切らない
	CHECK(expandOne("#macro one(x, y) = <%x|%y>\n", "!one([C, D]2, E)") == "<[C, D]2|E>");
	CHECK(expandOne("#macro one(x, y) = <%x|%y>\n", "!one((a,b), {c,d})") == "<(a,b)|{c,d}>");
	// 種類が合わない閉じ括弧は、引数の区切りや呼び出しの終わりとして扱わない
	CHECK(expandOne("#macro one(x, y) = <%x|%y>\n", "!one([C)D], E)") == "<[C)D]|E>");
	CHECK(anyContains(errorsOf("#macro one(x, y) = <%x|%y>\n#track t\n!one([C), E)\n"), "呼び出しに ) がありません"));
	CHECK(expandOne("#macro two(x, y) = [%x][%y]\n", "!two(, C)") == "[][C]");
	// 名前の直後が英数字でも、!{名前} なら引数を渡せる
	CHECK(expandOne("#macro k4(x) = [%x]\n", "!{k4}(C)8") == "[C]8");
	CHECK(expandOne("#macro bar(o)\nO%o C D ; comment\nE F\n#endmacro\n", "!bar(3)") == "O3 C D E F");
}

TEST_CASE("Arguments are expanded where the macro is called", "[mml][grammar][args]")
{
	const std::string defs =
		"#let base = 4\n"
		"#macro riff(o, len) = O%o L%len C\n"
		"#macro call(n) = !riff(%n, 8) !riff(%n+1, 16)\n"
		"#macro wrap(x) = [%x]2\n";
	// 式は呼んだ側の変数で計算する
	CHECK(expandOne(defs, "!riff(%base+1, %base*2)") == "O5 L8 C");
	CHECK(expandOne(defs, "!call(3)") == "O3 L8 C O4 L16 C");
	CHECK(expandOne(defs, "!wrap(!riff(2, 4))") == "[O2 L4 C]2");
	CHECK(expandOne(defs, "!wrap(!wrap(C))") == "[[C]2]2");
	// 引数は同名の変数より優先し、マクロ本体の外には影響しない
	CHECK(expandOne("#let o = 9\n#macro m(o) = O%o\n", "!m(2) O%o") == "O2 O9");
	// 引数のないマクロでは、呼び出し直後の ( を MML として残す
	CHECK(expandOne("#macro k = C\n", "!k(1)") == "C(1)");
	// 本体の %名前 が引数でなければ、変数として扱う
	CHECK(expandOne("#let v = 12\n#macro m(a) = V%v %a\n", "!m(C)") == "V12 C");
}

TEST_CASE("Integer expressions in MML and in directive lines", "[mml][grammar][args]")
{
	const std::string defs = "#let a = 3\n#let b = 4\n";
	CHECK(expandOne(defs, "V%a+1 O%{a*b-2} L%a*(%b+1) T%{(a+1)*2}") == "V4 O10 L15 T8");
	CHECK(expandOne(defs, "V%a-5 V%b*%a*2 V%a+%b*2") == "V-2 V24 V11");
	CHECK(expandOne(defs, "V%{ a + 1 } V%{-a} V%{a - -1} V%{ (a+b) * 2 }") == "V4 V-3 V4 V14");
	CHECK(expandOne(defs, "V%{a}+1 V%{a}*%b") == "V4 V12");
	// %名前から離れた演算子や、右に項のない演算子は式に含めない
	CHECK(expandOne(defs, "C%a +1 C%a+ C%a-C C%a*") == "C3 +1 C3+ C3-C C3*");
	CHECK(expandOne(defs, "O%a+C") == "O3+C");

	auto parsed = ModernSongScript::parse(
		"#let bpm = 60\n#let tail = 2\n#tempo %bpm*2+8\n#tail %{tail+1}\n#track a chip=synth vol=%bpm-50\nC\n");
	REQUIRE(parsed.ok());
	CHECK(parsed.song.options().tempo == 128);
	CHECK(parsed.song.options().tailSeconds == 3.0);
	const SongTextResult song = SongText::parse("#let x = 12\n#track a chip=synth vol=%x-2 ch=%x-12\nC\n");
	REQUIRE(song.errors.empty());
	CHECK(song.tracks[0].volume == 10);
	CHECK(song.tracks[0].channel == 0);
}

TEST_CASE("Arguments can be used in expressions when they are integers", "[mml][grammar][args]")
{
	CHECK(expandOne("#macro up(n) = O%n+1 L%n*2 C\n", "!up(3) !up(-1) !up(%{1+1})") == "O4 L6 C O0 L-2 C O3 L4 C");
	CHECK(expandOne("#macro both(a, b) = V%a+%b\n", "!both(3, 4)") == "V7");
	CHECK(expandOne("#let k = 2\n#macro scaled(a) = V%{a*k}\n", "!scaled(5)") == "V10");
	// 引数の式は呼んだ側で計算する
	CHECK(expandOne("#let k = 2\n#macro id(a) = %a\n", "!id(%k+3)") == "5");
}

TEST_CASE("Argument and expression mistakes are reported where they are written", "[mml][grammar][args]")
{
	const std::string defs = "#macro riff(o, len) = O%o L%len\n";

	const auto few = errorsOf(defs + "#track t\nC !riff(4) D\n");
	REQUIRE(few.size() == 1);
	CHECK(contains(few[0], "3 行目 3 列"));
	CHECK(contains(few[0], "引数が 2 つ要りますが、1 つ渡されています"));

	const auto many = errorsOf(defs + "#track t\n!riff(1,2,3)\n");
	REQUIRE(many.size() == 1);
	CHECK(contains(many[0], "3 つ渡されています"));

	const auto bare = errorsOf(defs + "#track t\nC !riff D\n");
	REQUIRE(bare.size() == 1);
	CHECK(contains(bare[0], "3 行目 3 列"));
	CHECK(contains(bare[0], "引数が 2 つ要ります"));

	const auto open = errorsOf(defs + "#track t\n!riff(1, 2\n");
	REQUIRE(open.size() == 1);
	CHECK(contains(open[0], "呼び出しに ) がありません"));

	const auto undefinedName = errorsOf("#track t\nV%{nosuch+1} V%nope+1\n");
	REQUIRE(undefinedName.size() == 2);
	CHECK(contains(undefinedName[0], "2 行目 2 列"));
	CHECK(contains(undefinedName[0], "'nosuch' は定義されていません"));
	CHECK(contains(undefinedName[1], "2 行目 15 列"));
	CHECK(contains(undefinedName[1], "'nope' は定義されていません"));
	CHECK(anyContains(errorsOf("#let a = 1\n#track t\nV%{a+}\n"), "式が途中で終わっています"));
	CHECK(anyContains(errorsOf("#let a = 1\n#track t\nV%{a/2}\n"), "'/' は式の中では使えません"));
	CHECK(anyContains(errorsOf("#let a = 1\n#track t\nV%{(a+1}\n"), "( に対応する ) がありません"));
	CHECK(anyContains(errorsOf("#let a = 99999\n#track t\nV%a*%a*%a\n"), "式の値が大きすぎます"));
	CHECK(anyContains(errorsOf("#track t\nV%{99999999999}\n"), "数が大きすぎます"));

	// 整数でない引数を式に使ったエラーは、呼び出した位置にマクロ名を付けて出す
	const auto notInteger = errorsOf("#macro up(n) = O%n+1\n#track t\nC !up(CDE)\n");
	REQUIRE(notInteger.size() == 1);
	CHECK(contains(notInteger[0], "3 行目 3 列"));
	CHECK(contains(notInteger[0], "引数 'n' の値 'CDE' は整数ではないので、式には使えません"));
	CHECK(contains(notInteger[0], "(マクロ 'up' の中)"));

	// 同じ呼び出しで同じ問題が起きても、エラーは 1 つにまとめる
	const auto twice = errorsOf("#macro up(n) = O%n+1 L%n*2\n#track t\n!up(CDE)\n");
	CHECK(twice.size() == 2);
	const auto same = errorsOf("#macro up(n) = O%n+1 O%n+1\n#track t\n!up(CDE)\n");
	CHECK(same.size() == 1);

	CHECK(anyContains(errorsOf("#macro m(a,a) = C\n"), "引数 'a' が 2 回書かれています"));
	CHECK(anyContains(errorsOf("#macro m(1x) = C\n"), "引数の名前は"));
	CHECK(anyContains(errorsOf("#macro m() = C\n"), "引数の名前は"));
	CHECK(anyContains(errorsOf("#macro m(a = C\n"), "( に対応する ) がありません"));
	CHECK(anyContains(errorsOf("#macro m(a,) = C\n"), "引数の名前は"));
	std::string many17 = "#macro m(";
	for (int i = 0; i < 17; ++i)
	{
		many17 += std::string(i == 0 ? "" : ",") + "a" + std::to_string(i);
	}
	CHECK(anyContains(errorsOf(many17 + ") = C\n"), "16 個まで"));
}

TEST_CASE("Mistakes inside a macro body are reported once, where the body is written", "[mml][grammar][args]")
{
	const auto undefinedInBody = errorsOf("#macro bad(a) = V%a+%zz\n#track t\n!bad(1)\n!bad(2)\n");
	REQUIRE(undefinedInBody.size() == 1);
	CHECK(contains(undefinedInBody[0], "1 行目 "));
	CHECK(contains(undefinedInBody[0], "'zz' は定義されていません"));
	CHECK(contains(undefinedInBody[0], "(マクロ 'bad' の本体)"));

	// 本体内の引数の数の違いは、本体の位置に出す
	const auto arity = errorsOf("#macro inner(a, b) = %a%b\n#macro outer(x) = !inner(%x)\n#track t\n!outer(1)\n");
	REQUIRE(arity.size() == 1);
	CHECK(contains(arity[0], "2 行目 "));
	CHECK(contains(arity[0], "引数が 2 つ要りますが、1 つ渡されています"));
	CHECK(contains(arity[0], "(マクロ 'outer' の本体)"));

	CHECK(anyContains(errorsOf("#macro a(x) = !b(%x)\n#macro b(x) = !a(%x)\n#track t\n!a(1)\n"), "自分を呼んでいます"));
	CHECK(anyContains(errorsOf("#macro a(x) = !a(%x)\n"), "自分を呼んでいます"));

	// 倍々に増える引数は上限で止める
	std::string doubling = "#macro d(x) = %x %x\n#track t\n";
	std::string call = "C";
	for (int i = 0; i < 25; ++i)
	{
		call = "!d(" + call + ")";
	}
	const SongTextResult big = SongText::parse(doubling + call + "\n");
	CHECK(anyContains(big.errors, "文字を超えました"));
	CHECK(big.tracks[0].mml.size() < 3000000);
}

TEST_CASE("Lint reports problems inside expansions with arguments at the call", "[mml][grammar][lint][args]")
{
	const Lint lint(
		"#macro loud(v) = V%v C\n"
		"#macro wrap(x) = !loud(%x)\n"
		"#macro play(x) = %x\n"
		"#track a chip=opna\n"
		"E !loud(20)\n"
		"F !wrap(30)\n"
		"G !play(V30 C)\n"
		"A !loud(5)\n");
	const auto warnings = lint.warnings();
	REQUIRE(warnings.size() == 3);
	CHECK(contains(warnings[0], "5 行目 3 列"));
	CHECK(contains(warnings[0], "V20"));
	CHECK(contains(warnings[0], "(マクロ 'loud' の中)"));
	// 入れ子の呼び出しは、一番外側の呼び出しの位置を指す
	CHECK(contains(warnings[1], "6 行目 3 列"));
	CHECK(contains(warnings[1], "V30"));
	CHECK(contains(warnings[1], "(マクロ 'loud' の中)"));
	CHECK(contains(warnings[2], "7 行目 3 列"));
	CHECK(contains(warnings[2], "V30"));
	CHECK(contains(warnings[2], "(マクロ 'play' の中)"));
	CHECK(lint.errors().empty());

	// 引数の数の違いと、整数でない引数を使った式は、呼び出した位置の Error にする
	const Lint wrong(
		"#macro loud(v) = V%v C\n"
		"#macro up(n) = O%n+1 C\n"
		"#track a chip=opna\n"
		"E !loud(1, 2)\n"
		"F !up(CDE)\n");
	const auto errors = wrong.errors();
	REQUIRE(errors.size() == 2);
	CHECK(contains(errors[0], "4 行目 3 列"));
	CHECK(contains(errors[0], "引数が 1 つ要りますが、2 つ渡されています"));
	CHECK(contains(errors[1], "5 行目 3 列"));
	CHECK(contains(errors[1], "(マクロ 'up' の中)"));
	CHECK(wrong.result.hasErrors());

	// 式で計算した値の問題は、式を書いた位置に出す。マクロ内の式なら呼び出した位置に出す
	const Lint computed(
		"#let base = 14\n"
		"#macro shift(n) = V%base+%n\n"
		"#track a chip=opna\n"
		"C V%base+3 !shift(2)\n");
	const auto computedWarnings = computed.warnings();
	REQUIRE(computedWarnings.size() == 2);
	CHECK(contains(computedWarnings[0], "4 行目 3 列"));
	CHECK(contains(computedWarnings[0], "V17"));
	CHECK(contains(computedWarnings[1], "4 行目 12 列"));
	CHECK(contains(computedWarnings[1], "V16"));
	CHECK(contains(computedWarnings[1], "(マクロ 'shift' の中)"));
}

TEST_CASE("A song with arguments renders the same PCM as the hand-expanded text", "[mml][grammar][args][hash]")
{
	const std::string withArgs =
		"#let bpm = 120\n"
		"#let lead = 12\n"
		"#macro riff(o, len) = O%o L%len CDEG\n"
		"#macro bar(o) = [!riff(%o, 8)]2\n"
		"#tempo %bpm\n"
		"#limiter off\n"
		"#tail 0.4\n"
		"#track a chip=opna voice=piano vol=%lead-2 send=reverb:30\n"
		"!bar(4) R4 !riff(%lead-8, 4)\n"
		"#track b chip=synth voice=pad group=pad vol=%lead-4\n"
		"O3 'CEG'1 !riff(%{2*2}, 1)\n";
	const std::string byHand =
		"#tempo 120\n"
		"#limiter off\n"
		"#tail 0.4\n"
		"#track a chip=opna voice=piano vol=10 send=reverb:30\n"
		"[O4 L8 CDEG]2 R4 O4 L4 CDEG\n"
		"#track b chip=synth voice=pad group=pad vol=8\n"
		"O3 'CEG'1 O4 L1 CDEG\n";
	auto macro = ModernSongScript::parse(withArgs);
	auto plain = ModernSongScript::parse(byHand);
	REQUIRE(macro.ok());
	REQUIRE(plain.ok());
	const std::string macroHash = hashPcm16(macro.song.render().toPcm16(true));
	CHECK(macroHash == hashPcm16(plain.song.render().toPcm16(true)));
	CHECK(!macroHash.empty());
}
