#pragma once

/// @file VisualIntentFx.hpp
/// @brief VisualIntent kind 2-5 (FadeOut / FadeIn / Shake / HitStop) の host 側演出状態
/// @details
/// DLL は intent を書くだけ (ADR 0005)。host (Engine) がこのクラスを 1 個所有し、
/// 毎フレーム advance(dt) で進めて描画 / dt 供給へ反映する。演出は観測対象外なので
/// GameMemory には入れない (engine 側状態で完結)。
///
/// 決定論:
///   - shake は乱数を使わず frame index ベースの擬似ノイズ (同 frame index = 同オフセット)。
///   - update は固定ステップ dt で呼ばれるため、advance / hitstop の経過も決定論的。
///     intent 自体が決定論的な module 出力なので、replay 時も同じフレームで同じ演出になる。

#include <cmath>
#include <cstdint>

#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::module
{

/// @brief shake の 1 フレーム分の描画オフセット (px)
struct ShakeOffset
{
	float dx = 0.0f;
	float dy = 0.0f;
};

/// @brief frame index → 決定的擬似ノイズオフセット。乱数不使用 (リプレイ bit-exact)。
/// @details 非整数周波数の sin/cos 合成。周期が割り切れないため見た目はランダム風に揺れる。
inline ShakeOffset deterministicShakeOffset(std::uint64_t frameIndex, float amplitudePx) noexcept
{
	// float の整数精度内に畳む (2^24 超で sin の引数精度が崩れるのを防ぐ)
	const float t = static_cast<float>(frameIndex % 100000ull);
	const float dx =
		amplitudePx * (0.6f * std::sin(t * 1.3f) + 0.4f * std::sin(t * 2.7f + 1.7f));
	const float dy =
		amplitudePx * (0.6f * std::cos(t * 1.7f) + 0.4f * std::cos(t * 2.3f + 0.9f));
	return ShakeOffset{dx, dy};
}

/// @brief fade 覆いの現在値 (a=0 なら描かない)
struct FadeOverlay
{
	float r = 0.0f;
	float g = 0.0f;
	float b = 0.0f;
	float a = 0.0f;
};

/// @brief kind 2-5 の演出状態機械。Engine が所有し ModuleAdapter が毎フレーム駆動する。
class VisualIntentFx
{
public:
	/// @brief intent 1 件を取り込む。kind 2-5 を消費したら true (1=Tint は呼び側が処理)。
	bool applyIntent(const VisualIntent& vi) noexcept
	{
		switch (vi.kind)
		{
		case kVisualIntentFadeOut:
			m_fadeR       = vi.r;
			m_fadeG       = vi.g;
			m_fadeB       = vi.b;
			m_fadeFrom    = m_fadeAlpha;
			m_fadeTo      = 1.0f;  // 覆いきったら fadeIn が来るまで維持
			m_fadeDurSec  = vi.durSec;
			m_fadeElapsed = 0.0f;
			if (m_fadeDurSec <= 0.0f) { m_fadeAlpha = 1.0f; }  // 尺 0 = 即座に覆う
			return true;
		case kVisualIntentFadeIn:
			// 覆い色は現状を引き継ぐ (覆い無し状態なら intent 色 — alpha 0 なので実害なし)
			if (m_fadeAlpha <= 0.0f) { m_fadeR = vi.r; m_fadeG = vi.g; m_fadeB = vi.b; }
			m_fadeFrom    = m_fadeAlpha;
			m_fadeTo      = 0.0f;
			m_fadeDurSec  = vi.durSec;
			m_fadeElapsed = 0.0f;
			if (m_fadeDurSec <= 0.0f) { m_fadeAlpha = 0.0f; }  // 尺 0 = 即座に晴らす
			return true;
		case kVisualIntentShake:
			if (vi.durSec > 0.0f && vi.a > 0.0f)
			{
				m_shakeAmpPx     = vi.a;
				m_shakeDurSec    = vi.durSec;
				m_shakeRemainSec = vi.durSec;
			}
			return true;
		case kVisualIntentHitStop:
			// 重ね掛けは加算でなく max — 二重発火で異常に長く止まらないように
			if (vi.durSec > m_hitStopRemainSec) { m_hitStopRemainSec = vi.durSec; }
			return true;
		default:
			return false;  // 0=None / 1=Tint / 未知 kind はここでは扱わない
		}
	}

	/// @brief 演出時間を dt (固定ステップ) 進める。毎 update 1 回呼ぶ。
	void advance(float dt) noexcept
	{
		if (dt < 0.0f) { dt = 0.0f; }

		// fade: from → to を durSec で線形補間。到達後は to を維持 (覆いの持続を保証)。
		if (m_fadeDurSec > 0.0f && m_fadeElapsed < m_fadeDurSec)
		{
			m_fadeElapsed += dt;
			float t = m_fadeElapsed / m_fadeDurSec;
			if (t > 1.0f) { t = 1.0f; }
			m_fadeAlpha = m_fadeFrom + (m_fadeTo - m_fadeFrom) * t;
		}

		// shake: 残量を減らす (振幅は currentShakeAmplitude が残量比で線形減衰)
		if (m_shakeRemainSec > 0.0f)
		{
			m_shakeRemainSec -= dt;
			if (m_shakeRemainSec < 0.0f) { m_shakeRemainSec = 0.0f; }
		}

		// hitstop: 実時間 (固定ステップ) で減衰
		if (m_hitStopRemainSec > 0.0f)
		{
			m_hitStopRemainSec -= dt;
			if (m_hitStopRemainSec < 0.0f) { m_hitStopRemainSec = 0.0f; }
		}
	}

	/// @brief 現在の fade 覆い (a=0 なら描画不要)
	[[nodiscard]] FadeOverlay overlay() const noexcept
	{
		return FadeOverlay{m_fadeR, m_fadeG, m_fadeB, m_fadeAlpha};
	}

	/// @brief shake が有効か (残量 > 0)
	[[nodiscard]] bool shakeActive() const noexcept { return m_shakeRemainSec > 0.0f; }

	/// @brief 現在の shake 振幅 (px)。残量比で線形減衰。
	[[nodiscard]] float currentShakeAmplitude() const noexcept
	{
		if (m_shakeRemainSec <= 0.0f || m_shakeDurSec <= 0.0f) { return 0.0f; }
		return m_shakeAmpPx * (m_shakeRemainSec / m_shakeDurSec);
	}

	/// @brief この frame index での決定的 shake オフセット
	[[nodiscard]] ShakeOffset shakeOffset(std::uint64_t frameIndex) const noexcept
	{
		return deterministicShakeOffset(frameIndex, currentShakeAmplitude());
	}

	/// @brief hitstop 中か (module へ渡す dt を 0 にするべきか)
	[[nodiscard]] bool hitStopActive() const noexcept { return m_hitStopRemainSec > 0.0f; }

private:
	// fade 覆い: from → to を durSec で補間し、到達後は to で保持する
	float m_fadeR = 0.0f, m_fadeG = 0.0f, m_fadeB = 0.0f;
	float m_fadeAlpha   = 0.0f;  ///< 現在 alpha (0=覆い無し / 1=完全に覆う)
	float m_fadeFrom    = 0.0f;
	float m_fadeTo      = 0.0f;
	float m_fadeDurSec  = 0.0f;
	float m_fadeElapsed = 0.0f;

	// shake
	float m_shakeAmpPx     = 0.0f;
	float m_shakeDurSec    = 0.0f;
	float m_shakeRemainSec = 0.0f;

	// hitstop
	float m_hitStopRemainSec = 0.0f;
};

}  // namespace mitiru::module
