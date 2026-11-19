#pragma once
/// @file IntegratedExamples.hpp
/// @brief 古いチップと現代的な音源を 1 つのテキストに混ぜた曲の例 3 本。検査のハッシュと、デモの WAV の書き出しに使う
/// @details どれも同じ #track の行の形 (chip=、voice=、pan=、vol=、ch=、group=) と、# の指示 (#tempo、#swing、
///          #tail、#group、#duck、#limiter、#master) を使う。3 本目は SF2 (tests/SyntheticSf2.hpp の合成の SF2) を使う。

#include "ModernExamples.hpp"

#include <vector>

namespace mitiru_mml_test
{

/// @brief 混ぜた曲の一覧
[[nodiscard]] inline const std::vector<ModernExample>& integratedExamples()
{
	static const std::vector<ModernExample> EXAMPLES = {
		{"chip_blend", R"(
; 132 BPM。OPM のリード (左)、SN76489 のアルペジオ (右)、NES の三角波のベース、OPNA の SSG のパッドを、
; ドラムシンセのキックとハイハットと混ぜる。ベースはキックでダッキングする
#tempo 132
#limiter ceiling=-1 release=80
#master gain=0 width=1.1
#tail 1.5
#duck target=bass by=kick depth=-8 attack=3 release=140
#group lead gain=-1
#group bass gain=-2
#group chip gain=-6
#group retro gain=-8

#track kick chip=drums drum=kick group=kick
O4 L4 [CCCC]8

#track hat chip=drums drum=hat group=drums
O4 L8 [RC]32

#track lead chip=opm voice=lead pan=left vol=12 group=lead
O5 L8 [E G A B >C <B A G E G A B >E4 <B4]4

#track bass chip=nes ch=triangle group=bass
O2 L4 V15 [E E G E A A B G]4

#track arp chip=sn76489 pan=right vol=9 group=chip
O4 L16 [E G B >E <B G E G]16

#track pad chip=opna part=ssg vol=8 group=retro
O3 L1 [E A]4
)", false},

		{"retro_club", R"(
; 128 BPM。OPL3 のベースとプラック、AY のコード、OPNA の FM のベルを、シンセの supersaw のパッドと
; ドラムシンセのリズム文法のドラムに重ねる。パッドはドラムの音でサイドチェーンの圧縮を受ける
#tempo 128
#limiter ceiling=-1 release=100
#master gain=-1 width=1.15
#tail 2.0
#duck target=pad by=drums mode=signal threshold=-34 ratio=8 lowpass=170 attack=3 release=160
#group drums gain=1
#group pad gain=-3
#group chips gain=-5

#track beat chip=drums rhythm group=drums
L8 [B H S H B B S H]8

#track bass chip=opl3 voice=bass group=chips
O2 L8 [C C >C< C C C >C< C F F >F< F G G >G< G]4

#track pluck chip=opl3 voice=pluck pan=right vol=10 group=chips
O5 L16 [C E G >C< G E C E F A >C F< C A F A]8

#track line chip=ay8910 voice=tone vol=9 group=chips
O4 L4 [CEG FAC CEG GBD]2 [CEG FAC]1 R2

#track bell chip=opna voice=bell pan=left vol=11 group=chips
O5 L4 R2 G E R2 A F R2 G E R2 B G

#track pad chip=synth voice=supersaw group=pad
O4 Xcutoff=800>6000/1 'CEG'1 'CEG'1 'FAC'1 'FAC'1 'CEG'1 'CEG'1 'GBD'1 'GBD'1
)", false},

		{"pocket_band", R"(
; 96 BPM のスウィング。SF2 のピアノとドラム、OPNA の FM のベース、SN76489 のノイズのシェイカー、
; シンセのリード。ピアノは左、リードは右に置き、グループごとにステムを書き出せる
#tempo 96
#swing 0.6 grid=8
#limiter ceiling=-1.5 release=120
#master gain=-1 width=1.1
#tail 2.0
#duck target=keys,bass by=kick depth=-5 attack=5 release=200
#group keys gain=-3
#group bass gain=2
#group retro gain=-9

#track kick chip=sampler drum=kit group=kick
O2 L4 [C R C R]8

#track snare chip=sampler drum=kit group=drums
O2 L4 [R D R D]8

#track piano chip=sampler voice=0 pan=left vol=12 group=keys
O4 L8 [C E G E >C <G E G F A >C <A F A >C <A]4

#track bass chip=opna voice=bass group=bass
O2 L4 [C G C G F C F C]4

#track shaker chip=sn76489 ch=noise vol=6 group=retro
O4 L16 [R C R C]32

#track lead chip=synth voice=square-lead pan=right vol=10 group=lead
O5 L8 R1 R1 [G4 E4 G4 >C4< B4 G2 A4 F4 A4 >D4< C2.]2
)", true},
	};
	return EXAMPLES;
}

/// @brief modernExamples と integratedExamples を続けた一覧
[[nodiscard]] inline std::vector<ModernExample> allExamples()
{
	std::vector<ModernExample> all = modernExamples();
	const auto& mixed = integratedExamples();
	all.insert(all.end(), mixed.begin(), mixed.end());
	return all;
}

} // namespace mitiru_mml_test
