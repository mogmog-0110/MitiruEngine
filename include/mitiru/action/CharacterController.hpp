#pragma once

/// @file CharacterController.hpp
/// @brief 三角形の地形を歩くカプセルのキャラクター (collide-and-slide、段差、坂の上限、地面への吸着、動く足場)。
/// @details 状態 CharacterState は flat POD で GameMemory に置く。地形は CollisionWorld に同じフレームで問い合わせる。
///          乱数も時刻も読まないので、同じ入力と同じ地形なら同じバイナリの上で bit 一致する。
///
///          1 tick の流れ (stepCharacter):
///          1. 足場に乗っていれば、足場が動いた分だけ運ぶ (carryDelta)
///          2. 重なっていれば押し出す
///          3. 速度を決める。接地中は入力の水平速度を地面の傾きに沿わせ、空中は重力を足す
///          4. 移動をサブステップに割り、カプセルを掃引して滑らせる。低い壁は段差として乗り越える
///          5. 下へ掃引して地面に吸い付け、接地を決め直す
///
///          坂か壁かは、接触の法線でなく三角形の面法線 (surfaceNormal) で決める。段差の角に触れたとき、
///          接触の法線は斜めでも、床の三角形に乗っていれば床として扱える。

#include <cmath>
#include <cstdint>
#include <type_traits>

#include <mitiru/action/CollisionWorld.hpp>

namespace mitiru::action
{

/// @brief CharacterState::events のビット (その tick に起きたこと)
namespace charevent
{
inline constexpr std::uint32_t kLanded       = 1u << 0;
inline constexpr std::uint32_t kJumped       = 1u << 1;
inline constexpr std::uint32_t kLeftGround   = 1u << 2;
inline constexpr std::uint32_t kHitWall      = 1u << 3;
inline constexpr std::uint32_t kHitCeiling   = 1u << 4;
inline constexpr std::uint32_t kSteppedUp    = 1u << 5;
inline constexpr std::uint32_t kDepenetrated = 1u << 6;
} // namespace charevent

/// @brief 調整値。単位はメートルと秒
struct CharacterConfig
{
	float         radius          = 0.35f;
	float         height          = 1.8f;   ///< 両端の半球を含む全長
	float         skinWidth       = 0.02f;  ///< 面との隙間。毎回の掃引が重なりから始まらないように空ける
	float         maxSlopeDeg     = 45.0f;  ///< これより急な面には立てない (壁として滑る)
	float         stepHeight      = 0.35f;  ///< 乗り越えられる段差
	float         snapDistance    = 0.3f;   ///< 下り坂や段差を降りるときに地面へ吸い付ける距離
	float         maxFallSpeed    = 50.0f;
	Vec3          gravity{0.0f, -25.0f, 0.0f};
	float         maxStepFraction = 0.5f;   ///< 1 サブステップの移動の上限 (半径に対する割合)
	std::int32_t  maxSubsteps     = 8;
	std::int32_t  maxSlideIterations = 4;
	std::uint32_t solidMask       = kAllLayers;
};

/// @brief GameMemory に置く状態。position は足元 (カプセルの一番下)
struct CharacterState
{
	Vec3          position{};
	Vec3          velocity{};
	Vec3          groundNormal{0.0f, 1.0f, 0.0f};
	Vec3          platformVelocity{};  ///< 乗っている足場の速度。足場を離れるときに速度へ足す
	std::uint32_t groundBody     = kLevelBody;
	std::uint32_t groundTriangle = 0;
	std::uint32_t events         = 0;  ///< この tick の charevent::k*
	std::uint8_t  grounded       = 0;
	std::uint8_t  pad[3]{};
};

/// @brief 1 tick の入力
struct CharacterInput
{
	Vec3  moveVelocity{};    ///< 望む水平速度 (m/s)。上下の成分は捨てる
	float jumpSpeed = 0.0f;  ///< 0 より大きければ、接地中のこの tick にこの速さで跳ぶ
	/// この tick の上。0 なら CharacterConfig::gravity の逆。0 でなければ、重力は大きさ |gravity| のまま
	/// -up へ向き、段差・坂・吸着・天井と壁の区別もこの上で決める (ループ、壁走り、小さな星)
	Vec3  up{};
};

static_assert(std::is_trivially_copyable_v<CharacterConfig>);
static_assert(std::is_trivially_copyable_v<CharacterState>);
static_assert(std::is_trivially_copyable_v<CharacterInput>);

/// @brief 足元 feet に立つカプセル
[[nodiscard]] inline Capsule characterCapsule(const CharacterConfig& c, const Vec3& feet, const Vec3& up) noexcept
{
	const float r = c.radius;
	return {feet + up * r, feet + up * maxf(r, c.height - r), r};
}

} // namespace mitiru::action

#include <mitiru/action/detail/CharacterController_impl.hpp>

namespace mitiru::action
{

/// @brief 1 tick 進める
inline void stepCharacter(CharacterState& s, const CharacterConfig& c, const CollisionWorld& world,
                          const CharacterInput& in, float dt)
{
	namespace cd = character_detail;
	s.events = 0;
	if (!(dt > 0.0f)) { return; }
	const bool custom      = lengthSq(in.up) > 1e-12f;
	const Vec3 up          = custom ? normalizeOr(in.up, Vec3{0.0f, 1.0f, 0.0f})
	                                : normalizeOr(-c.gravity, Vec3{0.0f, 1.0f, 0.0f});
	const float gravityUp  = custom ? -length(c.gravity) : dot(c.gravity, up);
	if (custom) { cd::dropUnwalkableGround(s, c, up); }
	const bool wasGrounded = s.grounded != 0;

	cd::carryWithPlatform(s, world, dt);
	cd::depenetrate(s, c, world, up);
	const bool jumped = cd::integrateVelocity(s, c, in, up, gravityUp, dt);

	const float maxStep = maxf(1e-3f, c.radius * c.maxStepFraction);
	const int   maxSub  = c.maxSubsteps < 1 ? 1 : c.maxSubsteps;
	const float travel  = length(s.velocity) * dt;
	const int   steps   = static_cast<int>(minf(static_cast<float>(maxSub), maxf(1.0f, std::ceil(travel / maxStep))));
	const Vec3  delta   = s.velocity * (dt / static_cast<float>(steps));
	for (int i = 0; i < steps; ++i) { cd::slideMove(s, c, world, delta, up); }

	cd::settleOnGround(s, c, world, up, wasGrounded && !jumped);
	if (wasGrounded && s.grounded == 0) { cd::leaveGround(s); }
	if (!wasGrounded && s.grounded != 0) { s.events |= charevent::kLanded; }
}

} // namespace mitiru::action
