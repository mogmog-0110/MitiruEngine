#pragma once

/// @file AnimEvents.hpp
/// @brief アニメーションイベント: クリップの時刻 t0 → t1 の間に通過したイベントを時刻順に返す。
/// @details 区間は [t0, t1)。毎フレーム「前の時刻 → 今の時刻」で問えば、同じイベントを 2 回拾わない。
///          ループするクリップは折り返さない時刻 (足し続けた秒) で渡す。周回をまたいだぶんは周回ごとに拾う。
///          終端ちょうどのイベントは、ループしないクリップでは t1 が終端を越えたフレームで、ループする
///          クリップでは周回の切れ目をまたいだフレームで拾う。
///          イベントの定義はモデルの隣の sidecar JSON (AnimSidecar.hpp) に書く。

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <mitiru/animation/AnimAsset.hpp>
#include <mitiru/animation/AnimRootMotion.hpp>
#include <mitiru/render/AnimationSampler.hpp>

namespace mitiru::animation
{

/// @brief 1 回の問い合わせで数える周回数の上限 (巨大な dt で拾い切れないときは古い周回を捨てる)。
inline constexpr int kMaxEventCycles = 8;

struct AnimEventHit
{
	std::uint16_t nameId = 0;   ///< AnimAsset::eventNames の添字 (findEventName で引いた値と比べる)
	std::int16_t clip = -1;
	float clipTimeSec = 0.0f;   ///< クリップ内の時刻
};

namespace detail
{

/// @brief クリップ内の区間 [lo, hi) (closeHi なら [lo, hi]) に入るイベントを out へ足す。
inline int appendEventsInRange(const AnimClip& clip, int clipIndex, float lo, float hi, bool closeHi,
                               AnimEventHit* out, int count, int maxOut) noexcept
{
	for (const auto& e : clip.events)
	{
		if (count >= maxOut) { break; }
		const bool inside = e.timeSec >= lo && (closeHi ? e.timeSec <= hi : e.timeSec < hi);
		if (inside) { out[count++] = {e.nameId, static_cast<std::int16_t>(clipIndex), e.timeSec}; }
	}
	return count;
}

} // namespace detail

/// @brief clip の [t0, t1) で通過したイベントを out に書く。戻り値は書いた個数 (<= maxOut)。
inline int collectAnimEvents(const AnimAsset& asset, int clipIndex, float t0, float t1, bool loop,
                             AnimEventHit* out, int maxOut) noexcept
{
	const AnimClip* clip = asset.clip(clipIndex);
	if (clip == nullptr || out == nullptr || maxOut <= 0 || !(t1 > t0) || clip->events.empty()) { return 0; }
	if (!std::isfinite(t0) || !std::isfinite(t1)) { return 0; }
	const float d = clip->durationSec;
	if (!loop || d <= 0.0f)
	{
		return detail::appendEventsInRange(*clip, clipIndex, t0, t1, false, out, 0, maxOut);
	}
	const float a = render::wrapTime(t0, d);
	const float b = render::wrapTime(t1, d);
	const double cycles = rootMotionCycle(t1, d) - rootMotionCycle(t0, d);
	if (cycles <= 0.0) { return detail::appendEventsInRange(*clip, clipIndex, a, b, false, out, 0, maxOut); }

	const bool dropped = cycles > kMaxEventCycles;
	int count = dropped ? 0 : detail::appendEventsInRange(*clip, clipIndex, a, d, true, out, 0, maxOut);
	const int middle = static_cast<int>(std::min(cycles - 1.0, static_cast<double>(kMaxEventCycles - 1)));
	for (int i = 0; i < middle; ++i)
	{
		count = detail::appendEventsInRange(*clip, clipIndex, 0.0f, d, true, out, count, maxOut);
	}
	return detail::appendEventsInRange(*clip, clipIndex, 0.0f, b, false, out, count, maxOut);
}

} // namespace mitiru::animation
