#pragma once

/// @file AnimIkRequest.hpp
/// @brief IK の依頼 1 件を 48 byte の POD にした形 (ABI v48)。Screen::drawModelPose が境界の向こうへ渡す。
/// @details host は描画のときに、ゲーム DLL は当たり判定のときに、同じ applyIkRequests を同じ姿勢へ掛ける。
///          どちらも AnimIK.hpp の解き方そのままなので、両者の姿勢は一致する。

#include <cstdint>
#include <type_traits>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>

#include <mitiru/animation/AnimIK.hpp>

namespace mitiru::animation
{

inline constexpr std::uint8_t kAnimIkTwoBone = 1;   ///< node = 根元 / 中間 / 先、target、aux = pole
inline constexpr std::uint8_t kAnimIkAim     = 2;   ///< node[0]、target、aux = 骨の局所の前向き (restLocalAxis)、param = 回せる角度の cos
inline constexpr std::uint8_t kAnimIkFoot    = 3;   ///< node = 股 / 膝 / 足首、target.y = 地面の高さ、aux = 地面の法線

inline constexpr std::uint8_t kAnimIkUsePole    = 1u << 0;  ///< TwoBone: aux を pole に使う
inline constexpr std::uint8_t kAnimIkKeepTip    = 1u << 1;  ///< TwoBone: 先の向きを保つ / Foot: 地面の傾きに沿わせる
inline constexpr std::uint8_t kAnimIkWorldSpace = 1u << 2;  ///< target と pole / 地面の法線をワールドで書いた (描画の配置行列の逆で戻す)

struct AnimIkRequest
{
	std::uint8_t  kind = 0;
	std::uint8_t  flags = kAnimIkKeepTip;
	std::uint8_t  _pad[2] = {};
	std::int16_t  node[3] = {-1, -1, -1};
	std::uint16_t _pad2 = 0;
	float         target[3] = {0.0f, 0.0f, 0.0f};
	float         aux[3] = {0.0f, 0.0f, 0.0f};
	float         weight = 1.0f;
	float         param = -1.0f;
	float         _reserved = 0.0f;
};

static_assert(sizeof(AnimIkRequest) == 48 && std::is_trivially_copyable_v<AnimIkRequest>, "AnimIkRequest wire size 固定 (v48)");

namespace detail
{

[[nodiscard]] inline sgc::Vec3f ikVec(const float (&v)[3]) noexcept { return {v[0], v[1], v[2]}; }

/// target を点、aux を向きとしてモデル空間へ戻す。
inline void ikToModel(const AnimIkRequest& r, const sgc::Mat4f& world, sgc::Vec3f& target, sgc::Vec3f& aux) noexcept
{
	target = ikVec(r.target);
	aux = ikVec(r.aux);
	if ((r.flags & kAnimIkWorldSpace) == 0) { return; }
	const sgc::Mat4f inv = world.inversed();
	target = inv.transformPoint(target);
	// Aim の aux は骨の局所の軸なので配置行列に関係しない
	if (r.kind == kAnimIkTwoBone) { aux = inv.transformPoint(aux); }
	else if (r.kind == kAnimIkFoot) { aux = inv.transformVector(aux); }
}

inline void applyIkRequest(const AnimAsset& asset, AnimPose& pose, const sgc::Mat4f& world, const AnimIkRequest& r)
{
	sgc::Vec3f target, aux;
	ikToModel(r, world, target, aux);
	if (r.kind == kAnimIkTwoBone)
	{
		TwoBoneIK ik{r.node[0], r.node[1], r.node[2], target, aux, (r.flags & kAnimIkUsePole) != 0,
		             (r.flags & kAnimIkKeepTip) != 0, r.weight};
		(void)solveTwoBoneIK(asset, pose, ik);
	}
	else if (r.kind == kAnimIkAim)
	{
		const sgc::Vec3f axis = (aux.lengthSquared() > 1e-12f) ? aux.normalized() : sgc::Vec3f{0, 0, 1};
		solveAimIK(asset, pose, AimIK{r.node[0], axis, target, r.param, r.weight});
	}
	else if (r.kind == kAnimIkFoot)
	{
		FootIK ik{r.node[0], r.node[1], r.node[2], target.y, aux, (r.flags & kAnimIkKeepTip) != 0, r.weight};
		(void)solveFootIK(asset, pose, ik);
	}
}

}  // namespace detail

/// @brief レスト姿勢でモデル空間の modelDir を向いている、node の局所の軸 (Aim の aux に渡す)。読み込みの時に 1 回求める
[[nodiscard]] inline sgc::Vec3f restLocalAxis(const AnimAsset& asset, int node, const sgc::Vec3f& modelDir)
{
	AnimPose rest;
	evaluatePose(asset, AnimPoseParams{}, rest);
	if (!validNode(rest, node)) { return modelDir; }
	return quatRotate(quatConj(modelRotation(rest, node)), modelDir).normalized();
}

/// @brief 評価済みの姿勢に IK の依頼を順に掛ける。world は描画に渡すのと同じ配置行列。
inline void applyIkRequests(const AnimAsset& asset, AnimPose& pose, const sgc::Mat4f& world,
                            const AnimIkRequest* requests, int count)
{
	if (requests == nullptr) { return; }
	for (int i = 0; i < count; ++i) { detail::applyIkRequest(asset, pose, world, requests[i]); }
}

}  // namespace mitiru::animation
