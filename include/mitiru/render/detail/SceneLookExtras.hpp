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

/// 各フィールドを、それぞれの applyXxx へ渡す。Renderer は applyWetness / applyCloudCover / applyColorLut / applyAutoExposure / applyContactShadow を持つこと
template <class Renderer>
inline void dispatchSceneLookExtras(Renderer& renderer, const SceneLook& look)
{
	renderer.applyWetness(look.wetness);
	renderer.applyCloudCover(look.cloudCover);
	renderer.applyColorLut(look.colorLut);
	renderer.applyAutoExposure(look.autoExposureSpeed, look.autoExposureRangeEv, look.autoExposureBiasEv);
	renderer.applyContactShadow(look.contactShadow, look.contactShadowDistance);
}

/// 番号の組が無いとき (宣言が無い、または番号が範囲外) の知らせ。色の仕上げのエージェントが使う
inline void warnColorLutMissing(bool noLooksDeclared)
{
	debug::warnOnce("color_lut.missing", noLooksDeclared
		? "色の LUT (SceneLook::colorLut) を使うには、assets/color_looks.json に looks を書いてください"
		: "SceneLook::colorLut の番号が assets/color_looks.json の looks の数を超えています");
}

}  // namespace mitiru::render::detail
