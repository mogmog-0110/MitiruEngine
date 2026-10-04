#pragma once

/// @file AnimRetarget_impl.hpp
/// @brief AnimRetarget.hpp の続き: 対応表から写し方を組み、クリップを時刻ごとに写す。

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/animation/AnimRetarget.hpp>

namespace mitiru::animation
{

namespace detail
{

/// 元の骨格をこちらの向きへ回す行列。どちらかの骨格に向きが書いてあればそれで決め、無ければ関節の位置から推す
[[nodiscard]] inline sgc::Mat4f retargetFrame(const RetargetRig& rig, const AnimBoneMap& map, std::vector<std::string>& warnings)
{
	if (map.source.declared || map.target.declared) { return axesRotation(map.source, map.target); }
	const auto& r = rig.pairs[static_cast<std::size_t>(rig.root)];
	std::vector<sgc::Vec3f> src, dst;
	for (const auto& p : rig.pairs)
	{
		src.push_back(matPos(rig.srcRest[static_cast<std::size_t>(p.src)]) - matPos(rig.srcRest[static_cast<std::size_t>(r.src)]));
		dst.push_back(matPos(rig.dstRest[static_cast<std::size_t>(p.dst)]) - matPos(rig.dstRest[static_cast<std::size_t>(r.dst)]));
	}
	const sgc::Mat4f m = detectAxesRotation(src, dst);
	if (m.m[0][0] != 1.0f || m.m[1][1] != 1.0f || m.m[2][2] != 1.0f)
	{
		warnings.push_back("retarget: 骨格の前と上の軸が違うので、元の骨格を回して写す (forward と up を書けば推さない)");
	}
	return m;
}

[[nodiscard]] inline std::optional<RetargetRig> buildRetargetRig(const AnimAsset& src, const std::vector<render::GltfNode>& dst,
                                                                 const AnimBoneMap& map, std::vector<std::string>& warnings)
{
	RetargetRig rig;
	rig.pairs = resolvePairs(map, src.nodes, dst, warnings);
	if (rig.pairs.empty())
	{
		warnings.push_back("retarget: 両方の骨格にある骨の対応が 1 つも無い");
		return std::nullopt;
	}
	rig.dstOrder = buildEvalOrder(dst);
	rig.srcRest = restModels(src.nodes, src.evalOrder);
	rig.dstRest = restModels(dst, rig.dstOrder);
	rig.dstMapped.assign(dst.size(), -1);
	for (std::size_t k = 0; k < rig.pairs.size(); ++k) { rig.dstMapped[static_cast<std::size_t>(rig.pairs[k].dst)] = static_cast<int>(k); }
	rig.root = 0;
	if (!map.root.empty())
	{
		const int node = src.findNode(map.root);
		const auto it = std::find_if(rig.pairs.begin(), rig.pairs.end(), [&](const RetargetPair& p) { return p.src == node; });
		if (it != rig.pairs.end()) { rig.root = static_cast<int>(it - rig.pairs.begin()); }
		else { warnings.push_back("retarget: root の骨が対応に無い: " + map.root); }
	}
	rig.srcFrame = retargetFrame(rig, map, warnings);
	for (auto& m : rig.srcRest) { m = rig.srcFrame * m; }
	rig.up = map.target.up;
	computeRestCorrection(rig, src.nodes, dst);
	rig.scale = map.scale > 0.0f ? map.scale : retargetScale(rig);
	return rig;
}

/// クリップのどれかのチャンネルにキーがある時刻を昇順に重ねずに並べる
[[nodiscard]] inline std::vector<float> clipKeyTimes(const AnimClip& clip)
{
	std::vector<float> times;
	for (const auto& ch : clip.channels) { times.insert(times.end(), ch.times.begin(), ch.times.end()); }
	std::sort(times.begin(), times.end());
	times.erase(std::unique(times.begin(), times.end()), times.end());
	if (times.empty()) { times.push_back(0.0f); }
	return times;
}

/// 元の姿勢 1 つを、対応した骨の局所の回転 (local) とルートの局所の位置 (rootT) にする
inline void retargetSample(const RetargetRig& rig, const std::vector<render::GltfNode>& dst, const AnimPose& srcPose,
                           std::vector<sgc::Vec4f>& modelRot, std::vector<sgc::Vec4f>& local, sgc::Vec3f& rootT)
{
	for (const int i : rig.dstOrder)
	{
		const auto ui = static_cast<std::size_t>(i);
		const int parent = dst[ui].parent;
		const sgc::Vec4f pr = parent >= 0 ? modelRot[static_cast<std::size_t>(parent)] : kQuatIdentity;
		const int k = rig.dstMapped[ui];
		if (k < 0)
		{
			modelRot[ui] = quatNormalize(quatMul(pr, dst[ui].rotation));
			continue;
		}
		const auto& p = rig.pairs[static_cast<std::size_t>(k)];
		const auto qs = decomposeAffine(rig.srcFrame * srcPose.model[static_cast<std::size_t>(p.src)]).r;
		modelRot[ui] = quatNormalize(quatMul(quatMul(qs, p.srcRestInv), p.dstRef));
		local[static_cast<std::size_t>(k)] = quatNormalize(quatMul(quatConj(pr), modelRot[ui]));
	}
	const auto& r = rig.pairs[static_cast<std::size_t>(rig.root)];
	const auto moved = rig.srcFrame.transformPoint(matPos(srcPose.model[static_cast<std::size_t>(r.src)])) -
	                   matPos(rig.srcRest[static_cast<std::size_t>(r.src)]);
	const auto pt = matPos(rig.dstRest[static_cast<std::size_t>(r.dst)]) + moved * rig.scale;
	const int parent = dst[static_cast<std::size_t>(r.dst)].parent;
	rootT = parent >= 0 ? rig.dstRest[static_cast<std::size_t>(parent)].inversed().transformPoint(pt) : pt;
}

/// 前のキーと内積が負なら符号を返す (補間が遠回りしない)
inline void pushRotationKey(render::GltfAnimationChannel& ch, const sgc::Vec4f& q)
{
	const bool flip = !ch.values.empty() && quatDot(ch.values.back(), q) < 0.0f;
	ch.values.push_back(flip ? sgc::Vec4f{-q.x, -q.y, -q.z, -q.w} : q);
}

[[nodiscard]] inline render::GltfAnimationClip retargetClip(const RetargetRig& rig, const AnimAsset& src, const AnimClip& clip,
                                                            const std::vector<render::GltfNode>& dst, std::string name)
{
	const auto times = clipKeyTimes(clip);
	render::GltfAnimationClip out;
	out.name = std::move(name);
	out.durationSec = clip.durationSec;
	for (const auto& p : rig.pairs)
	{
		render::GltfAnimationChannel ch;
		ch.nodeIndex = p.dst;
		ch.path = render::GltfAnimPath::Rotation;
		ch.times = times;
		out.channels.push_back(std::move(ch));
	}
	render::GltfAnimationChannel move;
	move.nodeIndex = rig.pairs[static_cast<std::size_t>(rig.root)].dst;
	move.path = render::GltfAnimPath::Translation;
	move.times = times;
	AnimPose pose;
	pose.resize(src);
	std::vector<sgc::Vec4f> modelRot(dst.size(), kQuatIdentity);
	std::vector<sgc::Vec4f> local(rig.pairs.size(), kQuatIdentity);
	for (const float t : times)
	{
		sampleClip(src, clip, t, pose.local);
		updateModelPose(src, pose);
		sgc::Vec3f rootT{};
		retargetSample(rig, dst, pose, modelRot, local, rootT);
		for (std::size_t k = 0; k < rig.pairs.size(); ++k) { pushRotationKey(out.channels[k], local[k]); }
		move.values.push_back({rootT.x, rootT.y, rootT.z, 0.0f});
	}
	out.channels.push_back(std::move(move));
	return out;
}

} // namespace detail

/// @brief source のクリップを target の骨格へ写し、target.animations の末尾に足す。戻り値は足した本数
/// @param clips 写すクリップの名前。空なら全部
/// @param prefix 足すクリップの名前の頭に付ける文字列。target に同じ名前のクリップがあれば足さずに warnings へ
inline int appendRetargetedClips(render::GltfSceneData& target, const render::GltfSceneData& source, const AnimBoneMap& map,
                                 const std::vector<std::string>& clips, std::string_view prefix, std::vector<std::string>& warnings)
{
	render::GltfSceneData rigOnly;
	rigOnly.nodes = source.nodes;
	rigOnly.skins = source.skins;
	rigOnly.animations = source.animations;
	const AnimAsset src = buildAnimAsset(std::move(rigOnly));
	const auto rig = detail::buildRetargetRig(src, target.nodes, map, warnings);
	if (!rig) { return 0; }
	for (const auto& want : clips)
	{
		if (src.findClip(want) < 0) { warnings.push_back("retarget: 元のファイルにクリップが無い: " + want); }
	}
	int added = 0;
	for (const auto& clip : src.clips)
	{
		if (!clips.empty() && std::find(clips.begin(), clips.end(), clip.name) == clips.end()) { continue; }
		const std::string name = std::string(prefix) + clip.name;
		const bool taken = std::any_of(target.animations.begin(), target.animations.end(),
		                               [&](const render::GltfAnimationClip& c) { return c.name == name; });
		if (taken)
		{
			warnings.push_back("retarget: 同じ名前のクリップがあるので足さない: " + name);
			continue;
		}
		target.animations.push_back(detail::retargetClip(*rig, src, clip, target.nodes, name));
		++added;
	}
	return added;
}

} // namespace mitiru::animation
