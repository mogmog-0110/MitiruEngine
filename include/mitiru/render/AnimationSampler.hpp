#pragma once

/// @file AnimationSampler.hpp
/// @brief glTF アニメーションクリップのポーズサンプリング。
/// @details クリップと絶対時間 (秒) から joint のワールドポーズ行列の列を組む純関数群。
///          GPU に依存せず、状態を持たない。同じ入力からは bit-exact に同じ出力を返す (決定論、軸②④)。
///          流れは samplePose → (blendPoses) → computeWorldPose → gatherJointWorld →
///          `Skinning.hpp::skinVertices` (DX12 は同じ式の compute) の順。gatherJointWorld は skin.joints 順への
///          gather だけを行う。inverseBind の乗算は skinVertices の内部で行うので、
///          ここで乗算すると二重にかかる (してはいけない)。
///          規約は sgc::Mat4f が行優先・列ベクトル (p' = M * p)、quaternion が xyzw。

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

/// @brief 局所 TRS から局所行列を組む (T * R * S。glTF 仕様の合成順)。
[[nodiscard]] inline sgc::Mat4f localMatrix(const NodeTRS& trs)
{
	return sgc::Mat4f::translation(trs.t) * quatToMat4(trs.r) * sgc::Mat4f::scaling(trs.s);
}

/// @brief ループ再生の時間の折返し。負の値も折返し、duration<=0 は 0 を返す。
[[nodiscard]] inline float wrapTime(float tSec, float durationSec)
{
	if (durationSec <= 0.0f) { return 0.0f; }
	float r = std::fmod(tSec, durationSec);
	if (r < 0.0f) { r += durationSec; }
	return r;
}

/// @brief glTF 仕様の cubic Hermite (in-tangent/value/out-tangent、区間長でスケール済み)。
///        u=0 で a、u=1 で b。成分ごとに独立適用 (quaternion は呼び出し側で正規化する)。
[[nodiscard]] inline sgc::Vec4f hermite(const sgc::Vec4f& a, const sgc::Vec4f& outTanA,
                                        const sgc::Vec4f& b, const sgc::Vec4f& inTanB,
                                        float u, float span)
{
	const float u2 = u * u;
	const float u3 = u2 * u;
	const float h00 = 2 * u3 - 3 * u2 + 1;
	const float h10 = u3 - 2 * u2 + u;
	const float h01 = -2 * u3 + 3 * u2;
	const float h11 = u3 - u2;
	const auto lerp1 = [&](float p0, float m0, float p1, float m1)
	{
		return h00 * p0 + h10 * span * m0 + h01 * p1 + h11 * span * m1;
	};
	return {lerp1(a.x, outTanA.x, b.x, inTanB.x), lerp1(a.y, outTanA.y, b.y, inTanB.y),
	        lerp1(a.z, outTanA.z, b.z, inTanB.z), lerp1(a.w, outTanA.w, b.w, inTanB.w)};
}

/// @brief チャンネルを時刻 t (wrap 済み) でサンプルする。
/// @details 端より外は端のキーへクランプする。STEP は直前のキーを保持する。CubicSpline は glTF 仕様の
///          Hermite (回転は補間後に正規化)。それ以外の Rotation は slerp、Translation/Scale
///          は成分ごとの lerp。空のチャンネルは identity 相当を返す。
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

	if (ch.interpolation == GltfAnimInterp::CubicSpline &&
	    idx < ch.outTangents.size() && (idx - 1) < ch.outTangents.size() && idx < ch.inTangents.size())
	{
		const auto v = hermite(a, ch.outTangents[idx - 1], b, ch.inTangents[idx], u, span);
		return (ch.path == GltfAnimPath::Rotation) ? quatNormalize(v) : v;
	}

	if (ch.path == GltfAnimPath::Rotation)
	{
		return quatSlerp(quatNormalize(a), quatNormalize(b), u);
	}
	return {a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u,
	        a.z + (b.z - a.z) * u, a.w + (b.w - a.w) * u};
}

/// @brief クリップを時刻 t でサンプルし、全ノードの局所 TRS を返す。
/// @details レストポーズ (nodes の TRS) を初期値にして、チャンネルが動かす要素だけを上書きする。
///          t には wrapTime 済みの値を渡すこと (この関数は折返さない)。
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
/// @details parent==-1 のルートから children を辿るので、ノードの並び順に依存しない。
///          循環や範囲外の children は無視する (訪問済みのノードは再訪しない)。
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
