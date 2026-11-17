#pragma once

/// @file RopeSwing.hpp
/// @brief 綱で振る・引く運動の純関数 (GAME_REQUESTS #90)。状態は引数で受けて結果を返すだけで、何も覚えない。
/// @details 触手・フック・grapple のように、固定点 (pivot) から綱で体を吊って振る運動に使う。
///          綱は伸びないが、たるむのは許す (綱より近いときは拘束しない)。向きは up (既定は +Y) で決め、重力は up の逆へ働く。
///          式は OOZE DRIVE の `swingVelocity`、`holdRope`、`longestRope`、`zipVelocity` と演算の順まで同じ。
///          同じ入力から同じ bit が出る (up が (0,1,0) のときの、符号つきの 0 を除く)。

#include <cfloat>
#include <cstdint>
#include <type_traits>

#include <mitiru/action/ActionMath.hpp>
#include <mitiru/action/CollisionWorld.hpp>

namespace mitiru::action
{

struct RopeSwingParams
{
	Vec3  up{0.0f, 1.0f, 0.0f};
	float gravity     = 0.0f;     ///< 下向きの加速 (m/s²)。重力の強さに「ぶら下がりの重さ」の倍率を掛けた値を渡す
	float pushAccel   = 0.0f;     ///< pushDir の向きへ足す加速。壁から離す押しなど
	float pumpAccel   = 0.0f;     ///< 体が pivot の真下にいるとき、速度の向きへ足す加速 (真横から上では 0、真下で最大)
	float steerAccel  = 0.0f;     ///< 操作の向きへ足す加速 (綱に垂直で、up に垂直な面へ落とした成分)
	float tangentKeep = 0.0f;     ///< 綱がぴんと張って外へ向く速さを、接線の速さとして残す割合 (0 で全部消す、1 で速さを保つ)
	float reelSpeed   = 0.0f;     ///< 綱より遠いときに、綱の長さへ戻す速さの上限
	float maxSpeed    = FLT_MAX;
	float slack       = 0.05f;    ///< 綱の長さのまわりの許容。距離の誤差で拘束が入り切りするのを避ける
};

struct RopeSwingInput
{
	Vec3  center{};
	Vec3  velocity{};
	Vec3  pivot{};
	float rope = 0.0f;            ///< 綱の長さ
	Vec3  steer{};                ///< 操作の向き (世界)。長さは 0 から 1
	Vec3  pushDir{};              ///< pushAccel を掛ける向き
	bool  taut = true;            ///< false なら綱の拘束を掛けない (重力、押し、操作の加速だけ)
};

static_assert(std::is_trivially_copyable_v<RopeSwingParams>);
static_assert(std::is_trivially_copyable_v<RopeSwingInput>);

/// @brief dt 秒後の速度。重力、押し、真下を通るときの加速 (pump)、操作の加速を足してから、綱の外向きの成分を消す。
/// @details 綱より遠ければ (距離 > rope + slack) 外向きを消して reelSpeed 以下で引き寄せ、綱の上 (許容の中) で外へ向いていれば
///          tangentKeep の割合だけ速さを接線へ回す。綱より近いときは何もしない (たるみ)。最後に maxSpeed へ切る
[[nodiscard]] inline Vec3 ropeSwingVelocity(const RopeSwingParams& p, const RopeSwingInput& in, float dt) noexcept
{
	if (!(dt > 0.0f)) { return in.velocity; }
	const Vec3  r    = in.center - in.pivot;
	const float dist = length(r);
	const Vec3  n    = normalizeOr(r, -p.up);
	Vec3 v = in.velocity + p.up * (-p.gravity * dt) + in.pushDir * (p.pushAccel * dt);
	const float below = maxf(0.0f, -dot(n, p.up));
	v += normalizeOr(projectOnPlane(v, n), Vec3{}) * (p.pumpAccel * below * dt);
	v += projectOnPlane(projectOnPlane(in.steer, p.up), n) * (p.steerAccel * dt);
	const float radial = dot(v, n);
	if (in.taut && dist > in.rope + p.slack)
	{
		const float pull = minf(p.reelSpeed, (dist - in.rope) / dt);
		v = projectOnPlane(v, n) - n * maxf(pull, -radial);
	}
	else if (in.taut && dist >= in.rope - p.slack && radial > 0.0f)
	{
		const Vec3  along = projectOnPlane(v, n);
		const float kept  = lerpf(length(along), length(v), p.tangentKeep);
		v = normalizeOr(along, Vec3{}) * kept;
	}
	return clampLength(v, p.maxSpeed);
}

/// @brief 綱を張ったまま pivot の真下へ床までの余裕を残せる、いちばん長い綱。床が無ければ probe 先まで続くものとして測る
/// @param radius 体の半径。clearance は床との間に残したい隙間。床が近すぎれば負を返すので、呼ぶ側で 0 以下を「綱が張れない」として扱う
[[nodiscard]] inline float longestRope(const CollisionWorld& world, const Vec3& pivot, const Vec3& up, float radius,
                                       float clearance, float probe = 400.0f, std::uint32_t mask = kAllLayers)
{
	const RayHit floor   = world.raycast(pivot, -up, probe, mask);
	const float  pivotH  = dot(pivot, up);
	const float  floorH  = floor.hit ? dot(floor.point, up) : pivotH - probe;
	return pivotH - floorH - radius - clearance;
}

/// @brief 綱より遠くへ出た体を、壁にめり込まないよう滑らせながら綱の長さまで戻した位置。1 tick に戻す距離は maxStep まで
/// @details 戻す向きの掃引が壁に当たったら、その面に沿って slideSteps 回まで続ける。外へ出ていなければ center のまま
[[nodiscard]] inline Vec3 holdRopeLength(const CollisionWorld& world, const Vec3& center, const Vec3& pivot, float rope,
                                         float radius, float skin, float maxStep, int slideSteps = 3,
                                         std::uint32_t mask = kAllLayers)
{
	const Vec3  r    = center - pivot;
	const float dist = length(r);
	if (dist <= rope || !(dist > 0.0f)) { return center; }
	Vec3 pos   = center;
	Vec3 delta = clampLength(r * (rope / dist) - r, maxStep);
	for (int i = 0; i < slideSteps && lengthSq(delta) > 1e-6f * 1e-6f; ++i)
	{
		const SweepHit hit = world.sphereCast(pos, radius, delta, mask);
		if (!hit.hit) { return pos + delta; }
		pos += delta * hit.fraction + hit.normal * skin;
		delta = projectOnPlane(delta * (1.0f - hit.fraction), hit.normal);
	}
	return pos;
}

/// @brief goal へ timeLeft 秒で着くための速度 (残りが 1 tick より短ければ 1 tick で着く速さ)。maxSpeed で切る
[[nodiscard]] inline Vec3 pullVelocity(const Vec3& center, const Vec3& goal, float timeLeft, float dt,
                                       float maxSpeed) noexcept
{
	const float left = maxf(timeLeft, dt);
	return clampLength((goal - center) * (1.0f / left), maxSpeed);
}

/// @brief 引き寄せにかける秒数。distance / range が 0 なら tMin、1 以上なら tMax
[[nodiscard]] inline float pullDuration(float distance, float range, float tMin, float tMax) noexcept
{
	const float share = range > 0.0f ? saturate(distance / range) : (distance > 0.0f ? 1.0f : 0.0f);
	return lerpf(tMin, tMax, share);
}

}  // namespace mitiru::action
