#pragma once

/// @file AnimPose.hpp
/// @brief 姿勢の評価。AnimPoseParams (POD) と AnimAsset (読み取り専用) だけから姿勢を出す純関数。
/// @details 隠れた状態を持たないので、AnimPoseParams を GameMemory に置けば巻き戻しとリプレイで
///          姿勢も戻る。AnimPose は計算の置き場で、中身は毎回すべて上書きする (前の値に依らない)。
///          レイヤは並び順に重ねる。Override は「いまの姿勢」と「レイヤの姿勢」を weight × マスクで
///          混ぜ、最初のレイヤはレスト姿勢へ重なる。Additive はクリップの t=0 からの差分を足す。
///          評価はノード数ぶんの配列を使い回すだけで、毎フレームの確保はしない。

#include <algorithm>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

#include <sgc/math/Mat4.hpp>

#include <mitiru/animation/AnimAsset.hpp>
#include <mitiru/animation/AnimMath.hpp>
#include <mitiru/render/AnimationSampler.hpp>

namespace mitiru::animation
{

inline constexpr int kAnimMaxLayers = 16;

enum AnimLayerFlag : std::uint8_t
{
	kAnimLayerLoop = 1u << 0,      ///< 時間をクリップ長で折り返す。無ければ 0..長さに丸める
	kAnimLayerAdditive = 1u << 1,  ///< t=0 からの差分を足す
};

enum AnimPoseFlag : std::uint32_t
{
	/// ルート骨の水平移動と Y 回転をクリップ先頭の値に固定する。動きはルートモーションとして
	/// ゲームが自分で足す (rootMotionDelta)。
	kAnimPoseInPlace = 1u << 0,
};

/// @brief 1 レイヤの指定。16 バイトの POD。
struct AnimLayer
{
	std::int16_t clip = -1;     ///< AnimAsset::clips の添字。-1 はレスト姿勢
	std::int16_t mask = -1;     ///< AnimAsset::masks の添字。-1 は全身
	std::uint8_t flags = kAnimLayerLoop;
	std::uint8_t reserved0 = 0;
	std::uint16_t reserved1 = 0;
	float timeSec = 0.0f;
	float weight = 1.0f;        ///< 0..1。範囲外は丸める

	/// @brief clip を timeSec の位置で weight だけ重ねるレイヤ。clip と mask には findClip / findMask の値をそのまま渡す
	[[nodiscard]] static constexpr AnimLayer clipAt(int clip, float timeSec, float weight = 1.0f,
	                                                std::uint8_t flags = kAnimLayerLoop, int mask = -1) noexcept
	{
		AnimLayer layer;
		layer.clip = animIndex16(clip);
		layer.mask = animIndex16(mask);
		layer.flags = flags;
		layer.timeSec = timeSec;
		layer.weight = weight;
		return layer;
	}
};

/// @brief 姿勢の入力のすべて。GameMemory に置ける。
struct AnimPoseParams
{
	std::uint32_t layerCount = 0;
	std::uint32_t flags = 0;
	AnimLayer layers[kAnimMaxLayers] = {};
};

static_assert(sizeof(AnimLayer) == 16);
static_assert(std::is_trivially_copyable_v<AnimLayer>);
static_assert(std::is_trivially_copyable_v<AnimPoseParams>);
static_assert(sizeof(AnimPoseParams) == 8 + 16 * kAnimMaxLayers);

/// @brief レイヤを末尾に足す。満杯なら false。
inline bool pushLayer(AnimPoseParams& params, const AnimLayer& layer) noexcept
{
	if (params.layerCount >= static_cast<std::uint32_t>(kAnimMaxLayers)) { return false; }
	params.layers[params.layerCount++] = layer;
	return true;
}

/// @brief 評価結果と作業用の配列。ノードごとの局所姿勢とモデル空間の行列を持つ。
struct AnimPose
{
	std::vector<NodeTRS> local;
	std::vector<sgc::Mat4f> model;
	std::vector<NodeTRS> scratch;

	/// 容量は保ったまま、asset のノード数に合わせる (同じ asset なら 2 回目以降は確保しない)。
	void resize(const AnimAsset& asset)
	{
		const auto n = asset.nodes.size();
		local.resize(n);
		model.resize(n);
		scratch.resize(n);
	}
};

/// @brief レイヤの時刻をクリップの範囲に入れる。
[[nodiscard]] inline float clipLocalTime(const AnimClip& clip, float timeSec, bool loop) noexcept
{
	if (loop) { return render::wrapTime(timeSec, clip.durationSec); }
	return std::clamp(timeSec, 0.0f, std::max(clip.durationSec, 0.0f));
}

/// @brief レスト姿勢を out に写す。
inline void restPose(const AnimAsset& asset, std::span<NodeTRS> out) noexcept
{
	for (std::size_t i = 0; i < out.size() && i < asset.nodes.size(); ++i)
	{
		const auto& n = asset.nodes[i];
		out[i] = {n.translation, n.rotation, n.scale};
	}
}

/// @brief クリップを時刻 t (範囲内) でサンプルする。チャンネルの無い要素はレスト姿勢のまま。
inline void sampleClip(const AnimAsset& asset, const AnimClip& clip, float t, std::span<NodeTRS> out) noexcept
{
	restPose(asset, out);
	for (const auto& ch : clip.channels)
	{
		if (ch.nodeIndex < 0 || static_cast<std::size_t>(ch.nodeIndex) >= out.size()) { continue; }
		const auto v = render::sampleChannel(ch, t);
		auto& p = out[static_cast<std::size_t>(ch.nodeIndex)];
		switch (ch.path)
		{
		case render::GltfAnimPath::Translation: p.t = {v.x, v.y, v.z}; break;
		case render::GltfAnimPath::Rotation:    p.r = v; break;
		case render::GltfAnimPath::Scale:       p.s = {v.x, v.y, v.z}; break;
		}
	}
}

/// @brief ルート骨のモデル空間の姿勢 (平行移動と回転) を局所姿勢から出す。
[[nodiscard]] inline NodeTRS rootModelTransform(const AnimAsset& asset, const NodeTRS& local) noexcept
{
	const auto& p = asset.rootParentRest;
	NodeTRS m;
	m.r = quatNormalize(quatMul(p.r, local.r));
	m.t = p.t + quatRotate(p.r, p.s * local.t);
	m.s = p.s * local.s;
	return m;
}

/// @brief ルート骨の水平移動と Y 回転をクリップ先頭へ戻す (kAnimPoseInPlace)。
inline void stripRootMotion(const AnimAsset& asset, const AnimClip& clip, std::span<NodeTRS> pose) noexcept
{
	const int root = asset.rootMotionNode;
	if (root < 0 || static_cast<std::size_t>(root) >= pose.size()) { return; }
	auto& local = pose[static_cast<std::size_t>(root)];
	const auto& p = asset.rootParentRest;
	const NodeTRS m = rootModelTransform(asset, local);
	const sgc::Vec3f mt{clip.rootStart.t.x, m.t.y, clip.rootStart.t.z};
	const auto mr = quatMul(clip.rootStart.twist, quatMul(quatConj(quatTwistY(m.r)), m.r));
	const auto pInv = quatConj(p.r);
	local.r = quatNormalize(quatMul(pInv, mr));
	const auto lt = quatRotate(pInv, mt - p.t);
	const auto safe = [](float v, float s) { return (s > 1e-12f || s < -1e-12f) ? v / s : v; };
	local.t = {safe(lt.x, p.s.x), safe(lt.y, p.s.y), safe(lt.z, p.s.z)};
}

/// @brief ノード i に対するレイヤの効き。マスク外の添字は 0。
[[nodiscard]] inline float layerNodeWeight(const AnimAsset& asset, int mask, float weight, std::size_t i) noexcept
{
	if (mask < 0) { return weight; }
	if (static_cast<std::size_t>(mask) >= asset.masks.size()) { return 0.0f; }
	const auto& w = asset.masks[static_cast<std::size_t>(mask)].weights;
	return i < w.size() ? weight * w[i] : 0.0f;
}

/// @brief base をレイヤの姿勢へ寄せる (T / S は線形、R は slerp)。
inline void applyOverride(const AnimAsset& asset, std::span<NodeTRS> base, std::span<const NodeTRS> layer,
                          int mask, float weight) noexcept
{
	for (std::size_t i = 0; i < base.size() && i < layer.size(); ++i)
	{
		const float w = layerNodeWeight(asset, mask, weight, i);
		if (!(w > 0.0f)) { continue; }
		if (w >= 1.0f)
		{
			base[i] = layer[i];
			continue;
		}
		base[i].t = lerp3(base[i].t, layer[i].t, w);
		base[i].r = quatSlerp(quatNormalize(base[i].r), quatNormalize(layer[i].r), w);
		base[i].s = lerp3(base[i].s, layer[i].s, w);
	}
}

/// @brief base にレイヤの「ref からの差分」を weight 倍して足す (局所空間の加算)。
inline void applyAdditive(const AnimAsset& asset, std::span<NodeTRS> base, std::span<const NodeTRS> layer,
                          std::span<const NodeTRS> ref, int mask, float weight) noexcept
{
	const auto safeRatio = [](float a, float b) { return (b > 1e-12f || b < -1e-12f) ? a / b : 1.0f; };
	for (std::size_t i = 0; i < base.size() && i < layer.size() && i < ref.size(); ++i)
	{
		const float w = std::min(layerNodeWeight(asset, mask, weight, i), 1.0f);
		if (!(w > 0.0f)) { continue; }
		const auto dr = quatMul(quatConj(quatNormalize(ref[i].r)), quatNormalize(layer[i].r));
		base[i].t = base[i].t + (layer[i].t - ref[i].t) * w;
		base[i].r = quatNormalize(quatMul(base[i].r, quatNlerp(kQuatIdentity, dr, w)));
		const sgc::Vec3f ds{safeRatio(layer[i].s.x, ref[i].s.x), safeRatio(layer[i].s.y, ref[i].s.y),
		                    safeRatio(layer[i].s.z, ref[i].s.z)};
		base[i].s = base[i].s * lerp3({1, 1, 1}, ds, w);
	}
}

/// @brief 1 レイヤを pose.scratch へサンプルして pose.local へ重ねる。
inline void applyLayer(const AnimAsset& asset, const AnimLayer& layer, std::uint32_t poseFlags, AnimPose& pose) noexcept
{
	const float weight = std::min(layer.weight, 1.0f);
	if (!(weight > 0.0f)) { return; }
	const AnimClip* clip = asset.clip(layer.clip);
	const bool additive = (layer.flags & kAnimLayerAdditive) != 0;
	if (clip == nullptr)
	{
		if (!additive)
		{
			restPose(asset, pose.scratch);
			applyOverride(asset, pose.local, pose.scratch, layer.mask, weight);
		}
		return;
	}
	const float t = clipLocalTime(*clip, layer.timeSec, (layer.flags & kAnimLayerLoop) != 0);
	sampleClip(asset, *clip, t, pose.scratch);
	// 加算レイヤも先頭へ固定する。基準の t=0 は固定後と同じ値なので、ルートの水平移動と Y 回転は差分に残らない
	if ((poseFlags & kAnimPoseInPlace) != 0) { stripRootMotion(asset, *clip, pose.scratch); }
	if (additive)
	{
		applyAdditive(asset, pose.local, pose.scratch, clip->refPose, layer.mask, weight);
		return;
	}
	applyOverride(asset, pose.local, pose.scratch, layer.mask, weight);
}

/// @brief 局所姿勢だけを評価する。IK や手続き的な調整を挟むときに使い、最後に updateModelPose を呼ぶ。
inline void evaluateLocalPose(const AnimAsset& asset, const AnimPoseParams& params, AnimPose& pose)
{
	pose.resize(asset);
	restPose(asset, pose.local);
	const auto count = std::min(params.layerCount, static_cast<std::uint32_t>(kAnimMaxLayers));
	for (std::uint32_t i = 0; i < count; ++i)
	{
		applyLayer(asset, params.layers[i], params.flags, pose);
	}
}

/// @brief 局所姿勢からモデル空間の行列を作り直す。
inline void updateModelPose(const AnimAsset& asset, AnimPose& pose)
{
	pose.resize(asset);
	std::fill(pose.model.begin(), pose.model.end(), sgc::Mat4f::identity());
	for (const int idx : asset.evalOrder)
	{
		const auto ui = static_cast<std::size_t>(idx);
		const int parent = asset.nodes[ui].parent;
		const auto local = localMatrix(pose.local[ui]);
		pose.model[ui] = (parent >= 0 && static_cast<std::size_t>(parent) < pose.model.size())
		                     ? pose.model[static_cast<std::size_t>(parent)] * local
		                     : local;
	}
}

/// @brief 姿勢を評価する (局所姿勢 → モデル空間)。
inline void evaluatePose(const AnimAsset& asset, const AnimPoseParams& params, AnimPose& pose)
{
	evaluateLocalPose(asset, params, pose);
	updateModelPose(asset, pose);
}

/// @brief ノードのワールド行列。instanceWorld は描画に渡すのと同じ配置の行列。
[[nodiscard]] inline sgc::Mat4f boneWorld(const AnimPose& pose, int node, const sgc::Mat4f& instanceWorld)
{
	if (node < 0 || static_cast<std::size_t>(node) >= pose.model.size()) { return instanceWorld; }
	return instanceWorld * pose.model[static_cast<std::size_t>(node)];
}

/// @brief ソケットのワールド行列 (骨の行列 × ソケットの offset)。
[[nodiscard]] inline sgc::Mat4f socketWorld(const AnimAsset& asset, const AnimPose& pose, int socket,
                                            const sgc::Mat4f& instanceWorld)
{
	if (socket < 0 || static_cast<std::size_t>(socket) >= asset.sockets.size()) { return instanceWorld; }
	const auto& s = asset.sockets[static_cast<std::size_t>(socket)];
	return boneWorld(pose, s.node, instanceWorld) * s.offset;
}

/// @brief skin の joints 順にモデル空間の行列を集める (逆バインド行列は掛けない)。
/// @return 書いた個数。out が足りなければ入るぶんだけ書く。
inline std::size_t gatherJointModel(const AnimAsset& asset, const AnimPose& pose, int skin,
                                    std::span<sgc::Mat4f> out) noexcept
{
	if (skin < 0 || static_cast<std::size_t>(skin) >= asset.skins.size()) { return 0; }
	const auto& joints = asset.skins[static_cast<std::size_t>(skin)].joints;
	const std::size_t n = std::min(joints.size(), out.size());
	for (std::size_t j = 0; j < n; ++j)
	{
		const int node = joints[j];
		out[j] = (node >= 0 && static_cast<std::size_t>(node) < pose.model.size())
		             ? pose.model[static_cast<std::size_t>(node)]
		             : sgc::Mat4f::identity();
	}
	return n;
}

/// @brief スキニングの palette (joint のモデル行列 × 逆バインド行列) を作る。
inline std::size_t buildSkinPalette(const AnimAsset& asset, const AnimPose& pose, int skin,
                                    std::span<sgc::Mat4f> out) noexcept
{
	const std::size_t n = gatherJointModel(asset, pose, skin, out);
	if (n == 0) { return 0; }
	const auto& inverseBind = asset.skins[static_cast<std::size_t>(skin)].inverseBindMatrices;
	for (std::size_t j = 0; j < n && j < inverseBind.size(); ++j) { out[j] = out[j] * inverseBind[j]; }
	return n;
}

} // namespace mitiru::animation
