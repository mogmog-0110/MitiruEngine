#pragma once
/// @file OpmPresets.hpp
/// @brief YM2151 (OPM) の FM 音色の型と、このライブラリで作った小さなプリセット 8 種
/// @details プリセットは FM 音源の一般的な作り方 (周波数比、エンベロープの形) から手で決めた値で、
///          既存のゲームや曲から抜いた値ではない。

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mitiru_mml
{

/// @brief OPM のオペレーター 1 つぶん (レジスタの名前に合わせた項目)
struct OpmOpParams
{
	std::uint8_t detune1 = 0;      ///< DT1 (0-7。4 以上は負の方向)
	std::uint8_t multiple = 1;     ///< MUL (0-15、0 は 0.5 倍)
	std::uint8_t totalLevel = 0;   ///< TL (0=最大、127=無音)
	std::uint8_t keyScale = 0;     ///< KS (0-3)
	std::uint8_t attackRate = 31;  ///< AR (0-31)
	bool amEnable = false;         ///< AMS-EN (LFO の振幅変調を受ける)
	std::uint8_t decay1Rate = 0;   ///< D1R (0-31)
	std::uint8_t detune2 = 0;      ///< DT2 (0-3。粗い音程のずらし)
	std::uint8_t decay2Rate = 0;   ///< D2R (0-31)
	std::uint8_t sustainLevel = 0; ///< D1L (0-15、0=最大)
	std::uint8_t releaseRate = 7;  ///< RR (0-15)
};

/// @brief OPM の FM 音色 (4 オペレーター)
/// @details ops は OPN 系のアルゴリズム図と同じ並び (op1〜op4)。レジスタ上のスロットは
///          op1=M1、op2=C1、op3=M2、op4=C2 に当たる。
///          アルゴリズム 0〜3 は op4、4 は op2 と op4、5 と 6 は op2〜op4、7 は全部がキャリア。
struct OpmVoice
{
	std::uint8_t algorithm = 4;     ///< CON (0-7)
	std::uint8_t feedback = 0;      ///< FL (0-7、op1 のフィードバック)
	std::uint8_t pmSensitivity = 0; ///< PMS (0-7)
	std::uint8_t amSensitivity = 0; ///< AMS (0-3)
	OpmOpParams ops[4];
};

namespace opm_presets
{

inline constexpr int COUNT = 8;

inline constexpr std::array<std::string_view, COUNT> NAMES = {
	"epiano", "brass", "bass", "strings", "lead", "bell", "organ", "pluck"};

namespace detail
{

/// @brief オペレーターの主な項目だけを並べて作る
[[nodiscard]] constexpr OpmOpParams op(
	int multiple, int totalLevel, int attackRate, int decay1Rate, int sustainLevel,
	int decay2Rate, int releaseRate, int detune1 = 0, int keyScale = 0)
{
	OpmOpParams p;
	p.multiple = static_cast<std::uint8_t>(multiple);
	p.totalLevel = static_cast<std::uint8_t>(totalLevel);
	p.attackRate = static_cast<std::uint8_t>(attackRate);
	p.decay1Rate = static_cast<std::uint8_t>(decay1Rate);
	p.sustainLevel = static_cast<std::uint8_t>(sustainLevel);
	p.decay2Rate = static_cast<std::uint8_t>(decay2Rate);
	p.releaseRate = static_cast<std::uint8_t>(releaseRate);
	p.detune1 = static_cast<std::uint8_t>(detune1);
	p.keyScale = static_cast<std::uint8_t>(keyScale);
	return p;
}

[[nodiscard]] constexpr OpmVoice voice(int algorithm, int feedback, OpmOpParams o1, OpmOpParams o2,
	OpmOpParams o3, OpmOpParams o4)
{
	OpmVoice v;
	v.algorithm = static_cast<std::uint8_t>(algorithm);
	v.feedback = static_cast<std::uint8_t>(feedback);
	v.ops[0] = o1;
	v.ops[1] = o2;
	v.ops[2] = o3;
	v.ops[3] = o4;
	return v;
}

} // namespace detail

/// @brief プリセット本体。範囲外の番号は 0 番になる
[[nodiscard]] inline const OpmVoice& getPreset(int index)
{
	using detail::op;
	using detail::voice;
	static const std::array<OpmVoice, COUNT> TABLE = {{
		// 0 epiano: 2 段の変調を 2 組。高い比の変調 (14 倍) が短く鳴って打鍵の音になる
		voice(4, 4, op(1, 30, 31, 14, 5, 5, 6, 0, 1), op(1, 4, 31, 10, 4, 3, 7, 0, 1),
			op(14, 36, 31, 20, 3, 8, 8), op(1, 8, 31, 12, 5, 4, 7, 3)),
		// 1 brass: 立ち上がりが遅めで、変調を深くして明るくする
		voice(4, 5, op(1, 24, 20, 5, 2, 0, 6), op(1, 2, 22, 4, 2, 0, 6),
			op(2, 30, 18, 5, 2, 0, 6), op(1, 2, 20, 4, 2, 0, 6, 3)),
		// 2 bass: 直列 4 段。最後のキャリアは 0.5 倍で 1 オクターブ下のうなりを作る
		voice(0, 6, op(1, 24, 31, 15, 8, 6, 6), op(1, 26, 31, 12, 6, 5, 6),
			op(1, 18, 31, 10, 5, 4, 6), op(0, 0, 31, 8, 4, 3, 6)),
		// 3 strings: 4 本のキャリアを少しずらして重ね、立ち上がりをゆっくりにする
		voice(7, 0, op(1, 22, 12, 0, 0, 0, 6, 1), op(1, 24, 11, 0, 0, 0, 6, 5),
			op(2, 30, 12, 0, 0, 0, 6, 2), op(1, 22, 11, 0, 0, 0, 6, 6)),
		// 4 lead: フィードバックを大きくした鋭い音
		voice(4, 6, op(1, 20, 28, 6, 3, 0, 7), op(1, 0, 28, 4, 1, 0, 7),
			op(2, 26, 28, 6, 3, 0, 7), op(1, 2, 28, 4, 1, 0, 7, 3)),
		// 5 bell: 整数でない比に近い 7 倍と 3 倍の変調で金属的にする
		voice(4, 3, op(7, 24, 31, 8, 6, 5, 5), op(1, 0, 31, 7, 6, 4, 5),
			op(3, 30, 31, 9, 7, 5, 5), op(2, 6, 31, 8, 6, 4, 5)),
		// 6 organ: 1、2、4、8 倍を足したドローバー風
		voice(7, 0, op(1, 12, 31, 0, 0, 0, 8), op(2, 18, 31, 0, 0, 0, 8),
			op(4, 24, 31, 0, 0, 0, 8), op(8, 30, 31, 0, 0, 0, 8)),
		// 7 pluck: 短く減衰する撥弦
		voice(4, 5, op(1, 26, 31, 18, 4, 10, 9), op(1, 6, 31, 16, 3, 8, 9),
			op(3, 34, 31, 22, 4, 12, 9), op(1, 10, 31, 18, 3, 8, 9, 2)),
	}};
	const std::size_t i = (index >= 0 && index < COUNT) ? static_cast<std::size_t>(index) : 0u;
	return TABLE[i];
}

/// @brief 名前から番号を探す。見つからなければ -1
[[nodiscard]] inline int findPreset(std::string_view name) noexcept
{
	for (int i = 0; i < COUNT; ++i)
	{
		if (NAMES[static_cast<std::size_t>(i)] == name)
		{
			return i;
		}
	}
	return -1;
}

} // namespace opm_presets

} // namespace mitiru_mml
