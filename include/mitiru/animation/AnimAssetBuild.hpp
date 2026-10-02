#pragma once

/// @file AnimAssetBuild.hpp
/// @brief glTF の骨格・クリップと、追加の定義 (イベント・マスク・ソケット・ルート骨) から AnimAsset を組む。
/// @details 追加の定義はモデルの隣の sidecar JSON (AnimSidecar.hpp) から読むか、コードで埋める。
///          定義の名前が骨格やクリップに見つからないものは捨てて warnings に積む (ロードは止めない)。

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <sgc/math/Mat4.hpp>

#include <mitiru/animation/AnimAsset.hpp>
#include <mitiru/animation/AnimPose.hpp>
#include <mitiru/render/GltfTypes.hpp>

namespace mitiru::animation
{

struct AnimEventDef
{
	std::string clip;
	std::string name;
	float timeSec = 0.0f;
};

/// @brief include の骨とその子孫に weight、exclude の骨とその子孫に 0 を置くマスク。
struct AnimMaskDef
{
	std::string name;
	std::vector<std::string> include;
	std::vector<std::string> exclude;
	float weight = 1.0f;
};

struct AnimSocketDef
{
	std::string name;
	std::string bone;
	sgc::Vec3f offset{0, 0, 0};
	sgc::Vec4f rotation{0, 0, 0, 1};
};

struct AnimAssetOptions
{
	std::string rootMotionBone;   ///< 空なら skin の skeleton、無ければ最上位の joint
	std::vector<AnimEventDef> events;
	std::vector<AnimMaskDef> masks;
	std::vector<AnimSocketDef> sockets;
};

namespace detail
{

/// @brief node とその子孫に value を書く。壊れた glTF の循環は 1 度訪れたノードで止める。
inline void fillSubtree(const AnimAsset& asset, int node, float value, std::vector<float>& weights)
{
	std::vector<char> visited(weights.size(), 0);
	std::vector<int> stack{node};
	while (!stack.empty())
	{
		const int n = stack.back();
		stack.pop_back();
		if (n < 0 || static_cast<std::size_t>(n) >= weights.size() || visited[static_cast<std::size_t>(n)] != 0)
		{
			continue;
		}
		visited[static_cast<std::size_t>(n)] = 1;
		weights[static_cast<std::size_t>(n)] = value;
		for (const int c : asset.nodes[static_cast<std::size_t>(n)].children) { stack.push_back(c); }
	}
}

inline void addMasks(AnimAsset& asset, const std::vector<AnimMaskDef>& defs, std::vector<std::string>& warnings)
{
	for (const auto& def : defs)
	{
		if (asset.masks.size() >= static_cast<std::size_t>(kAnimMaxMasks))
		{
			warnings.push_back("mask " + def.name + ": マスクが多すぎる");
			break;
		}
		AnimMask mask{def.name, std::vector<float>(asset.nodes.size(), 0.0f)};
		const auto apply = [&](const std::vector<std::string>& bones, float value) {
			for (const auto& bone : bones)
			{
				const int node = asset.findNode(bone);
				if (node < 0) { warnings.push_back("mask " + def.name + ": 骨が無い: " + bone); }
				else { fillSubtree(asset, node, value, mask.weights); }
			}
		};
		apply(def.include, std::clamp(def.weight, 0.0f, 1.0f));
		apply(def.exclude, 0.0f);
		asset.masks.push_back(std::move(mask));
	}
}

/// @brief イベント名の番号。AnimEventKey::nameId が 16 bit なので、それを超える種類は -1。
[[nodiscard]] inline int internEventName(AnimAsset& asset, const std::string& name)
{
	const int found = asset.findEventName(name);
	if (found >= 0) { return found; }
	if (asset.eventNames.size() > kAnimMaxEventNames) { return -1; }
	asset.eventNames.push_back(name);
	return static_cast<int>(asset.eventNames.size() - 1);
}

inline void addEvents(AnimAsset& asset, const std::vector<AnimEventDef>& defs, std::vector<std::string>& warnings)
{
	for (const auto& def : defs)
	{
		const int clip = asset.findClip(def.clip);
		if (clip < 0)
		{
			warnings.push_back("event " + def.name + ": クリップが無い: " + def.clip);
			continue;
		}
		auto& c = asset.clips[static_cast<std::size_t>(clip)];
		const float t = std::clamp(def.timeSec, 0.0f, std::max(c.durationSec, 0.0f));
		if (t != def.timeSec) { warnings.push_back("event " + def.name + ": 時刻をクリップの範囲に丸めた"); }
		const int id = internEventName(asset, def.name);
		if (id < 0)
		{
			warnings.push_back("event " + def.name + ": イベント名の種類が多すぎる");
			continue;
		}
		c.events.push_back({static_cast<std::uint16_t>(id), t});
	}
	for (auto& c : asset.clips)
	{
		std::stable_sort(c.events.begin(), c.events.end(),
		                 [](const AnimEventKey& a, const AnimEventKey& b) { return a.timeSec < b.timeSec; });
	}
}

inline void addSockets(AnimAsset& asset, const std::vector<AnimSocketDef>& defs, std::vector<std::string>& warnings)
{
	for (const auto& def : defs)
	{
		const int node = asset.findNode(def.bone);
		if (node < 0)
		{
			warnings.push_back("socket " + def.name + ": 骨が無い: " + def.bone);
			continue;
		}
		asset.sockets.push_back({def.name, node, sgc::Mat4f::translation(def.offset) * quatToMat4(def.rotation)});
	}
}

/// @brief ルート骨の親までのモデル空間の変換 (レスト姿勢)。
[[nodiscard]] inline NodeTRS restParentTransform(const AnimAsset& asset, int node)
{
	if (node < 0) { return {}; }
	sgc::Mat4f m = sgc::Mat4f::identity();
	std::size_t depth = 0;
	for (int p = asset.nodes[static_cast<std::size_t>(node)].parent;
	     p >= 0 && static_cast<std::size_t>(p) < asset.nodes.size() && depth < asset.nodes.size();
	     p = asset.nodes[static_cast<std::size_t>(p)].parent, ++depth)
	{
		const auto& n = asset.nodes[static_cast<std::size_t>(p)];
		m = localMatrix({n.translation, n.rotation, n.scale}) * m;
	}
	return decomposeAffine(m);
}

/// @brief クリップごとの t=0 の姿勢とルート骨の起点を作る。
inline void computeClipStarts(AnimAsset& asset)
{
	std::vector<NodeTRS> pose(asset.nodes.size());
	for (auto& clip : asset.clips)
	{
		sampleClip(asset, clip, 0.0f, pose);
		clip.refPose = pose;
		if (asset.rootMotionNode >= 0)
		{
			const auto m = rootModelTransform(asset, pose[static_cast<std::size_t>(asset.rootMotionNode)]);
			clip.rootStart = {m.t, quatTwistY(m.r)};
		}
	}
}

} // namespace detail

/// @brief AnimAsset を組む。scene の meshes / materials は使わない。
[[nodiscard]] inline AnimAsset buildAnimAsset(render::GltfSceneData scene, const AnimAssetOptions& options = {},
                                              std::vector<std::string>* warnings = nullptr)
{
	std::vector<std::string> localWarnings;
	auto& warn = warnings != nullptr ? *warnings : localWarnings;

	AnimAsset asset;
	asset.nodes = std::move(scene.nodes);
	asset.skins = std::move(scene.skins);
	asset.evalOrder = detail::buildEvalOrder(asset.nodes);
	if (scene.animations.size() > static_cast<std::size_t>(kAnimMaxClips))
	{
		warn.push_back("クリップが多すぎる (先頭の " + std::to_string(kAnimMaxClips) + " 本だけ使う)");
		scene.animations.resize(static_cast<std::size_t>(kAnimMaxClips));
	}
	asset.clips.reserve(scene.animations.size());
	for (auto& a : scene.animations)
	{
		AnimClip clip;
		clip.name = std::move(a.name);
		clip.durationSec = a.durationSec;
		clip.channels = std::move(a.channels);
		asset.clips.push_back(std::move(clip));
	}

	asset.rootMotionNode = options.rootMotionBone.empty() ? detail::defaultRootMotionNode(asset.nodes, asset.skins)
	                                                      : asset.findNode(options.rootMotionBone);
	if (!options.rootMotionBone.empty() && asset.rootMotionNode < 0)
	{
		warn.push_back("rootMotion: 骨が無い: " + options.rootMotionBone);
	}
	asset.rootParentRest = detail::restParentTransform(asset, asset.rootMotionNode);
	detail::addMasks(asset, options.masks, warn);
	detail::addEvents(asset, options.events, warn);
	detail::addSockets(asset, options.sockets, warn);
	detail::computeClipStarts(asset);
	return asset;
}

} // namespace mitiru::animation
