#pragma once
/// @file OplPresets.hpp
/// @brief OPL 系 (YM3526、YM3812、YMF262) の FM 音色の型と、このライブラリで作った小さなプリセット 8 種
/// @details プリセットは 2 オペレーター FM の一般的な作り方から手で決めた値で、既存のゲームや
///          曲から抜いた値ではない。OPM のプリセットと同じ名前を、似た役割で並べてある。

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mitiru_mml
{

/// @brief OPL のオペレーター 1 つぶん
struct OplOpParams
{
	bool tremolo = false;         ///< AM (トレモロ)
	bool vibrato = false;         ///< VIB (ビブラート)
	bool sustain = true;          ///< EGT (true なら発音中は SL の音量を保つ。false なら減衰し続ける)
	bool keyScaleRate = false;    ///< KSR
	std::uint8_t multiple = 1;    ///< MULT (0-15。0 は 0.5 倍、11 は 10 倍、13 は 12 倍、14 と 15 は 15 倍)
	std::uint8_t keyScaleLevel = 0; ///< KSL (0-3)
	std::uint8_t totalLevel = 0;  ///< TL (0=最大、63=最小)
	std::uint8_t attackRate = 15; ///< AR (0-15。15 は即座に立ち上がる)
	std::uint8_t decayRate = 0;   ///< DR (0-15)
	std::uint8_t sustainLevel = 0; ///< SL (0-15、0=最大)
	std::uint8_t releaseRate = 6; ///< RR (0-15)
	std::uint8_t waveform = 0;    ///< 波形 (OPL は 0 だけ、OPL2 は 0-3、OPL3 は 0-7)
};

/// @brief OPL の FM 音色 (変調器と搬送波の 2 オペレーター)
struct OplVoice
{
	bool additive = false;        ///< true なら 2 つとも出力へ足す (CNT=1)。false なら変調器が搬送波を変調する
	std::uint8_t feedback = 0;    ///< 変調器のフィードバック (0-7)
	OplOpParams modulator;
	OplOpParams carrier;
};

namespace opl_presets
{

inline constexpr int COUNT = 8;

inline constexpr std::array<std::string_view, COUNT> NAMES = {
	"epiano", "brass", "bass", "strings", "lead", "bell", "organ", "pluck"};

namespace detail
{

/// @brief オペレーターの主な項目だけを並べて作る
[[nodiscard]] constexpr OplOpParams op(
	int multiple, int totalLevel, int attackRate, int decayRate, int sustainLevel, int releaseRate,
	bool sustain, int waveform = 0, bool vibrato = false)
{
	OplOpParams p;
	p.multiple = static_cast<std::uint8_t>(multiple);
	p.totalLevel = static_cast<std::uint8_t>(totalLevel);
	p.attackRate = static_cast<std::uint8_t>(attackRate);
	p.decayRate = static_cast<std::uint8_t>(decayRate);
	p.sustainLevel = static_cast<std::uint8_t>(sustainLevel);
	p.releaseRate = static_cast<std::uint8_t>(releaseRate);
	p.sustain = sustain;
	p.waveform = static_cast<std::uint8_t>(waveform);
	p.vibrato = vibrato;
	return p;
}

[[nodiscard]] constexpr OplVoice voice(bool additive, int feedback, OplOpParams modulator, OplOpParams carrier)
{
	OplVoice v;
	v.additive = additive;
	v.feedback = static_cast<std::uint8_t>(feedback);
	v.modulator = modulator;
	v.carrier = carrier;
	return v;
}

} // namespace detail

/// @brief プリセット本体。範囲外の番号は 0 番になる
[[nodiscard]] inline const OplVoice& getPreset(int index)
{
	using detail::op;
	using detail::voice;
	static const std::array<OplVoice, COUNT> TABLE = {{
		// 0 epiano: 打鍵で明るく、すぐ落ち着く
		voice(false, 3, op(1, 22, 15, 7, 6, 6, false), op(1, 0, 15, 5, 4, 6, false)),
		// 1 brass: 立ち上がりが少し遅く、持続する
		voice(false, 4, op(1, 24, 9, 4, 3, 5, true), op(1, 0, 10, 3, 2, 5, true)),
		// 2 bass: 変調器を深くして低音に芯を作る
		voice(false, 5, op(1, 26, 15, 8, 8, 8, false), op(1, 0, 15, 6, 5, 8, true)),
		// 3 strings: ゆっくり立ち上がり、ビブラートをかける
		voice(false, 2, op(1, 32, 6, 2, 2, 5, true, 0, true), op(1, 4, 6, 2, 1, 5, true, 0, true)),
		// 4 lead: フィードバックを大きくして鋭くする
		voice(false, 6, op(2, 20, 15, 4, 3, 6, true), op(1, 0, 15, 3, 2, 6, true)),
		// 5 bell: 7 倍の変調で金属的にし、減衰させる
		voice(false, 0, op(7, 26, 15, 6, 8, 5, false), op(1, 0, 15, 5, 8, 5, false)),
		// 6 organ: 1 倍と 2 倍を足したドローバー風
		voice(true, 3, op(1, 10, 15, 0, 0, 8, true), op(2, 14, 15, 0, 0, 8, true)),
		// 7 pluck: 短く減衰する撥弦
		voice(false, 2, op(3, 30, 15, 9, 8, 8, false), op(1, 4, 15, 8, 10, 8, false)),
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

} // namespace opl_presets

} // namespace mitiru_mml
