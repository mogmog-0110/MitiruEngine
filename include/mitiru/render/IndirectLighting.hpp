#pragma once

/// @file IndirectLighting.hpp
/// @brief 間接光の設定。焼いた放射照度のプローブ (拡散の GI) と反射のプローブ、画面の反射 (SSR)
/// @details どれも描画だけが読み、GameMemory には触れない。既定は無効で、無効のままの絵と GPU の仕事は変わらない。
///          使い方は docs/LIGHTING_GI.md、決めたことは ADR 0070。ゲーム DLL が SceneLook で頼む形 (IndirectLightLook) は ADR 0071。

#include <cstdint>
#include <type_traits>

namespace mitiru::render
{

/// @brief 焼いた光 (*.lighting.bin) をどう使うか。焼いた光を読み込んでいなければ何も起きない
struct GlobalIlluminationSettings
{
	bool  enabled = true;              ///< 焼いた放射照度で環境光を置き換える
	float intensity = 1.0f;            ///< 拡散の間接光に掛ける倍率
	int   toonBands = 0;               ///< トゥーンの陰影で間接光の明るさを刻む段数 (0 か 1 = 刻まない、2..8)
	float normalBias = 0.0f;           ///< 面から浮かせて引く距離 (m)。0 = 格子の一番狭い間隔の 3 割
	bool  reflectionProbes = true;     ///< PBR と水面の映り込みに反射のプローブを使う
	float reflectionIntensity = 1.0f;
};

/// @brief 画面の反射。前のフレームの深度の階層 (HZB) を辿り、当たった所の前のフレームの色を映す。
///        PBR の材質と水面が使い、外れた所は反射のプローブか IBL か空の色のまま
struct ScreenSpaceReflectionSettings
{
	bool  enabled = false;
	float maxRoughness = 0.6f;   ///< これより粗い面は辿らない (粗さが近づくほど薄める)
	float thickness = 0.35f;     ///< 当たりとみなす物の奥行き (m)。薄い物の裏を当たりにしない
	float maxDistance = 40.0f;   ///< 辿るレイの長さ (m)
	int   maxSteps = 64;         ///< HZB を辿る回数の上限
	float intensity = 1.0f;
};

/// @brief IndirectLightLook::flags の bit
inline constexpr std::uint8_t kIndirectGi               = 1u << 0;   ///< 焼いた放射照度で環境光を置き換える
inline constexpr std::uint8_t kIndirectReflectionProbes = 1u << 1;   ///< PBR と水面に反射のプローブを映す
inline constexpr std::uint8_t kIndirectSsr              = 1u << 2;   ///< 画面の反射 (設定画面の上限で切れる)
inline constexpr std::uint8_t kIndirectOff              = 1u << 7;   ///< 0 以外の flags で「全部切る」を表す

/// @brief ゲーム DLL が SceneLook の末尾で頼む間接光 (ABI v51、16 byte)。毎フレーム sceneLook3D で頼み直す
/// @details flags が 0 の間は host の既定 (`--lighting-bake` / `--ssr` と設定画面) のまま。0 以外にすると bit 0..2 が
///          そのまま使う物になる (kIndirectOff だけなら全部切る)。数値の 0 は既定の値 (giIntensity 1、ssrMaxRoughness 0.6、
///          ssrIntensity 1)。焼いた光そのものは Screen::lightingBake3D で選ぶ。
struct IndirectLightLook
{
	std::uint8_t flags = 0;            ///< kIndirect*
	std::uint8_t toonBands = 0;        ///< 間接光の明るさを刻む段数 (0 か 1 = 刻まない、2..8)
	std::uint8_t _pad[2] = {0, 0};
	float giIntensity = 0.0f;
	float ssrMaxRoughness = 0.0f;
	float ssrIntensity = 0.0f;
};

static_assert(sizeof(IndirectLightLook) == 16 && std::is_trivially_copyable_v<IndirectLightLook>,
              "IndirectLightLook wire size 固定 (ABI v51)");

/// @brief ゲームの頼み (look) と host の既定から、描画に使う GI と SSR の設定を決める。flags が 0 なら既定をそのまま返す
inline void resolveIndirectLook(const IndirectLightLook& look, const GlobalIlluminationSettings& hostGi,
                                const ScreenSpaceReflectionSettings& hostSsr, GlobalIlluminationSettings& gi,
                                ScreenSpaceReflectionSettings& ssr) noexcept
{
	gi = hostGi;
	ssr = hostSsr;
	if (look.flags == 0) { return; }
	gi.enabled = (look.flags & kIndirectGi) != 0;
	gi.reflectionProbes = (look.flags & kIndirectReflectionProbes) != 0;
	gi.intensity = look.giIntensity > 0.0f ? look.giIntensity : 1.0f;
	gi.toonBands = look.toonBands;
	ssr.enabled = (look.flags & kIndirectSsr) != 0;
	ssr.maxRoughness = look.ssrMaxRoughness > 0.0f ? look.ssrMaxRoughness : ScreenSpaceReflectionSettings{}.maxRoughness;
	ssr.intensity = look.ssrIntensity > 0.0f ? look.ssrIntensity : 1.0f;
}

} // namespace mitiru::render
