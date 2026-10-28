#pragma once

/// @file WetSurface.hpp
/// @brief 濡れた面 (SceneLook::wetness、R4)。描く直前に材質と色の調整を書き換える純関数で、シェーダーは触らない
/// @details 暗くする量は色の調整 (DrawTint::mul) で渡す。まとめ描きとインスタンス描画のどちらも mul を通るので、
///          拡散色の渡り方が違う経路でも同じ量だけ暗くなる。wetness が 0 なら呼ばれない (従来の絵と全画素同一)。
///          面の向きでは重みを付けず、場面の全部を一様に濡らす (雨の中では壁も人も濡れる)。路面だけを濡らしたいときは
///          ゲームが路面の材質を直接つやのある値にする。

#include <algorithm>
#include <cstdint>

#include <mitiru/render/DrawParams3D.hpp>
#include <mitiru/render/Material.hpp>

namespace mitiru::render
{

/// wetness 1.0 (255) のときの変化。拡散色は書いた色の値に掛ける係数、粗さと光沢は倍率
inline constexpr float kWetDarken = 0.70f;          ///< 拡散色に掛ける (書いた色の値)
inline constexpr float kWetRoughnessKeep = 0.30f;   ///< 粗さに掛ける (小さいほど鏡面に近い)
inline constexpr float kWetShininessGain = 4.0f;    ///< Phong 指数に掛ける (1 + gain * w)
inline constexpr float kWetSpecularAdd = 0.35f;     ///< 鏡面色に足す

/// SceneLook::wetness (0..255) を 0..1 にする
[[nodiscard]] inline float wetAmount(std::uint8_t wetness) noexcept
{
	return static_cast<float>(wetness) * (1.0f / 255.0f);
}

/// 拡散色 (書いた色の値) に掛ける係数
[[nodiscard]] inline float wetDarkenScale(float w) noexcept { return 1.0f - (1.0f - kWetDarken) * w; }

/// 粗さに掛ける倍率
[[nodiscard]] inline float wetRoughnessScale(float w) noexcept { return 1.0f - (1.0f - kWetRoughnessKeep) * w; }

/// Dawn は粗さを持たず、トゥーンのつやを DrawCB::toonMaterial の 2 値で決める。DX12 が粗さ 1 の材質から出す値
/// (DX12PipelineStates_PostProcess.inl の toonMaterial) と同じ式で、濡れた分だけの色の倍率と指数の係数
[[nodiscard]] inline float wetToonGain(float w) noexcept { return 1.0f + 1.2f * (1.0f - wetRoughnessScale(w)); }
[[nodiscard]] inline float wetToonSharpness(float w) noexcept { return 1.0f + 3.0f * (1.0f - wetRoughnessScale(w)); }

/// 濡れた材質。拡散色は触らない (暗くするのは wetTint)。テクスチャと alpha の設定はそのまま
[[nodiscard]] inline Material wetMaterial(const Material& m, float w) noexcept
{
	Material out = m;
	out.roughness = std::clamp(m.roughness, 0.0f, 1.0f) * wetRoughnessScale(w);
	out.shininess = m.shininess * (1.0f + kWetShininessGain * w);
	out.specular.r = std::min(1.0f, m.specular.r + kWetSpecularAdd * w);
	out.specular.g = std::min(1.0f, m.specular.g + kWetSpecularAdd * w);
	out.specular.b = std::min(1.0f, m.specular.b + kWetSpecularAdd * w);
	return out;
}

/// 濡れて暗くした色の調整。add (照明の後に足す色) と alpha は変えない
[[nodiscard]] inline DrawTint wetTint(const DrawTint& t, float w) noexcept
{
	DrawTint out = t;
	const float k = wetDarkenScale(w);
	out.mul[0] *= k; out.mul[1] *= k; out.mul[2] *= k;
	return out;
}

/// Dawn のように拡散色を直に受ける経路が、wetTint と同じだけ暗くするための色
[[nodiscard]] inline sgc::Colorf wetDiffuse(const sgc::Colorf& c, float w) noexcept
{
	const float k = wetDarkenScale(w);
	return {c.r * k, c.g * k, c.b * k, c.a};
}

}  // namespace mitiru::render
