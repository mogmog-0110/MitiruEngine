#pragma once

/// @file SceneLookExtras.hpp
/// @brief IRenderer3D::applySceneLookExtras (ADR 0099) の出し分け。濡れ・雲・色の仕上げはエージェントが別なので、
///        renderer の override はこの 1 本を呼び、各エージェントは自分の applyXxx だけを実装する
/// @details applySceneLookExtras は sceneLook3D を呼んだゲームのフレーム頭にしか届かない。値が 0 に戻った
///          ことも各 applyXxx に届くので、0 のときに前の状態を片付けるのは applyXxx の側の仕事。

#include <cstdint>

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/render/SceneLook.hpp>

namespace mitiru::render::detail
{

/// 3 つのフィールドを、それぞれの applyXxx へ渡す。Renderer は applyWetness / applyCloudCover / applyColorLut を持つこと
template <class Renderer>
inline void dispatchSceneLookExtras(Renderer& renderer, const SceneLook& look)
{
	renderer.applyWetness(look.wetness);
	renderer.applyCloudCover(look.cloudCover);
	renderer.applyColorLut(look.colorLut);
}

/// 実装が入るまでの applyXxx の本体。IRenderer3D の既定と同じ key と文で、0 でなければ 1 度だけ知らせる
inline void warnWetnessNotReady(std::uint8_t wetness)
{
	if (wetness != 0) { debug::warnOnce("abi62.wetness", "濡れた面 (SceneLook::wetness) はまだエントリポイントだけで、絵は変わりません"); }
}

inline void warnCloudCoverNotReady(std::uint8_t cloudCover)
{
	if (cloudCover != 0) { debug::warnOnce("abi62.cloud_cover", "雲の層 (SceneLook::cloudCover) はまだエントリポイントだけで、絵は変わりません"); }
}

/// 番号の組が無いとき (宣言が無い、または番号が範囲外) の知らせ。色の仕上げのエージェントが使う
inline void warnColorLutMissing(bool noLooksDeclared)
{
	debug::warnOnce("color_lut.missing", noLooksDeclared
		? "色の LUT (SceneLook::colorLut) を使うには、assets/color_looks.json に looks を書いてください"
		: "SceneLook::colorLut の番号が assets/color_looks.json の looks の数を超えています");
}

}  // namespace mitiru::render::detail
