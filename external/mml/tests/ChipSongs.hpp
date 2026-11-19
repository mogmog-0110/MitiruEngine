#pragma once
/// @file ChipSongs.hpp
/// @brief チップごとの検査用の曲 (出力ハッシュの照合と、MML の書き方の例を兼ねる)
/// @details 各曲は 3 トラック前後で 3〜5 秒。チップごとに 1 曲。

#include <mitiru_mml/ChipDriver.hpp>
#include <mitiru_mml/MultiChipSequencer.hpp>

#include "Sha256.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mitiru_mml_test
{

/// @brief 曲のトラック 1 本 (Driver のチャンネル番号、音色番号、MML)
struct SongTrackDef
{
	int channel;
	int voice;
	const char* mml;
};

/// @brief OPM の曲: lead、bass、strings、bell の 4 声
inline constexpr SongTrackDef OPM_SONG[] = {
	{0, 4, "T140 O5 L8 V13 E G A B >C4 <B A G E4 R4 [EG]2 A4 B4 >C2"},
	{1, 2, "O2 V12 L4 [EEGE]2 [AABB]2"},
	{2, 3, "O4 V10 L1 E A"},
	{3, 5, "O6 V9 L8 R4 E G B >E4 <B G E4 R2"},
};

/// @brief OPL 系 (YM3526、YM3812、YMF262) の曲: lead、bass、strings、pluck の 4 声
inline constexpr SongTrackDef OPL_SONG[] = {
	{0, 4, "T132 O4 L8 V13 C E G >C4 <G E C4 R4 [CE]2 G4 A4 >C2"},
	{1, 2, "O2 V12 L4 [CCGC]2 [FFGG]2"},
	{2, 3, "O3 V9 L1 C F"},
	{3, 7, "O5 V10 L16 [CEG>C<GEC]4 R2"},
};

/// @brief OPL3 だけが足す 5 本目 (チャンネル 9 は後半のバンクの先頭)
inline constexpr SongTrackDef OPL3_EXTRA_TRACK = {9, 5, "O6 V8 L8 R4 E G >C4 <G E C4 R2"};

/// @brief SN76489 の曲: 矩形波 3 声とノイズ (ノイズのチャンネルは 3)
inline constexpr SongTrackDef SN76489_SONG[] = {
	{0, 0, "T150 O4 L8 V13 E G A B >C4 <B A G E4 R4 [EG]2 A4 B4 >C2"},
	{1, 0, "O3 V12 L4 [EEGE]2 [AABB]2"},
	{2, 0, "O4 V8 L2 E A E A"},
	{3, 1, "O4 V10 L16 [CRRR]16"},
};

/// @brief AY-3-8910 の曲: 矩形波 2 声とノイズ 1 声
inline constexpr SongTrackDef AY8910_SONG[] = {
	{0, 0, "T150 O4 L8 V13 E G A B >C4 <B A G E4 R4 [EG]2 A4 B4 >C2"},
	{1, 0, "O3 V11 L4 [EEGE]2 [AABB]2"},
	{2, 1, "O4 V10 L16 [CRRR]16"},
};

/// @brief NES の曲: 矩形波 2 声 (デューティ比を変える)、三角波、ノイズ
inline constexpr SongTrackDef NES_SONG[] = {
	{0, 0, "T150 O4 L8 V13 E G A B >C4 <B A G E4 R4 [EG]2 A4 B4 >C2"},
	{1, 1, "O4 V9 L8 R4 G B >E4 <B G E4 R2 R2"},
	{2, 0, "O3 V15 L4 [EEGE]2 [AABB]2"},
	{3, 1, "O4 V10 L16 [CRRR]16"},
};

/// @brief 描いた結果のハッシュとフレーム数
struct RenderedHash
{
	std::size_t frames = 0;
	std::string sha256;
};

/// @brief シーケンサーを描き、16bit ステレオにしてハッシュを取る
[[nodiscard]] inline RenderedHash hashRender(mitiru_mml::MultiChipSequencer& seq, int loopCount = 1)
{
	const mitiru_mml::StereoBuffer stereo = seq.render(loopCount);
	RenderedHash out;
	out.frames = stereo.frames();
	out.sha256 = Sha256::ofPcm(stereo.toPcm16Stereo());
	return out;
}

} // namespace mitiru_mml_test
