#pragma once

/// @file CameraRig.hpp
/// @brief 三人称のカメラ。ばねの腕、壁への押し込み (球の掃引) と遅れて伸びる戻り、ロックオンの構図、なめらかな追従。
/// @details 状態 CameraRigState は flat POD で GameMemory に置く。同じ入力なら同じバイナリの上で bit 一致する。
///          出力の CameraView は Screen::camera3D(eye, target, fovDeg, rollDeg) にそのまま渡せる。
///          向きはリグの上 (CameraRigState::up) と北 (yaw 0 の向き、既定は +Z) の座標系で、yaw (上まわり) と
///          pitch (正で見下ろす) で持つ。右スティックを右へ倒すと、視線は画面の右 (cross(視線, 上)) の側へ回る。
///
///          上の扱い: CameraRigInput::up を渡すと、リグの上はそちらへ upFollowRate で寄る。上が回るときは北も
///          同じ回転で運ぶので、視線は座標系ごと回り、上が視線の向きを通り過ぎても裏返らない。
///          上を渡さなければ座標系はワールドのまま (+Y と +Z) で、計算もワールド固定のときと同じになる。
///
///          壁の扱い: キャラの体の中 (足元の少し上) から腕の根元まで掃引し、そこから目の位置まで掃引する。
///          当たったら腕はその場で縮み、壁が無くなっても recoverDelay 秒は待ってからゆっくり伸びる
///          (角を回るたびにカメラが前後に跳ねない)。どちらの掃引も目の球を通すので、目は地形の中に入らない。

#include <cmath>
#include <cstdint>
#include <type_traits>

#include <mitiru/action/CollisionWorld.hpp>

namespace mitiru::action
{

struct CameraRigConfig
{
	float distance        = 4.5f;   ///< 腕の長さ (m)
	float minDistance     = 0.8f;
	float pivotHeight     = 1.5f;   ///< 足元から腕の根元まで
	float defaultPitchDeg = 15.0f;
	float minPitchDeg     = -30.0f;
	float maxPitchDeg     = 70.0f;
	float yawSpeedDeg     = 180.0f; ///< スティックを倒しきったときの角速度 (度/秒)
	float pitchSpeedDeg   = 120.0f;
	float followTime      = 0.08f;  ///< 腕の根元が追いつくまでのおよその秒数
	float fovDeg          = 55.0f;

	float         probeRadius     = 0.25f;  ///< 目の球の半径
	float         collisionMargin = 0.05f;  ///< 当たった位置からさらに手前へ引く量
	float         recoverDelay    = 0.25f;  ///< 壁が無くなってから腕が伸び始めるまで (秒)
	float         recoverRate     = 4.0f;   ///< 伸びる速さ (1/秒、指数)
	float         recoverMaxSpeed = 6.0f;   ///< 伸びる速さの上限 (m/秒)
	std::uint32_t collisionMask   = kAllLayers;

	float lockWeight      = 0.35f;  ///< 注視点を自分から相手へ寄せる割合 (重み付きの中点)
	float lockScreenRatio = 0.6f;   ///< 2 人を収める大きさ (画面の高さに対する割合)
	float lockMaxDistance = 10.0f;
	float lockBlendTime   = 0.25f;  ///< ロックオンの入り切りにかける秒数
	float lockYawRate     = 6.0f;   ///< 相手の方へ回り込む速さ (1/秒、指数)
	float lockPitchDeg    = 10.0f;

	float upFollowRate    = 6.0f;   ///< リグの上を CameraRigInput::up へ寄せる速さ (1/秒、指数)。0 以下ですぐ合わせる
};

struct CameraRigInput
{
	Vec3         follow{};      ///< 追うキャラの足元
	Vec3         lockTarget{};  ///< ロックオンの相手の中心
	float        orbitX = 0.0f; ///< 右スティック (-1..1)。正で右へ回る
	float        orbitY = 0.0f; ///< 正で見上げる
	std::uint8_t lockOn = 0;
	std::uint8_t pad[3]{};
	Vec3         up{};          ///< キャラの上。0 ならワールドの +Y
	float        rollDeg = 0.0f; ///< 視線まわりに足す傾き (度)。正で画面の右へ傾く (camera3D と同じ向き)
};

struct CameraRigState
{
	SpringV3      focus{};  ///< 注視点 (腕の根元)。追従とロックオンの入り切りをこのばねでならす
	float         yaw        = 0.0f;
	float         pitch      = 0.0f;
	float         lock       = 0.0f;  ///< ロックオンの効き (0..1)
	float         arm        = 0.0f;  ///< 衝突で縮んだあとの、今の腕の長さ
	float         clearTime  = 0.0f;  ///< 腕を今より伸ばせる状態が続いている秒数
	std::uint32_t initialized = 0;
	Vec3          up{0.0f, 1.0f, 0.0f};     ///< リグの上 (ならしたもの)
	Vec3          north{0.0f, 0.0f, 1.0f};  ///< yaw 0 の向き。up に直交する
};

struct CameraView
{
	Vec3  eye{};
	Vec3  target{0.0f, 0.0f, 1.0f};
	float fovDeg = 55.0f;
	Vec3  up{0.0f, 1.0f, 0.0f};  ///< 傾きを含む画面の上の目安 (視線に直交とは限らない)
	float rollDeg = 0.0f;        ///< up を camera3D の rollDeg (ワールドの +Y を基準にした傾き) に直したもの
};

static_assert(std::is_trivially_copyable_v<CameraRigConfig>);
static_assert(std::is_trivially_copyable_v<CameraRigInput>);
static_assert(std::is_trivially_copyable_v<CameraRigState>);
static_assert(std::is_trivially_copyable_v<CameraView>);

/// @brief yaw / pitch の視線 (目から注視点への向き)
[[nodiscard]] inline Vec3 cameraRigForward(float yaw, float pitch) noexcept
{
	return {std::sin(yaw) * std::cos(pitch), -std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
}

/// @brief 視線 forward に対する画面の右 (エンジンの camera3D と同じ cross(視線, 上))
[[nodiscard]] inline Vec3 cameraRigRight(const Vec3& forward, const Vec3& up = Vec3{0.0f, 1.0f, 0.0f}) noexcept
{
	const Vec3 fallback = up == Vec3{0.0f, 1.0f, 0.0f} ? Vec3{-1.0f, 0.0f, 0.0f} : anyPerpendicular(up);
	return normalizeOr(cross(forward, up), fallback);
}

/// @brief リグの座標系 (右手側 cross(up, north)、up、north) の成分 local をワールドの向きへ直す
[[nodiscard]] inline Vec3 cameraRigToWorld(const CameraRigState& s, const Vec3& local) noexcept
{
	if (s.up == Vec3{0.0f, 1.0f, 0.0f} && s.north == Vec3{0.0f, 0.0f, 1.0f}) { return local; }
	return cross(s.up, s.north) * local.x + s.up * local.y + s.north * local.z;
}

/// @brief cameraRigToWorld の逆
[[nodiscard]] inline Vec3 cameraRigToLocal(const CameraRigState& s, const Vec3& v) noexcept
{
	if (s.up == Vec3{0.0f, 1.0f, 0.0f} && s.north == Vec3{0.0f, 0.0f, 1.0f}) { return v; }
	return {dot(v, cross(s.up, s.north)), dot(v, s.up), dot(v, s.north)};
}

/// @brief リグの今の視線 (ワールド)
[[nodiscard]] inline Vec3 cameraRigForward(const CameraRigState& s) noexcept
{
	return cameraRigToWorld(s, cameraRigForward(s.yaw, s.pitch));
}

/// @brief 視線の向きと画面の上の目安 up から、Screen::camera3D の rollDeg を求める
/// @details camera3D は基準の上 (+Y、真上か真下を向くときは +Z) を視線まわりに回すので、同じ計算で基準を選ぶ。
[[nodiscard]] inline float cameraRollDeg(const Vec3& eye, const Vec3& target, const Vec3& up) noexcept
{
	Vec3        f{target.x - eye.x, target.y - eye.y, target.z - eye.z};
	const float fl = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
	if (fl < 1e-6f) { return 0.0f; }
	f = {f.x / fl, f.y / fl, f.z / fl};
	const Vec3 base = (f.y > 0.999f || f.y < -0.999f) ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
	const Vec3 b    = normalizeOr(projectOnPlane(base, f), anyPerpendicular(f));
	return std::atan2(dot(up, cross(f, b)), dot(up, b)) / kDegToRad;
}

} // namespace mitiru::action

#include <mitiru/action/detail/CameraRig_impl.hpp>

namespace mitiru::action
{

/// @brief 1 フレーム進めて、描く視点を返す
inline CameraView updateCameraRig(CameraRigState& s, const CameraRigConfig& c, const CameraRigInput& in,
                                  const CollisionWorld& world, float dt)
{
	namespace rd = camera_rig_detail;
	dt = clampf(std::isfinite(dt) ? dt : 0.0f, 0.0f, 0.1f);
	if (s.initialized == 0) { rd::snap(s, c, in); }
	const float lockGoal = in.lockOn != 0 ? 1.0f : 0.0f;
	const float lockStep = c.lockBlendTime > 1e-4f ? dt / c.lockBlendTime : 1.0f;
	s.lock = s.lock < lockGoal ? minf(lockGoal, s.lock + lockStep) : maxf(lockGoal, s.lock - lockStep);

	rd::followUp(s, c, in, dt);
	rd::orbit(s, c, in, dt);
	const Vec3  pivotGoal = in.follow + cameraRigToWorld(s, Vec3{0.0f, c.pivotHeight, 0.0f});
	const Vec3  mid       = lerp(pivotGoal, in.lockTarget, c.lockWeight);
	const Vec3  focus     = s.focus.update(lerp(pivotGoal, mid, s.lock), c.followTime, dt);
	const float armLength = lerpf(c.distance, rd::lockDistance(c, pivotGoal, in.lockTarget, mid), s.lock);
	const Vec3  forward   = cameraRigForward(s);
	const Vec3  eye = rd::placeEye(s, c, world, in.follow, focus, focus - forward * maxf(armLength, c.minDistance), dt);
	const Vec3  look = lengthSq(focus - eye) > 1e-6f ? focus : eye + forward * 0.1f;  // 目と注視点が重なると視線が決まらない
	return rd::makeView(s, in, eye, look, c.fovDeg);
}

} // namespace mitiru::action
