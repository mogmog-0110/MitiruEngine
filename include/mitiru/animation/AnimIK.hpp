#pragma once

/// @file AnimIK.hpp
/// @brief 評価済みの姿勢に掛ける IK: 2 骨 (腕・脚)、向け (頭・視線)、足の接地。
/// @details 目標はモデル空間 (描画の配置行列を掛ける前) で渡す。ワールドの点は worldToModel で変える。
///          どの関数も局所回転を書き換えたあと updateModelPose でモデル行列を作り直すので、続けて呼べる。
///          地面の高さはゲームが自分で測って渡す (物理には触らない)。加減乗除と sqrt だけで決まる。

#include <algorithm>
#include <cmath>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>

#include <mitiru/animation/AnimAsset.hpp>
#include <mitiru/animation/AnimMath.hpp>
#include <mitiru/animation/AnimPose.hpp>

namespace mitiru::animation
{

[[nodiscard]] inline bool validNode(const AnimPose& pose, int node) noexcept
{
	return node >= 0 && static_cast<std::size_t>(node) < pose.model.size();
}

[[nodiscard]] inline sgc::Vec3f modelPosition(const AnimPose& pose, int node) noexcept
{
	if (!validNode(pose, node)) { return {0, 0, 0}; }
	const auto& m = pose.model[static_cast<std::size_t>(node)].m;
	return {m[0][3], m[1][3], m[2][3]};
}

[[nodiscard]] inline sgc::Vec4f modelRotation(const AnimPose& pose, int node) noexcept
{
	return validNode(pose, node) ? decomposeAffine(pose.model[static_cast<std::size_t>(node)]).r : kQuatIdentity;
}

/// @brief ワールドの点を配置行列 instanceWorld の逆でモデル空間へ移す。
[[nodiscard]] inline sgc::Vec3f worldToModel(const sgc::Mat4f& instanceWorld, const sgc::Vec3f& p) noexcept
{
	return instanceWorld.inversed().transformPoint(p);
}

/// @brief node のモデル空間の回転を rotation にする局所回転を書く (モデル行列は作り直さない)。
inline void setModelRotation(const AnimAsset& asset, AnimPose& pose, int node, const sgc::Vec4f& rotation)
{
	const int parent = asset.nodes[static_cast<std::size_t>(node)].parent;
	const auto parentR = modelRotation(pose, parent);
	pose.local[static_cast<std::size_t>(node)].r = quatNormalize(quatMul(quatConj(parentR), rotation));
}

/// @brief node をモデル空間で delta だけ動かす (腰を下げる等)。モデル行列も作り直す。
inline void translateNodeModel(const AnimAsset& asset, AnimPose& pose, int node, const sgc::Vec3f& delta)
{
	if (!validNode(pose, node)) { return; }
	const int parent = asset.nodes[static_cast<std::size_t>(node)].parent;
	const sgc::Mat4f parentModel = validNode(pose, parent) ? pose.model[static_cast<std::size_t>(parent)]
	                                                       : sgc::Mat4f::identity();
	pose.local[static_cast<std::size_t>(node)].t += parentModel.inversed().transformVector(delta);
	updateModelPose(asset, pose);
}

struct TwoBoneIK
{
	int root = -1;                  ///< 肩 / 股関節
	int mid = -1;                   ///< 肘 / 膝
	int tip = -1;                   ///< 手首 / 足首
	sgc::Vec3f target{0, 0, 0};     ///< tip を置きたい点 (モデル空間)
	sgc::Vec3f pole{0, 0, 0};       ///< 肘 / 膝を向けたい点 (モデル空間)。usePole が false なら今の曲げ方向
	bool usePole = false;
	bool keepTipRotation = true;    ///< tip のモデル空間の向きを保つ (足裏や手の向きが親の回転に引きずられない)
	float weight = 1.0f;
};

namespace detail
{

/// @brief n に直交する曲げ方向。hint が n と平行なら任意の直交方向。
[[nodiscard]] inline sgc::Vec3f bendDirection(const sgc::Vec3f& n, const sgc::Vec3f& hint)
{
	sgc::Vec3f perp = hint - n * hint.dot(n);
	if (perp.lengthSquared() < 1e-12f)
	{
		perp = sgc::Vec3f{0, 0, 1} - n * n.z;
		if (perp.lengthSquared() < 1e-12f) { perp = sgc::Vec3f{1, 0, 0} - n * n.x; }
	}
	return perp.normalized();
}

/// @brief node のモデル空間の方向 from を to へ回す (局所回転を書き、モデル行列を作り直す)。
inline void rotateBoneToward(const AnimAsset& asset, AnimPose& pose, int node, const sgc::Vec3f& from,
                             const sgc::Vec3f& to)
{
	if (from.lengthSquared() < 1e-12f || to.lengthSquared() < 1e-12f) { return; }
	const auto q = quatFromTo(from.normalized(), to.normalized());
	setModelRotation(asset, pose, node, quatMul(q, modelRotation(pose, node)));
	updateModelPose(asset, pose);
}

} // namespace detail

/// @brief 2 骨 IK (余弦定理)。届かない目標は伸び切った向きで止める。
/// @return tip が目標に届いたか (weight 1 のとき、距離 1e-3 × 骨の長さ以内)。
inline bool solveTwoBoneIK(const AnimAsset& asset, AnimPose& pose, const TwoBoneIK& ik)
{
	if (!validNode(pose, ik.root) || !validNode(pose, ik.mid) || !validNode(pose, ik.tip)) { return false; }
	const float weight = std::clamp(ik.weight, 0.0f, 1.0f);
	if (!(weight > 0.0f)) { return false; }
	const auto rootLocal0 = pose.local[static_cast<std::size_t>(ik.root)].r;
	const auto midLocal0 = pose.local[static_cast<std::size_t>(ik.mid)].r;
	const auto tipModel0 = modelRotation(pose, ik.tip);

	const auto pa = modelPosition(pose, ik.root);
	const auto pb = modelPosition(pose, ik.mid);
	const auto pc = modelPosition(pose, ik.tip);
	const float a = (pb - pa).length();
	const float b = (pc - pb).length();
	const auto toTarget = ik.target - pa;
	const float dist = toTarget.length();
	if (a < 1e-6f || b < 1e-6f || !(dist >= 1e-6f) || !std::isfinite(dist)) { return false; }

	const float eps = 1e-4f * (a + b);
	const float c = std::clamp(dist, std::fabs(a - b) + eps, a + b - eps);
	const auto n = toTarget / dist;
	const auto bend = detail::bendDirection(n, ik.usePole ? ik.pole - pa : pb - pa);
	const float x = (a * a - b * b + c * c) / (2.0f * c);
	const float h = std::sqrt(std::max(a * a - x * x, 0.0f));
	const auto midGoal = pa + n * x + bend * h;
	const auto tipGoal = pa + n * c;

	detail::rotateBoneToward(asset, pose, ik.root, pb - pa, midGoal - pa);
	detail::rotateBoneToward(asset, pose, ik.mid, modelPosition(pose, ik.tip) - modelPosition(pose, ik.mid),
	                         tipGoal - modelPosition(pose, ik.mid));
	if (weight < 1.0f)
	{
		auto& rootR = pose.local[static_cast<std::size_t>(ik.root)].r;
		auto& midR = pose.local[static_cast<std::size_t>(ik.mid)].r;
		rootR = quatSlerp(rootLocal0, rootR, weight);
		midR = quatSlerp(midLocal0, midR, weight);
		updateModelPose(asset, pose);
	}
	if (ik.keepTipRotation)
	{
		setModelRotation(asset, pose, ik.tip, tipModel0);
		updateModelPose(asset, pose);
	}
	return (modelPosition(pose, ik.tip) - ik.target).length() <= 1e-3f * (a + b);
}

struct AimIK
{
	int node = -1;
	sgc::Vec3f aimAxis{0, 0, 1};     ///< node の局所空間で「前」に当たる単位ベクトル
	sgc::Vec3f target{0, 0, 0};      ///< 向けたい点 (モデル空間)
	float maxAngleCos = -1.0f;       ///< 今の向きから回せる角度の上限 (cos)。-1 なら制限なし
	float weight = 1.0f;
};

/// @brief node の aimAxis を target へ向ける (頭を向ける、銃口を向ける)。
inline void solveAimIK(const AnimAsset& asset, AnimPose& pose, const AimIK& ik)
{
	if (!validNode(pose, ik.node)) { return; }
	const float weight = std::clamp(ik.weight, 0.0f, 1.0f);
	if (!(weight > 0.0f)) { return; }
	const auto modelR = modelRotation(pose, ik.node);
	const auto cur = quatRotate(modelR, ik.aimAxis).normalized();
	const auto toTarget = ik.target - modelPosition(pose, ik.node);
	if (toTarget.lengthSquared() < 1e-12f) { return; }
	auto want = toTarget.normalized();
	const float d = cur.dot(want);
	if (ik.maxAngleCos > -1.0f && d < ik.maxAngleCos)
	{
		const float c = std::clamp(ik.maxAngleCos, -1.0f, 1.0f);
		const auto perp = detail::bendDirection(cur, want);
		want = (cur * c + perp * std::sqrt(std::max(1.0f - c * c, 0.0f))).normalized();
	}
	const auto local0 = pose.local[static_cast<std::size_t>(ik.node)].r;
	setModelRotation(asset, pose, ik.node, quatMul(quatFromTo(cur, want), modelR));
	auto& r = pose.local[static_cast<std::size_t>(ik.node)].r;
	r = quatSlerp(local0, r, weight);
	updateModelPose(asset, pose);
}

struct FootIK
{
	int hip = -1;
	int knee = -1;
	int ankle = -1;
	float groundY = 0.0f;               ///< 足の下の地面の高さ (モデル空間)。クリップは y=0 を地面として作られている前提
	sgc::Vec3f groundNormal{0, 1, 0};   ///< 地面の法線 (モデル空間)。平らな地面に対するアニメの足首の向きを、この傾きぶん回す
	bool alignToGround = true;
	float weight = 1.0f;
};

/// @brief 足首を「アニメの高さ + 地面の高さ」へ置き、足裏を地面の傾きに沿わせる。
/// @return 足首が目標に届いたか。届かないときは腰を下げる (footPelvisOffset) と届く。
inline bool solveFootIK(const AnimAsset& asset, AnimPose& pose, const FootIK& ik)
{
	if (!validNode(pose, ik.ankle)) { return false; }
	TwoBoneIK leg;
	leg.root = ik.hip;
	leg.mid = ik.knee;
	leg.tip = ik.ankle;
	leg.target = modelPosition(pose, ik.ankle) + sgc::Vec3f{0, ik.groundY, 0};
	leg.weight = ik.weight;
	const bool reached = solveTwoBoneIK(asset, pose, leg);
	if (ik.alignToGround && ik.groundNormal.lengthSquared() > 1e-12f)
	{
		const auto tilt = quatFromTo({0, 1, 0}, ik.groundNormal.normalized());
		const auto r0 = modelRotation(pose, ik.ankle);
		setModelRotation(asset, pose, ik.ankle, quatSlerp(r0, quatMul(tilt, r0), std::clamp(ik.weight, 0.0f, 1.0f)));
		updateModelPose(asset, pose);
	}
	return reached;
}

/// @brief 左右の足の地面の高さから、腰を下げる量 (0 以下) を出す。低い側の足が届くように下げる。
[[nodiscard]] inline float footPelvisOffset(float groundLeft, float groundRight, float maxDrop) noexcept
{
	return std::clamp(std::min({groundLeft, groundRight, 0.0f}), -std::fabs(maxDrop), 0.0f);
}

} // namespace mitiru::animation
