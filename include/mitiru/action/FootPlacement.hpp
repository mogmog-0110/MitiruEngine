#pragma once

/// @file FootPlacement.hpp
/// @brief 坂と段で足を地面に置く: 両足の下へレイを撃ち、低い側の足が届くだけ体を下げ、足の IK の依頼を作る。
///        クリップが足を着いている間は、その足首をワールドの 1 点に留めて滑らせない。
/// @details アニメは y = 0 の平らな地面で作られている前提で、描画の配置 (instanceWorld) の原点がキャラの足元にある。
///          足ごとの地面の高さ・腰を下げる量・留めた点は FootPlacementState (GameMemory) に置き、1 秒あたりの速さで寄せる。
///          出すものは「下げた配置」と AnimIkRequest (ワールドの座標) だけで、DLL は同じ依頼を
///          applyIkRequests に、host は Screen::drawModelPose に渡す。腰を下げる代わりに配置ごと下げ、足を留めるのは
///          2 骨の IK なので、新しい IK の種類は要らない。レイは CollisionWorld に撃つので、同じフレームのうちに答えが出る。
///          足が着いているかはクリップの足首の高さで決める (glTF には接地の曲線が無い)。足首がレスト姿勢の高さから
///          lockRise 以内に下りたら着き、unlockRise より上がったら離れる。

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
	float ankleHeight = -1.0f;   ///< レスト姿勢の足首の高さ (配置の原点から m)。負なら足を留めない。footLeg() が入れる
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
	bool lockFeet = true;         ///< クリップが足を着いている間、足首をワールドに留める
	float lockRise = 0.01f;       ///< 足首がレストの高さからこれ以内に下りたら、足を着いたとみなす (m)
	float unlockRise = 0.02f;     ///< 着いた足首がレストの高さからこれより上がったら、足を離したとみなす (m)
	float maxLockDistance = 0.25f;   ///< 留めた点とアニメの足首が水平にこれより離れたら、次に着くまで留めない (m)
	float lockBlendSpeed = 6.0f;  ///< 離した足をアニメの足首へ寄せ終える速さ (1 秒あたりの割合)。0 ならすぐ戻す
};

/// @brief GameMemory に置く足の接地の状態 (108 byte)。高さはすべて足元 (配置の原点) から
struct FootPlacementState
{
	float pelvisOffset = 0.0f;              ///< 体を下げる量 (0 以下)
	float footOffset[2] = {0.0f, 0.0f};     ///< 足の下の地面の高さ
	float weight[2] = {0.0f, 0.0f};         ///< 足の IK の効き。地面の無い足は 0 へ寄せ、アニメのまま動かす
	float lockWeight[2] = {0.0f, 0.0f};     ///< 留めの残り。留めている間は 1、離すと lockBlendSpeed で 0 へ減る
	std::uint8_t grounded[2] = {0, 0};      ///< 足の下に地面が見つかったか
	std::uint8_t contact[2] = {0, 0};       ///< クリップが足を着いているか (lockRise と unlockRise の間は前の値のまま)
	std::uint8_t pinned[2] = {0, 0};        ///< 足首を lockPos へ寄せているか。離れすぎたら次に着くまで 0
	std::uint8_t pad[2] = {0, 0};
	Vec3 normal[2]{{0, 1, 0}, {0, 1, 0}};   ///< 足の下の地面の法線
	Vec3 lockPos[2]{};                      ///< 足首を置く点 (ワールド)。離した後はアニメの足首へ寄っていく
	Vec3 releaseOffset[2]{};                ///< 離した時の、留めた点からアニメの足首までの差
};

static_assert(std::is_trivially_copyable_v<FootPlacementState> && sizeof(FootPlacementState) == 108);

/// @brief 描くときの配置と IK の依頼。ik は足 2 本の接地のあとに足 2 本の留めが並ぶ (効きが 0 の依頼は何もしない)
struct FootPlacementPose
{
	static constexpr int kIkCount = 4;
	sgc::Mat4f world;                         ///< 体を下げた配置。drawModelPose と applyIkRequests に渡す
	animation::AnimIkRequest ik[kIkCount];
};

/// @brief レスト姿勢の足首の高さを入れた脚を作る。scale は描画の配置に掛ける大きさ (モデルの単位から m へ)
[[nodiscard]] inline FootLeg footLeg(const animation::AnimAsset& asset, int hip, int knee, int ankle, float scale = 1.0f)
{
	animation::AnimPose rest;
	animation::evaluatePose(asset, animation::AnimPoseParams{}, rest);
	const float height = animation::validNode(rest, ankle) ? animation::modelPosition(rest, ankle).y * scale : -1.0f;
	return {hip, knee, ankle, height};
}

namespace detail
{

[[nodiscard]] inline float moveToward(float value, float target, float maxStep) noexcept
{
	if (!(maxStep > 0.0f)) { return target; }
	const float d = target - value;
	return value + (d > maxStep ? maxStep : (d < -maxStep ? -maxStep : d));
}

[[nodiscard]] inline Vec3 matPosition(const sgc::Mat4f& m) noexcept { return {m.m[0][3], m.m[1][3], m.m[2][3]}; }

/// 足 i を留めるかを決め、離した足の留めた点をアニメの足首へ寄せる。ankle は IK を掛ける前のアニメの足首 (ワールド)
inline void stepFootLock(FootPlacementState& s, int i, const Vec3& ankle, float baseY, const FootLeg& leg,
                         const FootPlacementConfig& cfg, float dt) noexcept
{
	const float rise = ankle.y - baseY - leg.ankleHeight;
	const bool usable = cfg.lockFeet && leg.ankleHeight >= 0.0f && s.grounded[i] != 0;
	const bool contact = usable && rise <= (s.contact[i] != 0 ? cfg.unlockRise : cfg.lockRise);
	const Vec3 drawn{ankle.x, ankle.y + s.footOffset[i], ankle.z};   // 留めなければ足首が描かれる位置
	const bool wasPinned = s.pinned[i] != 0;
	if (contact && s.contact[i] == 0)
	{
		// 着いた時は、いま足首を描いている点に留めるので跳ばない (離している途中なら寄せている途中の点)
		if (!(s.lockWeight[i] > 0.0f)) { s.lockPos[i] = drawn; }
		s.pinned[i] = 1;
		s.lockWeight[i] = 1.0f;
	}
	s.contact[i] = contact ? 1 : 0;
	const float dx = s.lockPos[i].x - drawn.x;
	const float dz = s.lockPos[i].z - drawn.z;
	if (!contact || dx * dx + dz * dz > cfg.maxLockDistance * cfg.maxLockDistance) { s.pinned[i] = 0; }
	if (s.pinned[i] != 0) { return; }
	// 離した時の差を、効きの smoothstep で縮める。縮む速さは離す瞬間と寄せ終わりで 0 になる
	if (wasPinned) { s.releaseOffset[i] = s.lockPos[i] - drawn; }
	s.lockWeight[i] = moveToward(s.lockWeight[i], 0.0f, cfg.lockBlendSpeed * dt);
	const float w = s.lockWeight[i];
	s.lockPos[i] = drawn + s.releaseOffset[i] * (w * w * (3.0f - 2.0f * w));
}

} // namespace detail

/// @brief 両足の下の地面を測り、state を dt 秒ぶん寄せる。pose は IK を掛ける前の評価済みの姿勢
inline void stepFootPlacement(FootPlacementState& state, const CollisionWorld& world, const animation::AnimPose& pose,
                              const sgc::Mat4f& instanceWorld, const FootLeg (&legs)[2], const FootPlacementConfig& cfg,
                              float dt) noexcept
{
	const Vec3 base = detail::matPosition(instanceWorld);
	Vec3 ankle[2];
	float target[2] = {0.0f, 0.0f};
	for (int i = 0; i < 2; ++i)
	{
		ankle[i] = animation::boneWorld(pose, legs[i].ankle, instanceWorld).transformPoint({0, 0, 0});
		const RayHit hit = world.raycast({ankle[i].x, base.y + cfg.rayUp, ankle[i].z}, {0, -1, 0}, cfg.rayUp + cfg.rayDown, cfg.mask);
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
		detail::stepFootLock(state, i, ankle[i], base.y, legs[i], cfg, dt);
	}
}

/// @brief state から、体を下げた配置と足の IK の依頼を作る (draw でも update でも同じ値になる)
[[nodiscard]] inline FootPlacementPose footPlacementPose(const FootPlacementState& state, const sgc::Mat4f& instanceWorld,
                                                         const FootLeg (&legs)[2], const FootPlacementConfig& cfg) noexcept
{
	FootPlacementPose out;
	out.world = sgc::Mat4f::translation({0.0f, state.pelvisOffset, 0.0f}) * instanceWorld;
	const float baseY = detail::matPosition(instanceWorld).y;
	const std::uint8_t keepTip = cfg.alignToGround ? animation::kAnimIkKeepTip : std::uint8_t{0};
	for (int i = 0; i < 2; ++i)
	{
		out.ik[i] = animation::AnimIkRequest::foot(legs[i].hip, legs[i].knee, legs[i].ankle, baseY + state.footOffset[i],
		                                           state.normal[i], animation::kAnimIkWorldSpace | keepTip);
		out.ik[i].weight = state.weight[i];
		// 留めは接地の後に掛け、足裏の向きは接地が決めたまま保つ。膝はいまの曲げの向きへ出す
		out.ik[2 + i] = animation::AnimIkRequest::twoBone(legs[i].hip, legs[i].knee, legs[i].ankle, state.lockPos[i], {0, 0, 0},
		                                                  animation::kAnimIkKeepTip | animation::kAnimIkWorldSpace);
		out.ik[2 + i].weight = state.lockWeight[i] > 0.0f ? 1.0f : 0.0f;
	}
	return out;
}

} // namespace mitiru::action
