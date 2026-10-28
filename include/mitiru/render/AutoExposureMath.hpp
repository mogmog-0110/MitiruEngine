#pragma once

/// @file AutoExposureMath.hpp
/// @brief 自動露出 (SceneLook::autoExposureSpeed、ADR 0109) の CPU の式。GPU の測定と試験が同じ式を読む
/// @details 画面の log2 輝度の平均 (GPU が測る) から狙いの EV を決め、フレームの歩数に比例して追い付かせる。
///          状態は描画の側だけが持ち、GameMemory にも InputSnapshot にも触れない。時間は実時間でなく、
///          ゲームのフレーム番号の差 (1 フレーム = 1/60 秒) で進めるので、同じ台本は同じ露出の履歴になる。

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mitiru::render
{

/// 平均がこの明るさになる倍率を狙う (中間灰。トーンマップの色調の軸 0.18 と同じ)
inline constexpr float kAutoExposureKey = 0.18f;
inline constexpr float kAutoExposureDefaultRangeEv = 3.0f;
/// 1 ゲームフレームが進める秒数。固定ステップの 60 Hz で決め打つ (実時間は読まない)
inline constexpr float kAutoExposureStepSeconds = 1.0f / 60.0f;
/// 1 点の輝度の下限と上限。空の太陽や光源 1 つが平均を引きずらないように頭打ちにする
inline constexpr float kAutoExposureLumaMin = 1.0f / 1024.0f;
inline constexpr float kAutoExposureLumaMax = 64.0f;
/// GPU が 1 辺に並べる標本の数 (縦横とも)。CPU の参照もこの格子で測る
inline constexpr int kAutoExposureGrid = 32;

struct AutoExposureParams
{
	float speed = 0.0f;     ///< 1 / 秒。0 以下は切
	float rangeEv = 0.0f;   ///< 0 = 既定。負は 0 と同じ
	float biasEv = 0.0f;

	[[nodiscard]] constexpr bool enabled() const noexcept { return speed > 0.0f; }
	[[nodiscard]] constexpr float range() const noexcept { return rangeEv > 0.0f ? rangeEv : kAutoExposureDefaultRangeEv; }
};

/// 1 標本の log2 輝度。GPU の PS と同じ頭打ちを掛ける。NaN は黒として数える
[[nodiscard]] inline float autoExposureTapLog2(float luma) noexcept
{
	if (!(luma == luma)) { luma = 0.0f; }
	return std::log2(std::clamp(luma, kAutoExposureLumaMin, kAutoExposureLumaMax));
}

/// 線形の RGB (Rec.709) の輝度
[[nodiscard]] constexpr float autoExposureLuma(float r, float g, float b) noexcept
{
	return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

/// 平均の log2 輝度 → 狙いの EV。平均が中間灰になる倍率の log2 に、ずらしを足し、幅で頭打ちにする
[[nodiscard]] inline float autoExposureTargetEv(float meanLog2Luma, const AutoExposureParams& p) noexcept
{
	const float want = std::log2(kAutoExposureKey) - meanLog2Luma + p.biasEv;
	return std::clamp(want, -p.range(), p.range());
}

struct AutoExposureState
{
	float ev = 0.0f;
	bool primed = false;   ///< 最初の測定を受けたか。受けるまでは倍率 1
};

/// 測定 1 つぶんを進めた状態を返す。最初の測定では狙いへ直ちに合わせる (暗い洞窟で始まる場面でも最初の絵から合っている)。
/// 以後は EV を指数で追わせる。frames はゲームのフレーム番号の差 (1 以上)
[[nodiscard]] inline AutoExposureState stepAutoExposure(const AutoExposureState& state, float meanLog2Luma,
                                                         const AutoExposureParams& p, int frames) noexcept
{
	const float target = autoExposureTargetEv(meanLog2Luma, p);
	if (!state.primed) { return {target, true}; }
	const float dt = static_cast<float>(std::max(frames, 1)) * kAutoExposureStepSeconds;
	const float blend = 1.0f - std::exp(-std::max(p.speed, 0.0f) * dt);
	return {state.ev + (target - state.ev) * blend, true};
}

/// tonemap の露出に掛ける倍率。測定を受ける前は 1
[[nodiscard]] inline float autoExposureScale(const AutoExposureState& state) noexcept
{
	return state.primed ? std::exp2(state.ev) : 1.0f;
}

}  // namespace mitiru::render
