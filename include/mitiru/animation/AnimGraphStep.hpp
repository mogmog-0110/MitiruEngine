#pragma once

/// @file AnimGraphStep.hpp
/// @brief 状態機械を dt 秒進める (stepAnimGraph)。
/// @details 1 レイヤの処理は次の順。(1) 今の状態の位置を進め、主なクリップのイベントを拾う。(2) クロスフェードを進める。
///          (3) 遷移を priority の順に調べ、成り立った最初の 1 本で状態を変える。トリガーは step の終わりに消える。
///          イベントは向かう先の状態のものだけを拾い、抜けていく状態のものは拾わない (足音が 2 回鳴らない)。
///          定義を読み込み直すと、次の step で状態の番号を名前の鍵で引き直す。

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <mitiru/animation/AnimEvents.hpp>
#include <mitiru/animation/AnimGraph.hpp>
#include <mitiru/animation/AnimGraphPose.hpp>
#include <mitiru/animation/AnimRootMotion.hpp>

namespace mitiru::animation
{

namespace detail
{

/// 状態の位置が 1 秒に進む周回の数。ブレンドスペースは重みで長さを混ぜる (どのクリップも同じ位置を共有する)
[[nodiscard]] inline float animStateRate(const AnimAsset& asset, const AnimGraphStateDef& st, const AnimGraphState& s,
                                         float phase) noexcept
{
	AnimGraphSample smp[kAnimGraphMaxStateSamples] = {};
	const int n = animStateSamples(asset, st, s, phase, 1.0f, smp);
	float rate = 0.0f;
	for (int i = 0; i < n; ++i)
	{
		const float d = clipDuration(asset, smp[i].clip);
		if (d > 0.0f) { rate += smp[i].weight / d; }
	}
	const float speed = st.speed * (st.speedParam >= 0 ? animParam(s, st.speedParam) : 1.0f);
	return rate * std::max(speed, 0.0f);
}

[[nodiscard]] inline int animDominantClip(const AnimAsset& asset, const AnimGraphStateDef& st, const AnimGraphState& s,
                                          float phase) noexcept
{
	AnimGraphSample smp[kAnimGraphMaxStateSamples] = {};
	return animStateSamples(asset, st, s, phase, 1.0f, smp) > 0 ? smp[0].clip : -1;
}

inline void pushGraphEvent(AnimGraphState& s, int nameId, int clip, float time) noexcept
{
	if (nameId < 0 || nameId > 0xFFFF || s.eventCount >= static_cast<std::uint32_t>(kAnimGraphMaxEvents)) { return; }
	s.events[s.eventCount++] = {static_cast<std::uint16_t>(nameId), static_cast<std::int16_t>(clip), time};
}

/// 折り返す前の位置 p1 を phase に書く。ループは周回を数え、ループしない状態は 1 で止める。
inline void wrapPhase(bool loop, float p1, float& phase, std::uint16_t& loops) noexcept
{
	if (!loop)
	{
		phase = std::min(p1, 1.0f);
		return;
	}
	const float k = std::floor(p1);
	phase = p1 - k;
	const float total = static_cast<float>(loops) + std::min(k, static_cast<float>(kAnimGraphMaxLoopsPerStep));
	loops = static_cast<std::uint16_t>(std::min(total, 65535.0f));
}

/// 重み付きのクリップ列が位置 p0 → p1 で動いたルートモーションを acc へ足す。sum は足した重みの和。
inline void accumulateRootMotion(const AnimAsset& asset, const AnimGraphSample* smp, int n, float p0, float p1,
                                 YawXform& acc, float& sum) noexcept
{
	for (int i = 0; i < n; ++i)
	{
		const float d = clipDuration(asset, smp[i].clip);
		if (!(smp[i].weight > 0.0f) || !(d > 0.0f)) { continue; }
		const YawXform m = rootMotionDelta(asset, smp[i].clip, p0 * d, p1 * d, smp[i].loop != 0);
		sum += smp[i].weight;
		acc.t = acc.t + m.t * smp[i].weight;
		acc.r = quatNlerp(acc.r, m.r, smp[i].weight / sum);
	}
}

[[nodiscard]] inline int findStateByKey(const AnimGraphLayerDef& def, std::int16_t idx, std::uint32_t key) noexcept
{
	if (idx >= 0 && static_cast<std::size_t>(idx) < def.states.size() && def.states[static_cast<std::size_t>(idx)].key == key)
	{
		return idx;
	}
	for (std::size_t i = 0; i < def.states.size(); ++i)
	{
		if (def.states[i].key == key) { return static_cast<int>(i); }
	}
	return -1;
}

[[nodiscard]] inline int findTransitionByKey(const AnimGraphLayerDef& def, std::int16_t idx, std::uint16_t key) noexcept
{
	if (idx < 0) { return -1; }
	if (static_cast<std::size_t>(idx) < def.transitions.size() && def.transitions[static_cast<std::size_t>(idx)].key == key) { return idx; }
	for (std::size_t i = 0; i < def.transitions.size(); ++i)
	{
		if (def.transitions[i].key == key) { return static_cast<int>(i); }
	}
	return -1;
}

/// 番号と鍵が合わなければ鍵で引き直す。見つからない今の状態は入口へ戻し、抜けていく状態は捨てる。
inline void resolveLayer(const AnimGraphLayerDef& def, AnimGraphLayerState& l) noexcept
{
	if (def.states.empty())
	{
		l = AnimGraphLayerState{};
		return;
	}
	if (const int st = findStateByKey(def, l.state, l.stateKey); st >= 0) { l.state = static_cast<std::int16_t>(st); }
	else
	{
		l = AnimGraphLayerState{};
		l.state = def.entry;
		l.stateKey = def.states[static_cast<std::size_t>(def.entry)].key;
	}
	if (l.from >= 0)
	{
		const int f = findStateByKey(def, l.from, l.fromKey);
		l.from = static_cast<std::int16_t>(f);
		if (f < 0) { l.fading = 0; }
	}
	l.transition = static_cast<std::int16_t>(findTransitionByKey(def, l.transition, l.transitionKey));
}

[[nodiscard]] inline bool animCondHolds(const AnimCondition& c, const AnimGraphStateDef& st, const AnimGraphLayerState& l,
                                        const AnimGraphState& s) noexcept
{
	const float v = animParam(s, c.param);
	switch (c.op)
	{
	case AnimCondOp::Greater:      return v > c.value;
	case AnimCondOp::GreaterEq:    return v >= c.value;
	case AnimCondOp::Less:         return v < c.value;
	case AnimCondOp::LessEq:       return v <= c.value;
	case AnimCondOp::Equal:        return v == c.value;
	case AnimCondOp::NotEqual:     return v != c.value;
	case AnimCondOp::IsTrue:       return v != 0.0f;
	case AnimCondOp::IsFalse:      return v == 0.0f;
	case AnimCondOp::Trigger:      return c.param >= 0 && c.param < kAnimGraphMaxParams && ((s.triggers >> c.param) & 1u) != 0;
	case AnimCondOp::Finished:     return st.loop ? l.loops >= 1 : l.phase >= 1.0f;
	case AnimCondOp::TimeAtLeast:  return l.stateTime >= c.value;
	case AnimCondOp::PhaseAtLeast: return l.phase >= c.value;
	}
	return false;
}

[[nodiscard]] inline bool animTransitionHolds(const AnimGraphTransitionDef& t, const AnimGraphStateDef& st,
                                              const AnimGraphLayerState& l, const AnimGraphState& s) noexcept
{
	if (t.from >= 0 ? t.from != l.state : (t.to == l.state && !t.allowSelf)) { return false; }
	if (l.phase < t.windowStart || l.phase > t.windowEnd) { return false; }
	for (const AnimCondition& c : t.conditions)
	{
		if (!animCondHolds(c, st, l, s)) { return false; }
	}
	return true;
}

/// 割り込まれたクロスフェードの今の姿勢を止めて残す (以後は時刻を進めない)。
inline void freezeLayer(const AnimGraph& graph, const AnimAsset& asset, AnimGraphState& s, int layer) noexcept
{
	auto& l = s.layers[layer];
	AnimGraphSample smp[kAnimGraphMaxLayerSamples] = {};
	const int n = animLayerSamples(graph, asset, s, layer, smp, kAnimGraphMaxFrozen);
	std::copy(smp, smp + n, l.frozen);
	l.frozenCount = static_cast<std::uint8_t>(n);
	l.from = -1;
	l.fromKey = 0;
}

inline void beginFade(AnimGraphLayerState& l, const AnimGraphTransitionDef& t) noexcept
{
	l.fadeTime = 0.0f;
	l.fadeDuration = t.duration;
	l.fading = t.duration > 0.0f ? 1 : 0;
	if (l.fading == 0)
	{
		l.from = -1;
		l.fromKey = 0;
		l.frozenCount = 0;
	}
}

inline void startTransition(const AnimGraph& graph, const AnimAsset& asset, AnimGraphState& s, int layer, int index) noexcept
{
	const auto& def = graph.layers[static_cast<std::size_t>(layer)];
	const auto& t = def.transitions[static_cast<std::size_t>(index)];
	auto& l = s.layers[layer];
	const auto& cur = def.states[static_cast<std::size_t>(l.state)];
	const auto& next = def.states[static_cast<std::size_t>(t.to)];
	for (const AnimCondition& c : t.conditions)
	{
		if (c.op == AnimCondOp::Trigger && c.param >= 0 && c.param < kAnimGraphMaxParams) { s.triggers &= ~(1u << c.param); }
	}
	pushGraphEvent(s, cur.exitEvent, -1, 0.0f);
	pushGraphEvent(s, next.enterEvent, -1, 0.0f);
	const bool sync = cur.syncGroup >= 0 && cur.syncGroup == next.syncGroup;
	const float phase = sync ? animSyncPhase(graph, asset, cur.syncGroup, animDominantClip(asset, cur, s, l.phase), l.phase,
	                                         animDominantClip(asset, next, s, 0.0f))
	                         : 0.0f;
	if (l.fading != 0) { freezeLayer(graph, asset, s, layer); }
	else
	{
		l.from = l.state;
		l.fromKey = l.stateKey;
		l.fromPhase = l.phase;
		l.frozenCount = 0;
	}
	l.state = t.to;
	l.stateKey = next.key;
	l.transition = static_cast<std::int16_t>(index);
	l.transitionKey = t.key;
	l.phase = phase;
	l.loops = 0;
	l.stateTime = 0.0f;
	beginFade(l, t);
}

/// 抜けていく状態の位置を f0 → f1 (折り返す前の値) へ進める。同じ同期の組なら向かう先の位置から写す。
inline void advanceFrom(const AnimGraph& graph, const AnimAsset& asset, const AnimGraphState& s, const AnimGraphLayerDef& def,
                        AnimGraphLayerState& l, int leaderClip, float dt, float& f0, float& f1) noexcept
{
	const auto& st = def.states[static_cast<std::size_t>(l.state)];
	const auto& fs = def.states[static_cast<std::size_t>(l.from)];
	f0 = l.fromPhase;
	const bool synced = fs.syncGroup >= 0 && fs.syncGroup == st.syncGroup;
	if (synced)
	{
		f1 = animSyncPhase(graph, asset, st.syncGroup, leaderClip, l.phase, animDominantClip(asset, fs, s, f0));
		if (f1 < f0) { f1 += 1.0f; }
	}
	else { f1 = f0 + animStateRate(asset, fs, s, f0) * dt; }
	std::uint16_t loops = 0;
	wrapPhase(fs.loop || synced, f1, l.fromPhase, loops);
}

inline void collectStateEvents(const AnimAsset& asset, AnimGraphState& s, int clip, bool loop, float p0, float p1) noexcept
{
	const float d = clipDuration(asset, clip);
	if (!(d > 0.0f) || (!loop && p0 >= 1.0f)) { return; }
	AnimEventHit hits[kAnimGraphMaxEvents];
	const int n = collectAnimEvents(asset, clip, p0 * d, p1 * d, loop, hits, kAnimGraphMaxEvents);
	for (int i = 0; i < n; ++i) { pushGraphEvent(s, hits[i].nameId, hits[i].clip, hits[i].clipTimeSec); }
}

inline void endFadeIfDone(AnimGraphLayerState& l) noexcept
{
	if (l.fadeTime < l.fadeDuration) { return; }
	l.fading = 0;
	l.from = -1;
	l.fromKey = 0;
	l.frozenCount = 0;
}

} // namespace detail

/// @brief 今の状態の位置を進め、イベントとルートモーション (最初のレイヤだけ) を拾う
inline void advanceAnimLayer(const AnimGraph& graph, const AnimAsset& asset, AnimGraphState& s, int layer, float dt) noexcept
{
	const auto& def = graph.layers[static_cast<std::size_t>(layer)];
	auto& l = s.layers[layer];
	const auto& st = def.states[static_cast<std::size_t>(l.state)];
	const float p0 = l.phase;
	const float p1 = p0 + detail::animStateRate(asset, st, s, p0) * dt;
	const int clip = detail::animDominantClip(asset, st, s, p0);
	detail::collectStateEvents(asset, s, clip, st.loop, p0, p1);
	const bool motion = layer == 0 && !def.additive;
	const float w = animLayerFadeWeight(def, l);
	YawXform delta{};
	float sum = 0.0f;
	AnimGraphSample smp[kAnimGraphMaxStateSamples] = {};
	if (motion) { detail::accumulateRootMotion(asset, smp, animStateSamples(asset, st, s, p0, w, smp), p0, p1, delta, sum); }
	detail::wrapPhase(st.loop, p1, l.phase, l.loops);
	l.stateTime += dt;
	if (l.fading != 0)
	{
		l.fadeTime += dt;
		if (l.from >= 0)
		{
			float f0 = 0.0f, f1 = 0.0f;
			detail::advanceFrom(graph, asset, s, def, l, clip, dt, f0, f1);
			const auto& fs = def.states[static_cast<std::size_t>(l.from)];
			if (motion)
			{
				detail::accumulateRootMotion(asset, smp, animStateSamples(asset, fs, s, f0, 1.0f - w, smp), f0, f1, delta, sum);
			}
		}
		detail::endFadeIfDone(l);
	}
	if (layer == 0) { s.rootDelta = delta; }
}

/// @brief 成り立つ遷移があれば 1 本だけ使う。戻り値は使った遷移の番号 (無ければ -1)
inline int evaluateAnimTransitions(const AnimGraph& graph, const AnimAsset& asset, AnimGraphState& s, int layer) noexcept
{
	const auto& def = graph.layers[static_cast<std::size_t>(layer)];
	const auto& l = s.layers[layer];
	if (l.fading != 0 && l.transition >= 0 && !def.transitions[static_cast<std::size_t>(l.transition)].interruptible) { return -1; }
	const auto& st = def.states[static_cast<std::size_t>(l.state)];
	for (std::size_t i = 0; i < def.transitions.size(); ++i)
	{
		if (detail::animTransitionHolds(def.transitions[i], st, l, s))
		{
			detail::startTransition(graph, asset, s, layer, static_cast<int>(i));
			return static_cast<int>(i);
		}
	}
	return -1;
}

/// @brief 状態機械を dt 秒進める。params とトリガーは呼ぶ前にゲームが書く。
/// @details 違うグラフの状態 (graphKey が違う、0 のまま) を渡したら、まず入口の状態に戻す。
///          読み込み直しで param の名前か型の並びが変わっていたら、param だけ既定値に戻す。
inline void stepAnimGraph(const AnimGraph& graph, const AnimAsset& asset, AnimGraphState& s, float dt) noexcept
{
	if (s.graphKey != graph.key) { resetAnimGraph(graph, s); }
	if (s.paramsKey != graph.paramsKey) { resetAnimParams(graph, s); }   // 並びが変わった param の値は別の param へ渡さない
	if (!(dt >= 0.0f) || !std::isfinite(dt)) { dt = 0.0f; }
	s.eventCount = 0;
	s.rootDelta = YawXform{};
	const auto layers = static_cast<std::uint32_t>(std::min(graph.layers.size(), static_cast<std::size_t>(kAnimGraphMaxLayers)));
	for (std::uint32_t i = s.layerCount; i < layers; ++i)
	{
		s.layers[i] = AnimGraphLayerState{};
		s.layers[i].stateKey = ~0u;   // 鍵が合わないので resolveLayer が入口へ置く
	}
	s.layerCount = layers;
	for (int i = 0; i < static_cast<int>(layers); ++i)
	{
		detail::resolveLayer(graph.layers[static_cast<std::size_t>(i)], s.layers[i]);
		if (s.layers[i].state < 0) { continue; }
		advanceAnimLayer(graph, asset, s, i, dt);
		(void)evaluateAnimTransitions(graph, asset, s, i);
	}
	s.triggers = 0;
}

} // namespace mitiru::animation
