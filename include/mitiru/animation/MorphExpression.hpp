#pragma once

/// @file MorphExpression.hpp
/// @brief モーフの重みの評価。クリップの weights と表情から、描画に渡すスロットごとの重みの配列を出す純関数。
/// @details 隠れた状態を持たないので、AnimPoseParams と ExpressionPose を GameMemory に置けば、巻き戻しとリプレイで表情も戻る。
///          加減乗除と比較だけを使うので、同じ入力からはどの機械でも同じビットが出る。
///
///          重みの決め方 (出力はスロットの順):
///          1. 既定の重み (node.weights、無ければ mesh.weights、無ければ 0)
///          2. AnimPoseParams のレイヤを並べた順に、クリップの weights を weight × マスクで混ぜる (姿勢の Override と同じ。加算レイヤは効かない)
///          3. 表情を足す。表情の値は 0..1 に丸め、VRM 1.0 の規則 (isBinary、overrideMouth / Blink / LookAt の block と blend) を当て、
///             バインドの weight を掛けてスロットごとに足し、0..1 に丸めてから 2 の結果へ足す。
///             表情の名前に当たる定義が無いときは、その名前のモーフ (extras.targetNames) を直接動かす。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

#include <mitiru/animation/AnimAsset.hpp>
#include <mitiru/animation/AnimPose.hpp>
#include <mitiru/animation/MorphExpressionTypes.hpp>

namespace mitiru::animation
{

/// @brief weights のチャンネルを時刻 t で標本化する。out は targetCount 個 (小さければ入るぶんだけ)。端より外は端のキー
inline void sampleMorphChannel(const render::GltfMorphChannel& ch, float t, std::span<float> out) noexcept
{
	const std::size_t n = std::min<std::size_t>(ch.targetCount, out.size());
	const std::size_t keys = ch.times.size();
	if (keys == 0 || ch.values.size() < keys * ch.targetCount)
	{
		for (std::size_t i = 0; i < n; ++i) { out[i] = 0.0f; }
		return;
	}
	const auto it = std::upper_bound(ch.times.begin(), ch.times.end(), t);
	const auto idx = static_cast<std::size_t>(it - ch.times.begin());
	const auto at = [&](std::size_t key, std::size_t target) { return ch.values[key * ch.targetCount + target]; };
	if (idx == 0 || idx >= keys)
	{
		const std::size_t key = (idx == 0) ? 0 : keys - 1;
		for (std::size_t i = 0; i < n; ++i) { out[i] = at(key, i); }
		return;
	}
	if (ch.interpolation == render::GltfAnimInterp::Step)
	{
		for (std::size_t i = 0; i < n; ++i) { out[i] = at(idx - 1, i); }
		return;
	}
	const float t0 = ch.times[idx - 1];
	const float span = ch.times[idx] - t0;
	const float u = (span > 1e-8f) ? (t - t0) / span : 0.0f;
	const bool cubic = ch.interpolation == render::GltfAnimInterp::CubicSpline &&
	                   ch.outTangents.size() >= keys * ch.targetCount && ch.inTangents.size() >= keys * ch.targetCount;
	for (std::size_t i = 0; i < n; ++i)
	{
		const float a = at(idx - 1, i);
		const float b = at(idx, i);
		if (!cubic) { out[i] = a + (b - a) * u; continue; }
		const float u2 = u * u;
		const float u3 = u2 * u;
		out[i] = (2 * u3 - 3 * u2 + 1) * a + (u3 - 2 * u2 + u) * span * ch.outTangents[(idx - 1) * ch.targetCount + i] +
		         (-2 * u3 + 3 * u2) * b + (u3 - u2) * span * ch.inTangents[idx * ch.targetCount + i];
	}
}

namespace detail
{

[[nodiscard]] inline float saturate01(float v) noexcept
{
	if (!(v > 0.0f)) { return 0.0f; }   // NaN も 0
	return v < 1.0f ? v : 1.0f;
}

[[nodiscard]] inline float lerpf(float a, float b, float w) noexcept { return a + (b - a) * w; }

/// @brief クリップのレイヤを順に重ねる (姿勢の Override と同じ混ぜ方)。out は既定の重みで埋めてあること
inline void applyMorphLayers(const AnimAsset& asset, const AnimPoseParams& params, std::span<float> out) noexcept
{
	const auto& table = asset.morph;
	const auto count = std::min(params.layerCount, static_cast<std::uint32_t>(kAnimMaxLayers));
	float sampled[kMorphMaxSlots];
	for (std::uint32_t li = 0; li < count; ++li)
	{
		const AnimLayer& layer = params.layers[li];
		const float weight = std::min(layer.weight, 1.0f);
		if (!(weight > 0.0f) || (layer.flags & kAnimLayerAdditive) != 0) { continue; }
		const AnimClip* clip = asset.clip(layer.clip);
		// 層の値 = 既定の重み。クリップが動かすスロットだけ標本化で置き換える
		for (std::size_t s = 0; s < out.size(); ++s) { sampled[s] = table.slots[s].defaultWeight; }
		if (clip != nullptr)
		{
			const float t = clipLocalTime(*clip, layer.timeSec, (layer.flags & kAnimLayerLoop) != 0);
			for (const auto& ch : clip->morphChannels)
			{
				if (ch.nodeIndex < 0 || static_cast<std::size_t>(ch.nodeIndex) >= table.nodeFirst.size()) { continue; }
				const int first = table.nodeFirst[static_cast<std::size_t>(ch.nodeIndex)];
				if (first < 0 || static_cast<std::size_t>(first) >= out.size()) { continue; }
				const std::size_t room = out.size() - static_cast<std::size_t>(first);
				const std::size_t n = std::min<std::size_t>(room, static_cast<std::size_t>(table.nodeCount[static_cast<std::size_t>(ch.nodeIndex)]));
				sampleMorphChannel(ch, t, std::span<float>(sampled + first, n));
			}
		}
		for (std::size_t s = 0; s < out.size(); ++s)
		{
			const float w = layerNodeWeight(asset, layer.mask, weight, static_cast<std::size_t>(table.slots[s].node));
			if (!(w > 0.0f)) { continue; }
			out[s] = (w >= 1.0f) ? sampled[s] : lerpf(out[s], sampled[s], w);
		}
	}
}

[[nodiscard]] inline MorphOverride overrideOf(const MorphExpression& e, MorphCategory c) noexcept
{
	switch (c)
	{
	case MorphCategory::Mouth:  return e.overrideMouth;
	case MorphCategory::Blink:  return e.overrideBlink;
	case MorphCategory::LookAt: return e.overrideLookAt;
	default:                    return MorphOverride::None;
	}
}

} // namespace detail

/// @brief 表情の定義 (名前の鍵に当たるもの) の 1 つぶんの、override を当てた後の値。pose にあって 0 以下の表情は 0
/// @details 返す値は isBinary を丸めた後。種類が同じ表情どうしの override は無効 (VRM の決まり)
inline void resolveExpressionValues(const MorphExpressionSet& set, const ExpressionPose& pose, float* values) noexcept
{
	const std::size_t n = std::min<std::size_t>(set.expressions.size(), static_cast<std::size_t>(kExpressionMaxEntries) * 4);
	float raw[kExpressionMaxEntries * 4] = {};
	const auto count = std::min(pose.count, static_cast<std::uint32_t>(kExpressionMaxEntries));
	for (std::uint32_t i = 0; i < count; ++i)
	{
		const int e = set.find(pose.entries[i].key);
		if (e < 0 || static_cast<std::size_t>(e) >= n) { continue; }
		raw[e] = detail::saturate01(pose.entries[i].weight);
	}
	for (std::size_t i = 0; i < n; ++i)
	{
		const auto& e = set.expressions[i];
		values[i] = e.isBinary ? (raw[i] > 0.5f ? 1.0f : 0.0f) : raw[i];
	}
	// 打ち消す側の値は isBinary を丸めた後 (values) を使う。打ち消される側は、効きが 0 を超えたら isBinary の表情を消す
	float factor[4] = {1.0f, 1.0f, 1.0f, 1.0f};   // 添字は MorphCategory
	for (int c = 1; c <= 3; ++c)
	{
		const auto cat = static_cast<MorphCategory>(c);
		bool blocked = false;
		float blend = 0.0f;
		for (std::size_t i = 0; i < n; ++i)
		{
			const auto& e = set.expressions[i];
			if (!(values[i] > 0.0f) || e.category == cat) { continue; }
			const MorphOverride ov = detail::overrideOf(e, cat);
			if (ov == MorphOverride::Block) { blocked = true; }
			else if (ov == MorphOverride::Blend) { blend += values[i]; }
		}
		factor[c] = blocked ? 0.0f : 1.0f - detail::saturate01(blend);
	}
	for (std::size_t i = 0; i < n; ++i)
	{
		const auto& e = set.expressions[i];
		const float f = factor[static_cast<std::size_t>(e.category)];
		if (f >= 1.0f) { continue; }
		values[i] = e.isBinary ? 0.0f : values[i] * f;
	}
}

/// @brief スロットごとの重みを out に書く。書いたスロットの数 (スロット表の数、out と kMorphMaxSlots で切る) を返す
/// @param pose  クリップの weights を混ぜるレイヤ。nullptr なら混ぜない
/// @param expr  表情の重み。nullptr なら足さない
inline int evaluateMorphWeights(const AnimAsset& asset, const AnimPoseParams* pose, const ExpressionPose* expr,
								std::span<float> out) noexcept
{
	const auto& table = asset.morph;
	const std::size_t slots = std::min<std::size_t>({static_cast<std::size_t>(table.slotCount()), out.size(), static_cast<std::size_t>(kMorphMaxSlots)});
	out = out.first(slots);
	for (std::size_t s = 0; s < slots; ++s) { out[s] = table.slots[s].defaultWeight; }
	if (slots == 0) { return 0; }
	if (pose != nullptr) { detail::applyMorphLayers(asset, *pose, out); }
	if (expr == nullptr || expr->count == 0) { return static_cast<int>(slots); }

	float sum[kMorphMaxSlots] = {};
	const auto& set = asset.expressions;
	float values[kExpressionMaxEntries * 4] = {};
	resolveExpressionValues(set, *expr, values);
	const std::size_t known = std::min<std::size_t>(set.expressions.size(), static_cast<std::size_t>(kExpressionMaxEntries) * 4);
	for (std::size_t i = 0; i < known; ++i)
	{
		if (!(values[i] > 0.0f)) { continue; }
		for (const auto& bind : set.expressions[i].binds)
		{
			const int slot = table.find(bind.node, bind.index);
			if (slot >= 0 && static_cast<std::size_t>(slot) < slots) { sum[slot] += bind.weight * values[i]; }
		}
	}
	// 定義の無い名前は、その名前のモーフを直接動かす
	const auto count = std::min(expr->count, static_cast<std::uint32_t>(kExpressionMaxEntries));
	for (std::uint32_t i = 0; i < count; ++i)
	{
		if (set.find(expr->entries[i].key) >= 0) { continue; }
		const float w = detail::saturate01(expr->entries[i].weight);
		if (!(w > 0.0f)) { continue; }
		for (std::size_t s = 0; s < slots; ++s)
		{
			if (table.slots[s].nameKey == expr->entries[i].key) { sum[s] += w; }
		}
	}
	for (std::size_t s = 0; s < slots; ++s) { out[s] += detail::saturate01(sum[s]); }
	return static_cast<int>(slots);
}

} // namespace mitiru::animation
