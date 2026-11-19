#pragma once
/// @file RealSf2Examples.hpp
/// @brief 本物の GM のサウンドフォント (GeneralUser GS) で鳴らす短い曲。mml_modern_demo の --sf2 と、TestRealSf2.cpp が使う
/// @details SF2 のパスは環境変数 MITIRU_MML_TEST_SF2 かデモの --sf2 で渡す。ファイルはリポジトリに含めない。

#include <cstdlib>
#include <string>

namespace mitiru_mml_test
{

/// @brief 環境変数 MITIRU_MML_TEST_SF2 のパス。無ければ空
[[nodiscard]] inline std::string realSf2PathFromEnvironment()
{
	const char* value = std::getenv("MITIRU_MML_TEST_SF2");
	return value != nullptr ? std::string(value) : std::string();
}

/// @brief GM の代表的な音色 (ピアノ、エレキベース、ストリングス、ブラス、シンセリード) とドラムキットの曲
inline constexpr const char* GM_SHOWCASE = R"(
; 100 BPM。GM のプログラム番号: 0 ピアノ、33 フィンガーベース、48 ストリングス、61 ブラスセクション、81 ノコギリのリード
#tempo 100
#limiter ceiling=-1 release=100
#master gain=9
#tail 2.5
#group drums gain=-1
#group keys gain=0
#duck target=bass by=kit depth=-3 attack=4 release=120

#track kit chip=sampler drum=kit group=drums
O2 L8 [C F+ D F+ C C D F+]8

#track piano chip=sampler voice=0 vol=13 group=keys
O4 L8 [C E G E >C <G E G A >C E <A F A >C <A]4

#track bass chip=sampler voice=33 vol=14 group=bass
O2 L4 [C C G G A A F F]4

#track strings chip=sampler voice=48 vol=10 group=pad
O4 L1 'CEG' 'ACE' 'FAC' 'GBD' 'CEG' 'ACE' 'FAC' 'GBD'

#track brass chip=sampler voice=61 vol=12 group=brass
O4 L4 R1 R1 R1 R1 E G >C2 <B4 G4 E2 D4 E4 G2

#track lead chip=sampler voice=81 vol=13 group=lead
O5 L8 R1 R1 [E G A G E D C D]2 E G >C <G E4 R4
)";

} // namespace mitiru_mml_test
