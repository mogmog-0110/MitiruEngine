#pragma once
/// @file OpnaVoice.hpp
/// @brief OPNA の FM 音色のデータ型
/// @details OpnaDriver から切り離してある。OpnaPresets (音色の表) と OpnaDriver が互いを
///          include し合わずに、この型だけを共有するため。

#include <cstdint>

namespace mitiru_mml
{

/// @brief FM オペレータのパラメーター (1 オペレータ分)
struct OpnaOpParams
{
	std::uint8_t detune = 0;       ///< デチューン (0-7)
	std::uint8_t multiple = 1;     ///< 周波数倍率 (0-15, 0=0.5倍)
	std::uint8_t totalLevel = 0;   ///< 出力レベル (0=最大, 127=無音)
	std::uint8_t keyScale = 0;     ///< キースケーリング (0-3)
	std::uint8_t attackRate = 31;  ///< アタックレート (0-31)
	std::uint8_t decayRate = 0;    ///< ディケイレート (0-31)
	std::uint8_t sustainRate = 0;  ///< サステインレート (0-31)
	std::uint8_t sustainLevel = 0; ///< サステインレベル (0-15, 0=最大)
	std::uint8_t releaseRate = 7;  ///< リリースレート (0-15)
};

/// @brief FM ボイス定義 (4 オペレータ + アルゴリズム + フィードバック)
struct OpnaFmVoice
{
	std::uint8_t algorithm = 4;  ///< アルゴリズム (0-7)
	std::uint8_t feedback = 5;   ///< フィードバック (0-7)
	OpnaOpParams ops[4];         ///< 4 オペレータ
};

} // namespace mitiru_mml
