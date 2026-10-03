#pragma once

/// @file FootPlacement.hpp
/// @brief 坂と段で足を地面に置く: 両足の下へレイを撃ち、低い側の足が届くだけ体を下げ、足の IK の依頼を作る。
/// @details アニメは y = 0 の平らな地面で作られている前提で、描画の配置 (instanceWorld) の原点がキャラの足元にある。
///          足ごとの地面の高さと腰を下げる量は FootPlacementState (GameMemory) に置き、1 秒あたりの速さで寄せる。
///          出すものは「下げた配置」と足 2 本ぶんの AnimIkRequest (ワールドの座標) だけで、DLL は同じ依頼を
///          applyIkRequests に、host は Screen::drawModelPose に渡す。腰を下げる代わりに配置ごと下げるので、
///          新しい IK の種類は要らない。レイは CollisionWorld に撃つので、同じフレームのうちに答えが出る。

#include <cstdint>
#include <type_traits>

#include <sgc/math/Mat4.hpp>

#include <mitiru/action/CollisionWorld.hpp>
#include <mitiru/animation/AnimIK.hpp>
#include <mitiru/animation/AnimIkRequest.hpp>
#include <mitiru/animation/AnimPose.hpp>

namespace mitiru::action
{

/// @brief 脚 1 本の骨 (AnimAsset::findNode の値)
struct FootLeg
{
	int hip = -1;
	int knee = -1;
	int ankle = -1;
};

struct FootPlacementConfig
{
	float rayUp = 0.5f;           ///< 足元からこの高さの上から撃つ (上がれる段の高さ)
	float rayDown = 0.6f;         ///< 足元からこの深さまで探す
	float maxPelvisDrop = 0.4f;   ///< 体を下げる量の上限 (m)
	float pelvisSpeed = 1.5f;     ///< 体を下げる量が 1 秒に動ける量 (m)。0 なら寄せずにすぐ合わせる
	float footSpeed = 3.0f;       ///< 足の高さが 1 秒に動ける量 (m)。0 なら寄せずにすぐ合わせる
	float weightSpeed = 5.0f;     ///< 地面を見失った足の IK を外す (見つけたら掛ける) 速さ (1 秒あたりの重み)。0 ならすぐ
	std::uint32_t mask = kAllLayers;
	bool alignToGround = true;    ///< 足裏を地面の傾きに沿わせる
};

/// @brief GameMemory に置く足の接地の状態 (48 byte)。高さはすべて足元 (配置の原点) から
struct FootPlacementState
{
	float pelvisOffset = 0.0f;              ///< 体を下げる量 (0 以下)
	float footOffset[2] = {0.0f, 0.0f};     ///< 足の下の地面の高さ
	float weight[2] = {0.0f, 0.0f};         ///< 足の IK の効き。地面の無い足は 0 へ寄せ、アニメのまま動かす
	std::uint8_t grounded[2] = {0, 0};      ///< 足の下に地面が見つかったか
	std::uint8_t pad[2] = {0, 0};
	Vec3 normal[2]{{0, 1, 0}, {0, 1, 0}};   ///< 足の下の地面の法線
};

static_assert(std::is_trivially_copyable_v<FootPlacementState>);

/// @brief 描くときの配置と、足 2 本の IK の依頼
struct FootPlacementPose
{
	sgc::Mat4f world;                         ///< 体を下げた配置。drawModelPose と applyIkRequests に渡す
	animation::AnimIkRequest ik[2];
};

namespace detail
{

[[nodiscard]] inline float moveToward(float value, float target, float maxStep) noexcept
{
	if (!(maxStep > 0.0f)) { return target; }
	const float d = target - value;
	return value + (d > maxStep ? maxStep : (d < -maxStep ? -maxStep : d));
}

[[nodiscard]] inline Vec3 matPosition(const sgc::Mat4f& m) noexcept { return {m.m[0][3], m.m[1][3], m.m[2][3]}; }

} // namespace detail

/// @brief 両足の下の地面を測り、state を dt 秒ぶん寄せる。pose は IK を掛ける前の評価済みの姿勢
inline void stepFootPlacement(FootPlacementState& state, const CollisionWorld& world, const animation::AnimPose& pose,
                              const sgc::Mat4f& instanceWorld, const FootLeg (&legs)[2], const FootPlacementConfig& cfg,
                              float dt) noexcept
{
	const Vec3 base = detail::matPosition(instanceWorld);
	float target[2] = {0.0f, 0.0f};
	for (int i = 0; i < 2; ++i)
	{
		const Vec3 ankle = animation::boneWorld(pose, legs[i].ankle, instanceWorld).transformPoint({0, 0, 0});
		const RayHit hit = world.raycast({ankle.x, base.y + cfg.rayUp, ankle.z}, {0, -1, 0}, cfg.rayUp + cfg.rayDown, cfg.mask);
		state.grounded[i] = hit.hit ? 1 : 0;
		state.normal[i] = hit.hit && hit.normal.y > 0.0f ? hit.normal : Vec3{0, 1, 0};
		target[i] = hit.hit ? clampf(hit.point.y - base.y, -cfg.rayDown, cfg.rayUp) : 0.0f;
	}
	const float drop = animation::footPelvisOffset(target[0], target[1], cfg.maxPelvisDrop);
	if (!(dt >= 0.0f)) { dt = 0.0f; }
	state.pelvisOffset = detail::moveToward(state.pelvisOffset, drop, cfg.pelvisSpeed * dt);
	for (int i = 0; i < 2; ++i)
	{
		state.footOffset[i] = detail::moveToward(state.footOffset[i], target[i], cfg.footSpeed * dt);
		state.weight[i] = detail::moveToward(state.weight[i], state.grounded[i] != 0 ? 1.0f : 0.0f, cfg.weightSpeed * dt);
	}
}

/// @brief state から、体を下げた配置と足の IK の依頼を作る (draw でも update でも同じ値になる)
[[nodiscard]] inline FootPlacementPose footPlacementPose(const FootPlacementState& state, const sgc::Mat4f& instanceWorld,
                                                         const FootLeg (&legs)[2], const FootPlacementConfig& cfg) noexcept
{
	FootPlacementPose out;
	out.world = sgc::Mat4f::translation({0.0f, state.pelvisOffset, 0.0f}) * instanceWorld;
	const float baseY = detail::matPosition(instanceWorld).y;
	const std::uint8_t flags = animation::kAnimIkWorldSpace | (cfg.alignToGround ? animation::kAnimIkKeepTip : std::uint8_t{0});
	for (int i = 0; i < 2; ++i)
	{
		out.ik[i] = animation::AnimIkRequest::foot(legs[i].hip, legs[i].knee, legs[i].ankle, baseY + state.footOffset[i],
		                                           state.normal[i], flags);
		out.ik[i].weight = state.weight[i];
	}
	return out;
}

} // namespace mitiru::action
