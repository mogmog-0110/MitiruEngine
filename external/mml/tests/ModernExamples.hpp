#pragma once
/// @file ModernExamples.hpp
/// @brief 現代的な曲の短い例 3 本 (# の指示と MML の文書)。検査のハッシュと、デモの WAV の書き出しに使う
/// @details 3 本目までのうち、2 本目は SF2 (tests/SyntheticSf2.hpp の合成の SF2) を使う。

#include <string>
#include <vector>

namespace mitiru_mml_test
{

/// @brief 例 1 曲
struct ModernExample
{
	const char* name;
	const char* script;
	bool needsSoundFont;
};

/// @brief 例の一覧
[[nodiscard]] inline const std::vector<ModernExample>& modernExamples()
{
	static const std::vector<ModernExample> EXAMPLES = {
		{"neon_drive", R"(
; ハ短調 124 BPM のシンセウェーブ。キックでベースとパッドを下げ、パッドは 1 小節かけてフィルターを開く
#tempo 124
#swing 0.54 grid=8
#limiter ceiling=-1 release=90
#master gain=-1 width=1.15
#tail 2.0
#duck target=bass,pad by=kick depth=-9 attack=3 release=170
#group drums gain=2
#group pad gain=-5
#group lead gain=-1

#track kick chip=drums drum=kick group=kick
O4 L4 [CCCC]8

#track clap chip=drums drum=clap group=drums
O4 L4 [RCRC]8

#track hat chip=drums drum=hat group=drums
O4 L8 [RC]32

#track bass chip=synth voice=saw-bass group=bass
O2 [C8 C8 >C8< C8 C8 >C8< C8 C8]2 [A-8 A-8 >A-8< A-8 A-8 >A-8< A-8 A-8]2
[E-8 E-8 >E-8< E-8 E-8 >E-8< E-8 E-8]2 [B-8 B-8 >B-8< B-8 B-8 >B-8< B-8 B-8]2

#track pad chip=synth voice=supersaw group=pad
O4 Xcutoff=700>7000/1 'CE-G'1 'CE-G'1 'A-CE-'1 'A-CE-'1 'E-GB-'1 'E-GB-'1 'B-DF'1 'B-DF'1

#track arp chip=synth voice=pluck group=lead
O5 Xarp=up Xarprate=16 Xarpgate=55 'CE-G'1 'CE-G'1 'A-CE-'1 'A-CE-'1 'E-GB-'1 'E-GB-'1 'B-DF'1 'B-DF'1

#track lead chip=synth voice=square-lead group=lead
O5 L8 R1 R1 R1 R1 G4 E-4 G4 B-4 A-4. G8 E-4 C2 D4 F4 B-2.. R4
)", false},

		{"rainy_lofi", R"(
; イ短調 84 BPM。ゆるく跳ねるスウィング。ドラムと鍵盤とベースは SF2 のサンプル、リードだけシンセ
#tempo 84
#swing 0.62 grid=8
#limiter ceiling=-1.5 release=120
#master gain=-1
#tail 2.5
#duck target=keys,bass by=kick depth=-6 attack=5 release=220
#group bass gain=6
#group keys gain=-4

#track kick chip=sampler drum=kit group=kick
O2 L8 [C R R R R C R R]8

#track snare chip=sampler drum=kit group=drums
O2 L8 [R R R R D R R R]8

#track hat chip=sampler drum=kit group=drums
O2 L8 [F+ F+ F+ F+]16

#track bass chip=sampler voice=33 group=bass
O2 L4 A A E A F F C F G G D G C C G C

#track keys chip=sampler voice=0 group=keys
Xpan=40 O4 [ 'ACEG'1 'DFAC'1 'GBDF'1 'CEGB'1 ]2

#track lead chip=synth voice=tri-lead group=lead
Xpan=92 O5 L8 R1 R1 E4 G4 A4 G8 E8 D4 F4 A2 G4 B4 D4 C2.
)", true},

		{"overclock", R"(
; 174 BPM のドラムンベース。うねるベース、1 小節で開くノイズのライザー、ドラムの音で下がるパッド
#tempo 174
#limiter ceiling=-0.5 release=60
#master gain=0 width=1.2
#tail 1.5
#duck target=pad by=drums mode=signal threshold=-34 ratio=10 lowpass=160 attack=2 release=140
#group pad gain=4

#track beat chip=drums rhythm group=drums
L8 [B H S H H B S H]8

#track hat chip=drums drum=hat group=drums
O4 L16 [R R C R]32

#track wobble chip=synth voice=wobble group=bass
O1 Xlfor=58 L2 [C C E- F C C B- G]4

#track riser chip=synth voice=riser group=fx
R1 R1 R1 R1 R1 R1 Xres=30>75/1 Xcutoff=300>3000/1 O4 C1 Xcutoff=3000>12000/1 C1

#track pad chip=synth voice=supersaw group=pad
Xarp=updown Xarprate=16 Xarpgate=70 Xarpoct=2 O4 'CE-G'1 'CE-G'1 'A-CE-'1 'A-CE-'1 'CE-G'1 'CE-G'1 'B-DF'1 'B-DF'1
)", false},
	};
	return EXAMPLES;
}

} // namespace mitiru_mml_test
