#pragma once
/// @file FmPresets.hpp
/// @brief MitiruEngine向けFM音色プリセットブリッジ
/// @details mitiru_mml::opna_presetsが提供するYM2608 FM音色プリセットを
///          mitiru::audio名前空間から簡便にアクセスするためのブリッジヘッダー。
///
///          プリセットはすべてYU-NO（PC-98版, elf 1996）のVGMデータから
///          抽出した実測値に基づく。
///
/// 使用例:
/// @code
/// #include <mitiru/audio/FmPresets.hpp>
///
/// mitiru_mml::OpnaDriver driver;
/// driver.setFmVoice(0, mitiru::audio::fm_presets::PIANO);
/// driver.fmNoteOn(0, 60);  // C4
/// @endcode
///
/// @note プリセット番号は mitiru_mml::opna_presets::getPreset(index) でも取得できる。
///       FM音色コマンド: MML内では @FM0〜@FM11 に対応する。

#include <mitiru_mml/OpnaPresets.hpp>

namespace mitiru::audio
{

/// @brief FM音色プリセット — mitiru_mml::opna_presetsへのエイリアス
namespace fm_presets = mitiru_mml::opna_presets;

} // namespace mitiru::audio
