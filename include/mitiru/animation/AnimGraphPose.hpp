#pragma once

/// @file AnimGraphPose.hpp
/// @brief 状態機械の今の状態から、姿勢へ出すクリップと重みを出す (animGraphPose)。
/// @details 状態の中のクリップは位置 (phase、0..1) を共有するので、長さの違う歩きと走りを混ぜても足並みがそろう。
///          クロスフェードの途中は、抜けていく状態 (割り込まれたときは止めた姿勢) と向かう先を曲線の重みで混ぜる。

#include <algorithm>
#include <cstdint>

#include <sgc/math/Vec2.hpp>

#include <mitiru/animation/AnimAsset.hpp>
#include <mitiru/animation/AnimBlendSpace.hpp>
#include <mitiru/animation/AnimGraph.hpp>
#include <mitiru/animation/AnimPose.hpp>

namespace mitiru::animation
{

/// 1 レイヤが出すクリップの上限 (向かう先と抜けていく側の和)
inline constexpr int kAnimGraphMaxLayerSamples = kAnimGraphMaxStateSamples + kAnimGraphMaxFrozen;

namespace detail
{

[[nodiscard]] inline float animFadeCurve(AnimFadeCurve curve, float t) noexcept
{
	t = std::clamp(t, 0.0f, 1.0f);
	switch (curve)
	{
	case AnimFadeCurve::Smooth:  return t * t * (3.0f - 2.0f * t);
	case AnimFadeCurve::EaseIn:  return t * t;
	case AnimFadeCurve::EaseOut: return 1.0f - (1.0f - t) * (1.0f - t);
	case AnimFadeCurve::Linear:
	default:                     return t;
	}
}

[[nodiscard]] inline float clipDuration(const AnimAsset& asset, int clip) noexcept
{
	const AnimClip* c = asset.clip(clip);
	return c != nullptr ? std::max(c->durationSec, 0.0f) : 0.0f;
}

/// 重みの大きい順に並べ (同じ重みは元の順)、cap 本に切って和を 1 にする。
[[nodiscard]] inline int keepHeaviest(AnimGraphSample* s, int n, int cap) noexcept
{
	std::stable_sort(s, s + n, [](const AnimGraphSample& a, const AnimGraphSample& b) { return a.weight > b.weight; });
	n = std::min(n, cap);
	float sum = 0.0f;
	for (int i = 0; i < n; ++i) { sum += s[i].weight; }
	if (!(sum > 0.0f)) { return 0; }
	for (int i = 0; i < n; ++i) { s[i].weight /= sum; }
	return n;
}

/// ブレンドスペースの重みを出す。戻り値は個数。
inline int blendWeights(const AnimGraphStateDef& st, const AnimGraphState& s, BlendWeight* out) noexcept
{
	const int n = std::min(static_cast<int>(st.points.size()), kMaxBlendSamples);
	if (n <= 0) { return 0; }
	if (st.kind == AnimMotionKind::Clip)
	{
		out[0] = {0, 1.0f};
		return 1;
	}
	if (st.kind == AnimMotionKind::Blend1D)
	{
		float th[kMaxBlendSamples] = {};
		for (int i = 0; i < n; ++i) { th[i] = st.points[static_cast<std::size_t>(i)].x; }
		return blendSpace1D(th, n, animParam(s, st.paramX), out);
	}
	sgc::Vec2f pts[kMaxBlendSamples] = {};
	for (int i = 0; i < n; ++i) { pts[i] = {st.points[static_cast<std::size_t>(i)].x, st.points[static_cast<std::size_t>(i)].y}; }
	return blendSpace2D(pts, n, {animParam(s, st.paramX), animParam(s, st.paramY)}, out);
}

} // namespace detail

/// @brief 状態 st を位置 phase で、weight を掛けたクリップの列にする。戻り値は書いた個数 (<= kAnimGraphMaxStateSamples)
inline int animStateSamples(const AnimAsset& asset, const AnimGraphStateDef& st, const AnimGraphState& s, float phase,
                            float weight, AnimGraphSample* out) noexcept
{
	BlendWeight w[kMaxBlendSamples] = {};
	const int n = detail::blendWeights(st, s, w);
	AnimGraphSample tmp[kMaxBlendSamples] = {};
	int count = 0;
	for (int i = 0; i < n; ++i)
	{
		if (!(w[i].weight > 0.0f)) { continue; }
		const int clip = st.points[static_cast<std::size_t>(w[i].index)].clip;
		tmp[count++] = {animIndex16(clip), static_cast<std::uint8_t>(st.loop ? 1 : 0), 0,
		                phase * detail::clipDuration(asset, clip), w[i].weight};
	}
	count = detail::keepHeaviest(tmp, count, kAnimGraphMaxStateSamples);
	for (int i = 0; i < count; ++i)
	{
		out[i] = tmp[i];
		out[i].weight *= weight;
	}
	return count;
}

/// @brief クロスフェードの向かう先の重み (0..1)。フェードしていなければ 1
[[nodiscard]] inline float animLayerFadeWeight(const AnimGraphLayerDef& def, const AnimGraphLayerState& l) noexcept
{
	if (l.fading == 0 || !(l.fadeDuration > 0.0f)) { return 1.0f; }
	const bool known = l.transition >= 0 && static_cast<std::size_t>(l.transition) < def.transitions.size();
	const AnimFadeCurve curve = known ? def.transitions[static_cast<std::size_t>(l.transition)].curve : AnimFadeCurve::Linear;
	return detail::animFadeCurve(curve, l.fadeTime / l.fadeDuration);
}

/// @brief レイヤ layer が今出すクリップ (重みの和は 1)。戻り値は個数 (<= cap)
inline int animLayerSamples(const AnimGraph& graph, const AnimAsset& asset, const AnimGraphState& s, int layer,
                            AnimGraphSample* out, int cap) noexcept
{
	if (layer < 0 || layer >= static_cast<int>(s.layerCount) || static_cast<std::size_t>(layer) >= graph.layers.size()) { return 0; }
	const auto& def = graph.layers[static_cast<std::size_t>(layer)];
	const auto& l = s.layers[layer];
	if (l.state < 0 || static_cast<std::size_t>(l.state) >= def.states.size()) { return 0; }
	AnimGraphSample all[kAnimGraphMaxLayerSamples] = {};
	const float w = animLayerFadeWeight(def, l);
	int n = animStateSamples(asset, def.states[static_cast<std::size_t>(l.state)], s, l.phase, w, all);
	if (l.fading != 0 && w < 1.0f)
	{
		if (l.from >= 0 && static_cast<std::size_t>(l.from) < def.states.size())
		{
			n += animStateSamples(asset, def.states[static_cast<std::size_t>(l.from)], s, l.fromPhase, 1.0f - w, all + n);
		}
		for (int i = 0; i < l.frozenCount && i < kAnimGraphMaxFrozen && l.from < 0; ++i)
		{
			all[n] = l.frozen[i];
			all[n++].weight *= 1.0f - w;
		}
	}
	n = detail::keepHeaviest(all, n, std::min(cap, kAnimGraphMaxLayerSamples));
	std::copy(all, all + n, out);
	return n;
}

/// @brief 今の状態を姿勢の入力にする。out のレイヤを書き直す (flags はそのまま)。
/// @details Override のレイヤは、レイヤの中の混ぜ方を保ったまま下の姿勢へ weight だけ重なる重みに直して並べる。
///          AnimPoseParams のレイヤが足りないときは、グラフのレイヤごとに同じ数ずつ、重みの大きいクリップを残す。
inline void animGraphPose(const AnimGraph& graph, const AnimAsset& asset, const AnimGraphState& s, AnimPoseParams& out) noexcept
{
	out.layerCount = 0;
	const int layers = std::min(static_cast<int>(s.layerCount), static_cast<int>(graph.layers.size()));
	if (layers <= 0) { return; }
	const int cap = std::max(kAnimMaxLayers / layers, 1);
	for (int i = 0; i < layers; ++i)
	{
		const auto& def = graph.layers[static_cast<std::size_t>(i)];
		const float layerWeight = std::clamp(def.weight * (def.weightParam >= 0 ? animParam(s, def.weightParam) : 1.0f), 0.0f, 1.0f);
		if (!(layerWeight > 0.0f)) { continue; }
		AnimGraphSample smp[kAnimGraphMaxLayerSamples] = {};
		const int n = animLayerSamples(graph, asset, s, i, smp, cap);
		float done = 0.0f;
		for (int k = 0; k < n; ++k)
		{
			done += smp[k].weight;
			std::uint8_t flags = smp[k].loop != 0 ? kAnimLayerLoop : std::uint8_t{0};
			float w = layerWeight * smp[k].weight;
			if (def.additive) { flags |= kAnimLayerAdditive; }
			else
			{
				const float rest = 1.0f - layerWeight * (1.0f - done);
				w = rest > 1e-6f ? w / rest : 1.0f;
			}
			(void)pushLayer(out, AnimLayer::clipAt(smp[k].clip, smp[k].time, w, flags, def.mask));
		}
	}
}

/// @brief 位置 phase をクリップ srcClip からクリップ dstClip へ、同期の組 group の印で写す。
/// @details src で phase の直前の印と次の印の間の割合を求め、dst で同じ名前の印から同じ割合だけ進んだ位置を返す。
///          印がどちらかに無いときは phase をそのまま返す (長さで割った位置をそろえる)。
namespace detail
{

inline constexpr int kAnimMaxSyncMarkers = 16;

struct SyncMarker
{
	float at = 0.0f;   ///< クリップの長さで割った時刻
	int name = -1;
};

/// クリップ clip のイベントのうち、同期の組 group の印を時刻順に out へ。戻り値は個数。
inline int gatherSyncMarkers(const AnimGraph& graph, const AnimAsset& asset, int group, int clip, SyncMarker* out) noexcept
{
	const AnimClip* c = asset.clip(clip);
	if (c == nullptr || !(c->durationSec > 0.0f) || group < 0 || static_cast<std::size_t>(group) >= graph.syncGroups.size())
	{
		return 0;
	}
	const auto& names = graph.syncGroups[static_cast<std::size_t>(group)].markers;
	int n = 0;
	for (const auto& e : c->events)
	{
		if (n < kAnimMaxSyncMarkers && std::find(names.begin(), names.end(), static_cast<int>(e.nameId)) != names.end())
		{
			out[n++] = {e.timeSec / c->durationSec, static_cast<int>(e.nameId)};
		}
	}
	return n;
}

} // namespace detail

[[nodiscard]] inline float animSyncPhase(const AnimGraph& graph, const AnimAsset& asset, int group, int srcClip,
                                         float phase, int dstClip) noexcept
{
	detail::SyncMarker a[detail::kAnimMaxSyncMarkers], b[detail::kAnimMaxSyncMarkers];
	const int na = detail::gatherSyncMarkers(graph, asset, group, srcClip, a);
	const int nb = detail::gatherSyncMarkers(graph, asset, group, dstClip, b);
	if (na == 0 || nb == 0) { return phase; }
	int i = na - 1;
	while (i >= 0 && a[i].at > phase) { --i; }
	// phase が最初の印より前なら、前の周回の最後の印から数える
	const int from = i >= 0 ? i : na - 1;
	const float start = i >= 0 ? a[i].at : a[na - 1].at - 1.0f;
	const float end = i < 0 ? a[0].at : (i + 1 < na ? a[i + 1].at : a[0].at + 1.0f);
	const float f = end > start ? (phase - start) / (end - start) : 0.0f;
	int j = 0;
	while (j < nb && b[j].name != a[from].name) { ++j; }
	if (j == nb) { return phase; }
	const float ds = b[j].at;
	const float de = j + 1 < nb ? b[j + 1].at : b[0].at + 1.0f;
	const float p = ds + f * (de - ds);
	return p >= 1.0f ? p - 1.0f : p;
}

} // namespace mitiru::animation
