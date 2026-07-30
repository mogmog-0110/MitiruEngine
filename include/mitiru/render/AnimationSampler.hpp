#pragma once

/// @file AnimationSampler.hpp
/// @brief glTF アニメーションクリップのポーズサンプリング (ADR 0028)。
/// @details クリップと絶対時間 (秒) から joint のワールドポーズ行列列を組む純関数群。
///          GPU 非依存・状態なし — 同一入力は bit-exact に同一出力 (決定論、軸②④)。
///          流れ: samplePose → (blendPoses) → computeWorldPose → gatherJointWorld →
///          `Skinning.hpp::skinVertices` へ。gatherJointWorld は skin.joints 順への
///          gather のみを行う。inverseBind の乗算は skinVertices 内部の責務であり、
///          ここで乗算すると二重適用になる (してはいけない)。
///          規約: sgc::Mat4f は行優先・列ベクトル (p' = M * p)。quaternion は xyzw。

#include <algorithm>
#include <cmath>
#include <vector>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/math/Vec4.hpp>

#include <mitiru/render/GltfTypes.hpp>

namespace mitiru::render
{

/// @brief 1 ノードの局所 TRS ポーズ。rotation は quaternion xyzw。
struct NodeTRS
{
	sgc::Vec3f t{0, 0, 0};
	sgc::Vec4f r{0, 0, 0, 1};
	sgc::Vec3f s{1, 1, 1};
};

/// @brief quaternion を正規化する (ゼロ長は identity を返す)。
[[nodiscard]] inline sgc::Vec4f quatNormalize(const sgc::Vec4f& q)
{
	const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
	if (len <= 1e-8f) { return {0, 0, 0, 1}; }
	return {q.x / len, q.y / len, q.z / len, q.w / len};
}

/// @brief 最短弧の球面線形補間。u=0 で a、u=1 で b。
[[nodiscard]] inline sgc::Vec4f quatSlerp(const sgc::Vec4f& a, const sgc::Vec4f& b, float u)
{
	sgc::Vec4f q2 = b;
	float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
	if (dot < 0.0f)  // 最短弧: 反対半球なら符号反転
	{
		q2 = {-b.x, -b.y, -b.z, -b.w};
		dot = -dot;
	}
	if (dot > 0.9995f)  // ほぼ同一 → nlerp で数値安定
	{
		return quatNormalize({a.x + (q2.x - a.x) * u, a.y + (q2.y - a.y) * u,
		                      a.z + (q2.z - a.z) * u, a.w + (q2.w - a.w) * u});
	}
	const float theta = std::acos(std::clamp(dot, -1.0f, 1.0f));
	const float sinTheta = std::sin(theta);
	const float wa = std::sin((1.0f - u) * theta) / sinTheta;
	const float wb = std::sin(u * theta) / sinTheta;
	return {a.x * wa + q2.x * wb, a.y * wa + q2.y * wb,
	        a.z * wa + q2.z * wb, a.w * wa + q2.w * wb};
}

/// @brief 単位 quaternion (xyzw) を回転行列へ変換する (行優先・列ベクトル規約)。
[[nodiscard]] inline sgc::Mat4f quatToMat4(const sgc::Vec4f& q)
{
	const auto n = quatNormalize(q);
	const float x = n.x, y = n.y, z = n.z, w = n.w;
	return {
		1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),     0,
		2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),     0,
		2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y), 0,
		0,                       0,                       0,                       1,
	};
}

/// @brief 局所 TRS から局所行列を組む (T * R * S — glTF 仕様の合成順)。
[[nodiscard]] inline sgc::Mat4f localMatrix(const NodeTRS& trs)
{
	return sgc::Mat4f::translation(trs.t) * quatToMat4(trs.r) * sgc::Mat4f::scaling(trs.s);
}

/// @brief ループ再生の時間折返し。負値も折返し、duration<=0 は 0。
[[nodiscard]] inline float wrapTime(float tSec, float durationSec)
{
	if (durationSec <= 0.0f) { return 0.0f; }
	float r = std::fmod(tSec, durationSec);
	if (r < 0.0f) { r += durationSec; }
	return r;
}

/// @brief チャンネルを時刻 t (wrap 済み) でサンプルする。
/// @details 端の外は端キーへクランプ。STEP は直前キーを保持。Rotation は slerp、
///          Translation/Scale は成分 lerp。空チャンネルは identity 相当を返す。
[[nodiscard]] inline sgc::Vec4f sampleChannel(const GltfAnimationChannel& ch, float t)
{
	if (ch.times.empty() || ch.values.empty())
	{
		return (ch.path == GltfAnimPath::Rotation) ? sgc::Vec4f{0, 0, 0, 1}
		                                           : sgc::Vec4f{0, 0, 0, 0};
	}
	const auto it = std::upper_bound(ch.times.begin(), ch.times.end(), t);
	const auto idx = static_cast<std::size_t>(it - ch.times.begin());
	if (idx == 0) { return ch.values.front(); }
	if (idx >= ch.times.size()) { return ch.values.back(); }
	if (ch.interpolation == GltfAnimInterp::Step) { return ch.values[idx - 1]; }

	const float t0 = ch.times[idx - 1];
	const float t1 = ch.times[idx];
	const float span = t1 - t0;
	const float u = (span > 1e-8f) ? (t - t0) / span : 0.0f;
	const auto& a = ch.values[idx - 1];
	const auto& b = ch.values[idx];
	if (ch.path == GltfAnimPath::Rotation)
	{
		return quatSlerp(quatNormalize(a), quatNormalize(b), u);
	}
	return {a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u,
	        a.z + (b.z - a.z) * u, a.w + (b.w - a.w) * u};
}

/// @brief クリップを時刻 t でサンプルし、全ノードの局所 TRS を返す。
/// @details レストポーズ (nodes の TRS) を初期値に、チャンネルが動かす要素だけ上書き。
///          t は wrapTime 済みを渡すこと (本関数は折返さない)。
[[nodiscard]] inline std::vector<NodeTRS> samplePose(
	const std::vector<GltfNode>& nodes, const GltfAnimationClip& clip, float t)
{
	std::vector<NodeTRS> pose(nodes.size());
	for (std::size_t i = 0; i < nodes.size(); ++i)
	{
		pose[i] = {nodes[i].translation, nodes[i].rotation, nodes[i].scale};
	}
	for (const auto& ch : clip.channels)
	{
		if (ch.nodeIndex < 0 || static_cast<std::size_t>(ch.nodeIndex) >= pose.size())
		{
			continue;
		}
		const auto v = sampleChannel(ch, t);
		auto& p = pose[static_cast<std::size_t>(ch.nodeIndex)];
		switch (ch.path)
		{
		case GltfAnimPath::Translation: p.t = {v.x, v.y, v.z}; break;
		case GltfAnimPath::Rotation:    p.r = v; break;
		case GltfAnimPath::Scale:       p.s = {v.x, v.y, v.z}; break;
		}
	}
	return pose;
}

/// @brief 2 ポーズを混ぜる (crossfade)。mix=0 で a、1 で b。T/S は lerp、R は slerp。
[[nodiscard]] inline std::vector<NodeTRS> blendPoses(
	const std::vector<NodeTRS>& a, const std::vector<NodeTRS>& b, float mix)
{
	if (a.size() != b.size()) { return a; }
	const float u = std::clamp(mix, 0.0f, 1.0f);
	std::vector<NodeTRS> out(a.size());
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		out[i].t = {a[i].t.x + (b[i].t.x - a[i].t.x) * u,
		            a[i].t.y + (b[i].t.y - a[i].t.y) * u,
		            a[i].t.z + (b[i].t.z - a[i].t.z) * u};
		out[i].r = quatSlerp(quatNormalize(a[i].r), quatNormalize(b[i].r), u);
		out[i].s = {a[i].s.x + (b[i].s.x - a[i].s.x) * u,
		            a[i].s.y + (b[i].s.y - a[i].s.y) * u,
		            a[i].s.z + (b[i].s.z - a[i].s.z) * u};
	}
	return out;
}

/// @brief 局所ポーズから全ノードのワールドポーズ行列を組む。
/// @details parent==-1 のルートから children を辿るのでノードの並び順に依存しない。
///          循環や範囲外 children は無視 (訪問済みは再訪しない)。
[[nodiscard]] inline std::vector<sgc::Mat4f> computeWorldPose(
	const std::vector<GltfNode>& nodes, const std::vector<NodeTRS>& localPose)
{
	std::vector<sgc::Mat4f> world(nodes.size(), sgc::Mat4f::identity());
	if (localPose.size() != nodes.size()) { return world; }

	std::vector<char> visited(nodes.size(), 0);
	std::vector<int> stack;
	stack.reserve(nodes.size());
	for (std::size_t i = 0; i < nodes.size(); ++i)
	{
		if (nodes[i].parent == -1) { stack.push_back(static_cast<int>(i)); }
	}
	while (!stack.empty())
	{
		const int idx = stack.back();
		stack.pop_back();
		const auto ui = static_cast<std::size_t>(idx);
		if (visited[ui] != 0) { continue; }
		visited[ui] = 1;

		const int parent = nodes[ui].parent;
		const auto local = localMatrix(localPose[ui]);
		world[ui] = (parent >= 0 && static_cast<std::size_t>(parent) < world.size())
		                ? world[static_cast<std::size_t>(parent)] * local
		                : local;
		for (const int child : nodes[ui].children)
		{
			if (child >= 0 && static_cast<std::size_t>(child) < nodes.size() &&
			    visited[static_cast<std::size_t>(child)] == 0)
			{
				stack.push_back(child);
			}
		}
	}
	return world;
}

/// @brief ノード基準のワールドポーズを skin.joints 順へ集める (gather のみ)。
/// @details 戻り値はそのまま `skinVertices` の worldPose 引数へ渡す。
///          inverseBind はここで乗算しない (skinVertices が内部で乗算する)。
[[nodiscard]] inline std::vector<sgc::Mat4f> gatherJointWorld(
	const std::vector<sgc::Mat4f>& worldPoseByNode, const GltfSkinData& skin)
{
	std::vector<sgc::Mat4f> out(skin.joints.size(), sgc::Mat4f::identity());
	for (std::size_t j = 0; j < skin.joints.size(); ++j)
	{
		const int node = skin.joints[j];
		if (node >= 0 && static_cast<std::size_t>(node) < worldPoseByNode.size())
		{
			out[j] = worldPoseByNode[static_cast<std::size_t>(node)];
		}
	}
	return out;
}

} // namespace mitiru::render
