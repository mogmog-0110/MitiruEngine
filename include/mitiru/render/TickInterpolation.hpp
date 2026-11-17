#pragma once

/// @file TickInterpolation.hpp
/// @brief 固定 tick の間に描く物体や動く箱、触手の先の位置と回転を、前後の tick から補間する。
/// @details update が tick の終わりに push し、draw が Screen::tickAlpha() で sample する。
/// 状態は巻き戻しで古くならないように、CameraBlend と同じく GameMemory の外に置く。
/// 頂点数が tick ごとに変わるメッシュは、元になるパーティクルや骨の位置を samplePositions で補間してから組み立てる。
/// TAA と動きのぼけの prevPositions は「前の描画フレームでその点があった位置」なので、前の tick ではなく
/// 前のフレームに実際に描いた値 (exchangeDrawn、previousDrawnPositions) を渡す。

#include <cmath>
#include <cstdint>
#include <type_traits>

#include <mitiru/action/ActionMath.hpp>

namespace mitiru::render
{

struct TickLimits
{
	/// 1 tick でこれより大きく動いた点は、ワープやリスポーンとみなして補間せず今の tick に置く。0 以下では切らない。
	float maxShift = 3.0f;
};

/// 前の tick と今の tick の値の組
template <class T>
struct TickPair
{
	T             prev{};
	T             cur{};
	std::uint8_t  valid  = 0;
	std::uint8_t  pad[3]{};
};

using TickFloat = TickPair<float>;
using TickVec3  = TickPair<action::Vec3>;
using TickPose  = TickPair<action::Pose>;

namespace tick_detail
{

[[nodiscard]] inline float sanitizeAlpha(float alpha) noexcept
{
	return std::isfinite(alpha) ? action::saturate(alpha) : 1.0f;
}

[[nodiscard]] inline bool finite(float v) noexcept { return std::isfinite(v); }
[[nodiscard]] inline bool finite(const action::Vec3& v) noexcept
{
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
[[nodiscard]] inline bool finite(const action::Pose& p) noexcept
{
	return finite(p.position) && std::isfinite(p.rotation.x) && std::isfinite(p.rotation.y) &&
	       std::isfinite(p.rotation.z) && std::isfinite(p.rotation.w);
}

[[nodiscard]] inline bool jumped(const action::Vec3& a, const action::Vec3& b, float maxShift) noexcept
{
	return maxShift > 0.0f && action::lengthSq(b - a) > maxShift * maxShift;
}

[[nodiscard]] inline bool farApart(float, float, float) noexcept { return false; }
[[nodiscard]] inline bool farApart(const action::Vec3& a, const action::Vec3& b, float maxShift) noexcept
{
	return jumped(a, b, maxShift);
}
[[nodiscard]] inline bool farApart(const action::Pose& a, const action::Pose& b, float maxShift) noexcept
{
	return jumped(a.position, b.position, maxShift);
}

[[nodiscard]] inline float blend(float a, float b, float t, float) noexcept { return action::lerpf(a, b, t); }

[[nodiscard]] inline action::Vec3 blend(const action::Vec3& a, const action::Vec3& b, float t, float maxShift) noexcept
{
	return jumped(a, b, maxShift) ? b : action::lerp(a, b, t);
}

[[nodiscard]] inline action::Pose blend(const action::Pose& a, const action::Pose& b, float t, float maxShift) noexcept
{
	if (jumped(a.position, b.position, maxShift)) { return b; }
	return {action::lerp(a.position, b.position, t), action::Quat::slerp(a.rotation, b.rotation, t)};
}

}  // namespace tick_detail

/// tick の終わりに現在値を積む。最初の push か cut = true (ワープ、シーンの切り替え) では、前の値も現在値にして補間しない。
template <class T>
inline void pushTick(TickPair<T>& pair, const T& value, bool cut = false) noexcept
{
	pair.prev = (pair.valid != 0 && !cut) ? pair.cur : value;
	pair.cur  = value;
	pair.valid = 1;
}

/// alpha は 0 で前の tick、1 で今の tick を表す。push がないとき、alpha が 1 以上のとき、値が有限でないときは現在値を返す。
template <class T>
[[nodiscard]] inline T sampleTick(const TickPair<T>& pair, float alpha, const TickLimits& limits = {}) noexcept
{
	if (pair.valid == 0 || !tick_detail::finite(pair.prev) || !tick_detail::finite(pair.cur)) { return pair.cur; }
	const float t = tick_detail::sanitizeAlpha(alpha);
	if (t >= 1.0f) { return pair.cur; }
	return tick_detail::blend(pair.prev, pair.cur, t, limits.maxShift);
}

/// パーティクル、骨、触手の節を点ごとに補間する。各点に maxShift を適用するため、1 点だけワープしても残りは補間する。
/// prev が nullptr のときは現在値を写す。out は cur と同じ場所でもよい。
inline void samplePositions(const action::Vec3* prev, const action::Vec3* cur, int count, float alpha,
                            action::Vec3* out, const TickLimits& limits = {}) noexcept
{
	if (cur == nullptr || out == nullptr || count <= 0) { return; }
	const float t = tick_detail::sanitizeAlpha(alpha);
	for (int i = 0; i < count; ++i)
	{
		const action::Vec3 c = cur[i];
		if (prev == nullptr || t >= 1.0f || !tick_detail::finite(prev[i]) || !tick_detail::finite(c))
		{
			out[i] = c;
			continue;
		}
		out[i] = tick_detail::blend(prev[i], c, t, limits.maxShift);
	}
}

/// 前の描画フレームで実際に描いた値。prevPositions はこの値でなければ、補間した今の位置との動きが食い違う
template <class T>
struct TickDrawn
{
	T            value{};
	std::uint8_t valid = 0;
	std::uint8_t pad[3]{};
};

/// 描画フレームごとに 1 回、今描く値を渡す。前のフレームで描いた値を返し、今の値を覚える。
/// 最初のフレームと、1 フレームで maxShift を超えて動いたとき (ワープ) は今の値を返し、動きのぼけの筋を出さない
template <class T>
[[nodiscard]] inline T exchangeDrawn(TickDrawn<T>& drawn, const T& now, const TickLimits& limits = {}) noexcept
{
	const bool keep = drawn.valid != 0 && !tick_detail::farApart(drawn.value, now, limits.maxShift);
	const T previous = keep ? drawn.value : now;
	drawn.value = now;
	drawn.valid = 1;
	return previous;
}

/// 点の並びの exchangeDrawn。last は前のフレームに描いた並びで、呼んだあとに now を last へ写す (数が違うか last が無ければ動かなかった扱い)
inline void previousDrawnPositions(const action::Vec3* last, int lastCount, const action::Vec3* now, int count,
                                   action::Vec3* out, const TickLimits& limits = {}) noexcept
{
	if (now == nullptr || out == nullptr || count <= 0) { return; }
	const bool same = last != nullptr && lastCount == count;
	for (int i = 0; i < count; ++i)
	{
		out[i] = (same && !tick_detail::farApart(last[i], now[i], limits.maxShift)) ? last[i] : now[i];
	}
}

static_assert(std::is_trivially_copyable_v<TickFloat> && std::is_standard_layout_v<TickFloat>);
static_assert(std::is_trivially_copyable_v<TickVec3> && std::is_standard_layout_v<TickVec3>);
static_assert(std::is_trivially_copyable_v<TickPose> && std::is_standard_layout_v<TickPose>);
static_assert(std::is_trivially_copyable_v<TickDrawn<TickVec3>> && std::is_standard_layout_v<TickDrawn<TickVec3>>);

}  // namespace mitiru::render
