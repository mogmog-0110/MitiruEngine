#pragma once
/// @file BaselineSongs.hpp
/// @brief レンダリングの速度測定と、出力 PCM の回帰検査に使う曲の一覧
/// @details 最適化やリファクタリングの前後で同じ曲を描き、PCM の SHA-256 と時間を比べる。
///          曲を足すときは BaselineHashes.hpp の表も作り直す (mml_bench --emit-table)。

#include <mitiru_mml/OpnaSequencer.hpp>

#include <string>
#include <vector>

namespace mitiru_mml_test
{

/// @brief 曲を構成するトラック 1 本
struct SongTrack
{
	enum class Kind
	{
		Fm,
		Ssg,
		Rhythm
	};

	Kind kind = Kind::Fm;
	std::string mml;
	int preset = 0; ///< FM 音色プリセット番号 (Fm のときだけ使う)
};

/// @brief 検査用の曲
struct BaselineSong
{
	std::string name;
	std::vector<SongTrack> tracks;
	int loopCount = 1;
};

/// @brief 文字列を n 回つなげる
[[nodiscard]] inline std::string repeatText(const std::string& text, int n)
{
	std::string out;
	for (int i = 0; i < n; ++i)
	{
		out += text;
		out += ' ';
	}
	return out;
}

/// @brief 曲の定義から OpnaSequencer を作る
[[nodiscard]] inline mitiru_mml::OpnaSequencer buildSequencer(const BaselineSong& song)
{
	mitiru_mml::OpnaSequencer seq;
	for (const auto& t : song.tracks)
	{
		switch (t.kind)
		{
		case SongTrack::Kind::Fm: seq.addFmTrack(t.mml, t.preset); break;
		case SongTrack::Kind::Ssg: seq.addSsgTrack(t.mml); break;
		case SongTrack::Kind::Rhythm: seq.addRhythmTrack(t.mml); break;
		}
	}
	return seq;
}

/// @brief 5 トラック (FM 3 本 + SSG + リズム) の 8 小節ぶんを 1 ブロックとして作る
/// @details Am - Dm - E7 - Am を 2 小節ずつ。リファレンスの 5 トラック構成のテンプレートに沿う。
[[nodiscard]] inline std::vector<SongTrack> fiveTrackBlock(int tempo, int repeats)
{
	const std::string t = "T" + std::to_string(tempo) + " ";

	const std::string melody = t + "V13 O5 L8 "
		+ repeatText("E4 C D E4 G4 A2 R4 R4  F4 D E F4 A4 >C2< R4 R4  G+4 E F+ G+4 B4 >D2< R4 R4  A4 G E D4 C4 A1", repeats);
	const std::string harmony = t + "V9 O4 L1 "
		+ repeatText("C E  D F  E G+  C E", repeats);
	const std::string bass = t + "V11 O3 L8 "
		+ repeatText("A4E4A4E4A4E4A4E4 D4A4D4A4D4A4D4A4 E4B4E4B4E4B4E4B4 A4E4A4E4A4E4A4E4", repeats);
	const std::string arp = t + "V7 O5 L16 "
		+ repeatText("[C E A E]8 [D F A F]8 [E G+ B G+]8 [C E A E]8", repeats);
	const std::string drums = t + "L8 "
		+ repeatText(repeatText("BHSHBHSH", 8), repeats);

	return {
		{SongTrack::Kind::Fm, melody, 2},
		{SongTrack::Kind::Fm, harmony, 3},
		{SongTrack::Kind::Fm, bass, 6},
		{SongTrack::Kind::Ssg, arp, 0},
		{SongTrack::Kind::Rhythm, drums, 0},
	};
}

/// @brief 検査用の曲の一覧を返す
[[nodiscard]] inline std::vector<BaselineSong> baselineSongs()
{
	using Kind = SongTrack::Kind;
	std::vector<BaselineSong> songs;

	songs.push_back({"fm_single",
		{{Kind::Fm, "T120 @FM0 O4 L4 CDEF", 0}}, 1});

	songs.push_back({"fm_ssg_mixed",
		{{Kind::Fm, "T120 @FM0 O4 L4 CDEF", 0},
		 {Kind::Ssg, "T120 O5 L8 CDEFGAB>C", 0}}, 1});

	songs.push_back({"ref_am_three_track",
		{{Kind::Fm, "T100 V13 O5 L4 E4 C D E G A2 R4  F D E F A >C2< R4  G+ E F+ G+ B >D2< R4  A G E D C A1", 2},
		 {Kind::Fm, "T100 V11 O3 L8 A4E4A4E4A4E4A4E4 D4A4D4A4D4A4D4A4 E4B4E4B4E4B4E4B4 A4E4A4E4A4E4A4E4", 6},
		 {Kind::Rhythm, "T100 L8 " + repeatText("BHSHBHSH", 16), 0}}, 1});

	{
		BaselineSong s{"five_track_16bar", fiveTrackBlock(140, 2), 1};
		songs.push_back(std::move(s));
	}

	songs.push_back({"technique_mix",
		{{Kind::Fm, "T150 O4 L4 V12 C&D E _F G {AB}>C< P0 D P2 E P1 (C D E F) R4 [CE]3 $ G A B T90 G T180 A B", 1},
		 {Kind::Ssg, "T150 O5 V10 L8 SE9,500 CDEFGAB>C V6 L16 CDEFGAB>C R2 $ CEG>C", 0},
		 {Kind::Fm, "T150 O3 L4 V9 C E G E ~C ~E G+ E C&C R4 [C E G]4", 5},
		 {Kind::Rhythm, "T150 L8 BHSH BHSY BHSH BIFH BR SR B4", 0}}, 2});

	songs.push_back({"fm_overflow_seven_tracks",
		{{Kind::Fm, "T120 O4 L4 V12 CEGC", 0},
		 {Kind::Fm, "T120 O3 L4 V12 CEGC", 1},
		 {Kind::Fm, "T120 O5 L4 V12 CEGC", 2},
		 {Kind::Fm, "T120 O4 L8 V12 CEGCEGCE", 3},
		 {Kind::Fm, "T120 O3 L8 V12 GECGECGE", 4},
		 {Kind::Fm, "T120 O5 L8 V12 GECGECGE", 5},
		 {Kind::Fm, "T120 O2 L4 V12 CGCG", 6}}, 1});

	{
		BaselineSong s{"long_five_track", fiveTrackBlock(132, 6), 1};
		songs.push_back(std::move(s));
	}

	return songs;
}

} // namespace mitiru_mml_test
