#pragma once

/// @file Tween.hpp
/// @brief 汎用Tween/Easingシステム — UI遷移、ポップアップ、プロパティアニメーション用
/// @details 独立したヘッダ。Screen.hppに依存しない。
///          UIアニメ (通知ポップアップ、コンボ表示、遷移効果等) で使用。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <numbers>
#include <vector>

namespace mitiru::animation
{

// ============================================================================
// Easing Functions
// ============================================================================

/// イージング関数型 (t: 0-1 → 0-1)
using EaseFunc = float (*)(float);

namespace ease
{

inline float linear(float t) noexcept { return t; }

// ── Quad ──
inline float inQuad(float t) noexcept { return t * t; }
inline float outQuad(float t) noexcept { return t * (2.0f - t); }
inline float inOutQuad(float t) noexcept {
	return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
}

// ── Cubic ──
inline float inCubic(float t) noexcept { return t * t * t; }
inline float outCubic(float t) noexcept { float u = t - 1.0f; return u * u * u + 1.0f; }
inline float inOutCubic(float t) noexcept {
	return t < 0.5f ? 4.0f * t * t * t : (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) + 1.0f;
}

// ── Back ──
inline float inBack(float t) noexcept {
	constexpr float s = 1.70158f;
	return t * t * ((s + 1.0f) * t - s);
}
inline float outBack(float t) noexcept {
	constexpr float s = 1.70158f;
	float u = t - 1.0f;
	return u * u * ((s + 1.0f) * u + s) + 1.0f;
}

// ── Elastic ──
inline float outElastic(float t) noexcept {
	if (t <= 0.0f || t >= 1.0f) return t;
	constexpr float p = 0.3f;
	return std::pow(2.0f, -10.0f * t) *
	       std::sin((t - p / 4.0f) * (2.0f * std::numbers::pi_v<float>) / p) + 1.0f;
}

// ── Bounce ──
inline float outBounce(float t) noexcept {
	if (t < 1.0f / 2.75f) return 7.5625f * t * t;
	if (t < 2.0f / 2.75f) { t -= 1.5f / 2.75f; return 7.5625f * t * t + 0.75f; }
	if (t < 2.5f / 2.75f) { t -= 2.25f / 2.75f; return 7.5625f * t * t + 0.9375f; }
	t -= 2.625f / 2.75f;
	return 7.5625f * t * t + 0.984375f;
}
inline float inBounce(float t) noexcept { return 1.0f - outBounce(1.0f - t); }

} // namespace ease

// ============================================================================
// Tween
// ============================================================================

/// 1つのTweenインスタンス
struct Tween
{
	float from = 0.0f;
	float to = 1.0f;
	float duration = 1.0f;
	float elapsed = 0.0f;
	float delay = 0.0f;
	EaseFunc easing = ease::linear;
	std::function<void(float)> onUpdate;  ///< 毎フレーム呼ばれる (現在値)
	std::function<void()> onComplete;     ///< 完了時に呼ばれる
	bool finished = false;
	bool autoRemove = true;               ///< 完了後に自動削除

	/// 現在の値を取得
	[[nodiscard]] float value() const noexcept
	{
		if (elapsed < delay) return from;
		const float t = std::clamp((elapsed - delay) / duration, 0.0f, 1.0f);
		return from + (to - from) * easing(t);
	}

	/// 進捗率 (0-1)
	[[nodiscard]] float progress() const noexcept
	{
		if (elapsed < delay) return 0.0f;
		return std::clamp((elapsed - delay) / duration, 0.0f, 1.0f);
	}

	/// 更新
	void update(float dt)
	{
		if (finished) return;
		elapsed += dt;
		if (onUpdate) onUpdate(value());
		if (elapsed >= delay + duration)
		{
			finished = true;
			if (onUpdate) onUpdate(to);
			if (onComplete) onComplete();
		}
	}
};

// ============================================================================
// TweenManager
// ============================================================================

/// 複数のTweenを管理する
class TweenManager
{
public:
	/// Tweenを追加して参照を返す
	Tween& add(float from, float to, float duration,
	           EaseFunc easing = ease::linear,
	           std::function<void(float)> onUpdate = nullptr)
	{
		m_tweens.push_back({from, to, duration, 0.0f, 0.0f, easing,
		                    std::move(onUpdate), nullptr, false, true});
		return m_tweens.back();
	}

	/// 遅延付きTweenを追加
	Tween& addDelayed(float from, float to, float duration, float delay,
	                  EaseFunc easing = ease::linear,
	                  std::function<void(float)> onUpdate = nullptr)
	{
		auto& tw = add(from, to, duration, easing, std::move(onUpdate));
		tw.delay = delay;
		return tw;
	}

	/// 全Tweenを更新
	void update(float dt)
	{
		for (auto& tw : m_tweens) tw.update(dt);

		// 完了+autoRemoveのものを削除
		m_tweens.erase(
			std::remove_if(m_tweens.begin(), m_tweens.end(),
				[](const Tween& tw) { return tw.finished && tw.autoRemove; }),
			m_tweens.end());
	}

	/// 全クリア
	void clear() { m_tweens.clear(); }

	/// アクティブなTween数
	[[nodiscard]] std::size_t count() const noexcept { return m_tweens.size(); }

	/// 全て完了しているか
	[[nodiscard]] bool allDone() const noexcept { return m_tweens.empty(); }

private:
	std::vector<Tween> m_tweens;
};

} // namespace mitiru::animation
