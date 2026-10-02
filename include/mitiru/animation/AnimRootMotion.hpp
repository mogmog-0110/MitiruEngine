#pragma once

/// @file AnimRootMotion.hpp
/// @brief ルートモーション: クリップの時刻 t0 → t1 の間にルート骨が水平に動いた量と Y 回転。
/// @details 移動量は t0 時点のルート骨の向き (Y 回転) を基準にした座標で返すので、ゲームは自分の
///          向きで回して位置へ足す (pos += rotate(yaw, delta.t)、yaw = yaw * delta.r)。
///          単位はモデル空間 (配置のスケールは掛かっていない)。
///          ループするクリップは t0 / t1 を折り返さない時刻 (足し続けた秒) で渡せば、またいだ周回ぶんを
///          正しく合成する。t1 < t0 は逆再生として逆変換を返す。
///          描画でルートを動かさないようにするには姿勢の評価に kAnimPoseInPlace を付ける。

#include <algorithm>
#include <cmath>

#include <mitiru/animation/AnimAsset.hpp>
#include <mitiru/animation/AnimMath.hpp>
#include <mitiru/animation/AnimPose.hpp>
#include <mitiru/render/AnimationSampler.hpp>

namespace mitiru::animation
{

/// @brief 1 回の問い合わせで合成する周回数の上限 (巨大な dt で止まらないように)。
inline constexpr int kMaxRootMotionCycles = 64;

/// @brief 1 ノードだけをサンプルする (ルートモーション用。全ノードは要らない)。
[[nodiscard]] inline NodeTRS sampleNode(const AnimAsset& asset, const AnimClip& clip, int node, float t) noexcept
{
	const auto& n = asset.nodes[static_cast<std::size_t>(node)];
	NodeTRS p{n.translation, n.rotation, n.scale};
	for (const auto& ch : clip.channels)
	{
		if (ch.nodeIndex != node) { continue; }
		const auto v = render::sampleChannel(ch, t);
		switch (ch.path)
		{
		case render::GltfAnimPath::Translation: p.t = {v.x, v.y, v.z}; break;
		case render::GltfAnimPath::Rotation:    p.r = v; break;
		case render::GltfAnimPath::Scale:       p.s = {v.x, v.y, v.z}; break;
		}
	}
	return p;
}

/// @brief 同じ周回の中 (0 <= ta, tb <= 長さ) の移動量。
[[nodiscard]] inline YawXform rootMotionSegment(const AnimAsset& asset, const AnimClip& clip, float ta, float tb) noexcept
{
	const int root = asset.rootMotionNode;
	const auto m0 = rootModelTransform(asset, sampleNode(asset, clip, root, ta));
	const auto m1 = rootModelTransform(asset, sampleNode(asset, clip, root, tb));
	const auto twist0Inv = quatConj(quatTwistY(m0.r));
	return {quatRotate(twist0Inv, m1.t - m0.t), quatNormalize(quatMul(twist0Inv, quatTwistY(m1.r)))};
}

/// @brief t の周回番号。t - wrapTime(t) は長さのほぼ整数倍なので四捨五入で揃える (wrapTime と食い違わない)。
[[nodiscard]] inline double rootMotionCycle(float t, float duration) noexcept
{
	return std::floor(static_cast<double>(t - render::wrapTime(t, duration)) / duration + 0.5);
}

/// @brief クリップ clip の時刻 t0 → t1 のルートモーション。クリップやルート骨が無ければ動かない。
[[nodiscard]] inline YawXform rootMotionDelta(const AnimAsset& asset, int clipIndex, float t0, float t1, bool loop) noexcept
{
	const AnimClip* clip = asset.clip(clipIndex);
	if (clip == nullptr || asset.rootMotionNode < 0 || !std::isfinite(t0) || !std::isfinite(t1)) { return {}; }
	if (t1 < t0) { return inverseYaw(rootMotionDelta(asset, clipIndex, t1, t0, loop)); }
	const float d = clip->durationSec;
	if (!loop || d <= 0.0f)
	{
		const float hi = std::max(d, 0.0f);
		return rootMotionSegment(asset, *clip, std::clamp(t0, 0.0f, hi), std::clamp(t1, 0.0f, hi));
	}
	const float a = render::wrapTime(t0, d);
	const float b = render::wrapTime(t1, d);
	const double cycles = rootMotionCycle(t1, d) - rootMotionCycle(t0, d);
	if (cycles <= 0.0) { return rootMotionSegment(asset, *clip, a, b); }

	YawXform out = rootMotionSegment(asset, *clip, a, d);
	const YawXform full = rootMotionSegment(asset, *clip, 0.0f, d);
	const int middle = static_cast<int>(std::min(cycles - 1.0, static_cast<double>(kMaxRootMotionCycles)));
	for (int i = 0; i < middle; ++i) { out = composeYaw(out, full); }
	return composeYaw(out, rootMotionSegment(asset, *clip, 0.0f, b));
}

} // namespace mitiru::animation
