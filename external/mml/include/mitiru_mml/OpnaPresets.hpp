#pragma once
/// @file OpnaPresets.hpp
/// @brief YM2608 (OPNA) FM音色プリセット集
/// @details YU-NO（PC-98版）VGMデータから抽出した実FM音色を12種類定義する。
///          各プリセットは4オペレータ分のDT/MUL/TL/KS/AR/DR/SR/SL/RRを含む。
///          データソース: YU-NO ~この世の果てで恋を唄う少女~ (elf, 1996) FM音源レジスタ値
///
/// TL (Total Level): 0=最大音量, 127=無音
///   キャリア: 10-29が適正（YU-NOの実データに基づく）
///   モジュレータ: 20-50が適正（倍音の変調深度を決定）
/// FB (Feedback): 0-7（YU-NOではFB=4およびFB=7が多用される）
/// DT (Detune): 3と7の対称使用でコーラス/デチューン効果を実現

#include <mitiru_mml/OpnaDriver.hpp>

namespace mitiru_mml
{

/// @brief OPNA FM音色プリセット名前空間
namespace opna_presets
{

/// @brief ピアノ — YU-NO "Memories" Ch0 (ALG=4, FB=4) — メインメロディ音色
inline constexpr OpnaDriver::FmVoice PIANO =
{
	4, 4,
	{
		{0, 1, 28, 0, 31,  0, 0, 0,  0},  // OP1
		{0, 1, 23, 0, 31,  0, 0, 0,  0},  // OP2
		{0, 1, 10, 0, 31, 13, 8, 2, 10},  // OP3
		{0, 1, 10, 0, 31, 13, 8, 2, 10},  // OP4
	}
};

/// @brief ベル — YU-NO "Memories" Ch1 (ALG=4, FB=6) — ベル/キラキラ音色
inline constexpr OpnaDriver::FmVoice BELL =
{
	4, 6,
	{
		{3, 3, 35, 1, 27,  4, 0, 15, 6},  // OP1
		{7,14, 41, 1, 31,  7, 0, 14, 4},  // OP2
		{4, 1, 42, 2, 31,  7, 0, 15, 7},  // OP3
		{7, 4, 49, 0, 31,  8, 0, 15, 7},  // OP4
	}
};

/// @brief ブラス — YU-NO "Movement 1" Ch0 (ALG=4, FB=7) — パワフル音色
inline constexpr OpnaDriver::FmVoice BRASS =
{
	4, 7,
	{
		{3, 14, 30, 0, 31, 14, 0, 2,  0},  // OP1
		{7,  2, 50, 0, 31,  0, 0, 0,  0},  // OP2
		{7,  2, 24, 3, 25, 14, 6, 1,  7},  // OP3
		{3,  2, 24, 1, 31,  0, 6, 0,  7},  // OP4
	}
};

/// @brief ストリングス — YU-NO "Girl" Ch0 (ALG=4, FB=0) — 柔らかい持続音
inline constexpr OpnaDriver::FmVoice STRINGS =
{
	4, 0,
	{
		{3, 2, 20, 0,  8,  6, 0, 2,  0},  // OP1
		{7, 3, 20, 0,  8,  6, 5, 2,  0},  // OP2
		{3, 4, 18, 0,  6,  0, 6, 1, 10},  // OP3
		{7, 6, 18, 0,  6,  0, 5, 1, 10},  // OP4
	}
};

/// @brief オルガン — YU-NO "Prologue" Ch0 (ALG=4, FB=7) — 厚みのあるパッド
inline constexpr OpnaDriver::FmVoice ORGAN =
{
	4, 7,
	{
		{0, 4, 27, 0, 31,  0, 0, 0,  0},  // OP1
		{0, 6, 35, 0, 31,  0, 0, 0,  0},  // OP2
		{0, 8, 29, 0,  7,  0, 4, 0, 12},  // OP3
		{0,12, 29, 0,  7,  0, 4, 0, 12},  // OP4
	}
};

/// @brief エレクトリックピアノ — YU-NO "Reminiscence" Ch2 (ALG=4, FB=0) — 温かい音
inline constexpr OpnaDriver::FmVoice E_PIANO =
{
	4, 0,
	{
		{7, 0, 10, 1, 31, 31, 0, 1,  0},  // OP1
		{3, 0, 10, 1, 31, 31, 0, 1,  0},  // OP2
		{7, 1, 24, 0,  6,  5, 4, 1, 10},  // OP3
		{3, 1, 24, 0,  6,  5, 4, 1, 10},  // OP4
	}
};

/// @brief ベース — YU-NO "Movement 1" Ch3 (ALG=4, FB=4) — 低音ベース
inline constexpr OpnaDriver::FmVoice BASS =
{
	4, 4,
	{
		{0, 1, 28, 0, 31,  0, 0, 0,  0},  // OP1
		{0, 1, 23, 0, 31,  0, 0, 0,  0},  // OP2
		{0, 1, 13, 0, 31, 13, 8, 2, 10},  // OP3
		{0, 1, 13, 0, 31, 13, 8, 2, 10},  // OP4
	}
};

/// @brief フルート — YU-NO "Girl" Ch1 (ALG=4, FB=0) — 澄んだ音
inline constexpr OpnaDriver::FmVoice FLUTE =
{
	4, 0,
	{
		{7,14, 23, 0, 31, 25, 10, 3,  0},  // OP1
		{3,14, 23, 0, 31, 25, 10, 3,  0},  // OP2
		{7, 2, 22, 0, 31, 15,  6, 1,  9},  // OP3
		{3, 2, 22, 0, 31, 15,  6, 1,  9},  // OP4
	}
};

/// @brief 琴/ハープ — YU-NO "Movement 1" Ch1 (ALG=3, FB=4) — 撥弦楽器
inline constexpr OpnaDriver::FmVoice KOTO =
{
	3, 4,
	{
		{3, 6, 32, 0, 31,  7, 7, 2,  9},  // OP1
		{4, 0, 24, 2, 31,  9, 6, 1,  9},  // OP2
		{7, 5, 48, 2, 31, 10, 6, 3,  9},  // OP3
		{7, 1, 23, 0, 31, 15, 4, 1,  8},  // OP4
	}
};

/// @brief シンセリード — YU-NO "Prologue" Ch2 (ALG=6, FB=7) — リード音
inline constexpr OpnaDriver::FmVoice SYNTH_LEAD =
{
	6, 7,
	{
		{7, 4, 27, 0, 31,  0, 0, 0,  0},  // OP1
		{7, 4, 24, 0, 31, 10, 5, 2, 10},  // OP2
		{3, 8, 26, 0, 31, 10, 5, 2, 10},  // OP3
		{3, 2, 24, 0, 31, 10, 5, 2, 10},  // OP4
	}
};

/// @brief ビブラフォン — YU-NO "Memories" Ch2 (ALG=4, FB=7) — 金属的な温かい音
inline constexpr OpnaDriver::FmVoice VIBRAPHONE =
{
	4, 7,
	{
		{0, 8, 27, 0, 31,  0, 0, 0,  0},  // OP1
		{0, 5, 20, 0, 31,  0, 0, 0,  0},  // OP2
		{0, 8, 18, 0, 17,  0, 5, 0, 12},  // OP3
		{0,10, 18, 0, 17,  0, 5, 0, 12},  // OP4
	}
};

/// @brief パッド/ディストーション — YU-NO "Memories" Ch4 (ALG=6, FB=7) — 厚いパッド
inline constexpr OpnaDriver::FmVoice DIST_GUITAR =
{
	6, 7,
	{
		{7, 4, 23, 0, 31,  0, 0, 0,  0},  // OP1
		{7, 8, 29, 0,  5,  0, 5, 0, 10},  // OP2
		{3, 4, 31, 0,  5,  0, 5, 0, 10},  // OP3
		{3, 2, 29, 0,  5,  0, 5, 0, 10},  // OP4
	}
};

/// @brief プリセット番号からFmVoiceを取得する
/// @param index プリセット番号 (0-11)
/// @return FMボイスデータ
[[nodiscard]] inline constexpr const OpnaDriver::FmVoice& getPreset(int index) noexcept
{
	switch (index)
	{
	case 0:  return PIANO;
	case 1:  return BELL;
	case 2:  return BRASS;
	case 3:  return STRINGS;
	case 4:  return ORGAN;
	case 5:  return E_PIANO;
	case 6:  return BASS;
	case 7:  return FLUTE;
	case 8:  return KOTO;
	case 9:  return SYNTH_LEAD;
	case 10: return VIBRAPHONE;
	case 11: return DIST_GUITAR;
	default: return PIANO;
	}
}

} // namespace opna_presets
} // namespace mitiru_mml
