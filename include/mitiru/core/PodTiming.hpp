#pragma once

/// @file PodTiming.hpp
/// @brief GameMemory に置ける flat POD のタイマー/トゥイーン。
/// @details
/// `t += dt; if (t > x)` の手書きパターンを置き換える最小部品。全型 trivially_copyable
/// なので GameMemory に埋めれば巻き戻し・リプレイ対象に自動編入される (ADR 0017)。
/// `animation/Tween.hpp` は関数ポインタを持つため GameMemory には置けない — こちらを使う。
///
/// @code
///   struct GameMemory {
///       mitiru::Timer   spawnTimer;   // 0.5 秒ごとに敵を出す
///       mitiru::Tween01 fadeIn;       // 1.2 秒かけてフェード
///   };
///   // update 内:
///   if (g.spawnTimer.every(0.5f, dt)) { spawnEnemy(g); }
///   float a = g.fadeIn.step(1.2f, dt, mitiru::Ease::OutQuad);  // 0..1
/// @endcode

#include <algorithm>
#include <cstdint>
#include <type_traits>

namespace mitiru
{

/// @brief イージング種別。関数ポインタではなく enum で持つ (flat POD 契約)。
enum class Ease : std::uint8_t
{
	Linear,
	InQuad,
	OutQuad,
	InOutQuad,
	OutCubic,
	OutBack,
	OutBounce,
};

namespace detail
{

inline float easeOutBack(float x) noexcept
{
	constexpr float s  = 1.70158f;  // 標準オーバーシュート定数
	constexpr float c3 = s + 1.0f;
	const float     u  = x - 1.0f;
	return 1.0f + c3 * u * u * u + s * u * u;
}

inline float easeOutBounce(float x) noexcept
{
	constexpr float n1 = 7.5625f;
	constexpr float d1 = 2.75f;
	if (x < 1.0f / d1) { return n1 * x * x; }
	if (x < 2.0f / d1) { const float u = x - 1.5f / d1; return n1 * u * u + 0.75f; }
	if (x < 2.5f / d1) { const float u = x - 2.25f / d1; return n1 * u * u + 0.9375f; }
	const float u = x - 2.625f / d1;
	return n1 * u * u + 0.984375f;
}

}  // namespace detail

/// @brief イージング純関数。入力は [0,1] に clamp される。
inline float ease(float x01, Ease e) noexcept
{
	const float x = std::clamp(x01, 0.0f, 1.0f);
	switch (e)
	{
	case Ease::Linear:    return x;
	case Ease::InQuad:    return x * x;
	case Ease::OutQuad:   return x * (2.0f - x);
	case Ease::InOutQuad: return x < 0.5f ? 2.0f * x * x : 1.0f - (-2.0f * x + 2.0f) * (-2.0f * x + 2.0f) * 0.5f;
	case Ease::OutCubic:  { const float u = x - 1.0f; return u * u * u + 1.0f; }
	case Ease::OutBack:   return detail::easeOutBack(x);
	case Ease::OutBounce: return detail::easeOutBounce(x);
	}
	return x;
}

/// @brief 繰り返し/ワンショット両用の蓄積タイマー。GameMemory に直接埋められる。
struct Timer
{
	float t = 0.0f;  ///< 蓄積経過秒

	/// @brief dt を足し、interval を超える度に true。超過分は次回へ繰越。
	bool every(float interval, float dt) noexcept
	{
		t += dt;
		if (interval > 0.0f && t >= interval)
		{
			t -= interval;
			return true;
		}
		return false;
	}

	/// @brief dt を足し、初めて deadline を超えた 1 回だけ true。
	bool once(float deadline, float dt) noexcept
	{
		const bool already = t >= deadline;
		t += dt;
		return !already && t >= deadline;
	}

	void reset() noexcept { t = 0.0f; }
};

/// @brief 0→1 を duration 秒で進める進行度。値への適用 (lerp 等) は呼び手が行う。
struct Tween01
{
	float t = 0.0f;  ///< 経過秒

	/// @brief dt 進めて eased 進行度 0..1 を返す。完了後は 1 を返し続ける。
	float step(float duration, float dt, Ease e = Ease::Linear) noexcept
	{
		t += dt;
		const float x = duration > 0.0f ? std::min(t / duration, 1.0f) : 1.0f;
		return ease(x, e);
	}

	[[nodiscard]] bool done(float duration) const noexcept { return t >= duration; }

	void reset() noexcept { t = 0.0f; }
};

// flat POD 契約の自己文書化 (GameMemory に置けることの保証)
static_assert(std::is_trivially_copyable_v<Timer>, "Timer は flat POD であること");
static_assert(std::is_trivially_copyable_v<Tween01>, "Tween01 は flat POD であること");

}  // namespace mitiru
