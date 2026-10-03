#pragma once

/// @file RagdollBlend.hpp
/// @brief アニメと ragdoll を切り替える状態 (GameMemory に置く POD) と、そこから出す部位の動かし方と混ぜる重み。
/// @details 状態は 4 つ。
///          Animated  : 全部位をアニメの姿勢へ速度で運ぶ (描くのはアニメのまま。体は物に当たって押す)。
///          HitReact  : 下半身はアニメのまま、上半身は関節の motor でアニメの向きを追う。motor は殴られた直後に弱く、
///                      hitSeconds をかけて強く戻る。描く姿勢は上半身だけ物理で、最後の 2 割でアニメへ戻す。
///          Dead      : motor を切って全身を倒れ込ませる。描く姿勢は全身が物理。
///          GettingUp : 倒れた体はそのまま、描く姿勢を getUpSeconds をかけて物理からアニメへ戻す。
///                      終わったフレームに ragdollAdvance が true を返すので、ゲームは部位をアニメの姿勢へ置き直す。
///          物理の世界 (JoltGameWorld) は窓口 (ADR 0054) で GameMemory と一緒に戻る。ここの状態も GameMemory にあり、
///          motor の強さと部位の動かし方は毎フレームこの状態から決め直すので、巻き戻してからの再実行も同じになる。

#include <algorithm>
#include <cstdint>
#include <span>
#include <type_traits>

#include <mitiru/physics/RagdollRig.hpp>

namespace mitiru::physics3d
{

enum class RagdollMode : std::uint8_t
{
	Animated = 0,
	HitReact = 1,
	Dead = 2,
	GettingUp = 3,
};

/// @brief 部位の動かし方
enum class RagdollPartDrive : std::uint8_t
{
	Follow = 0,   ///< アニメの姿勢へ速度で運ぶ (重力なし)
	Motor = 1,    ///< 関節の motor で親に対する向きを追う (重力あり。押されると崩れて戻る)
	Limp = 2,     ///< motor を切る (重力あり)
};

/// @brief 1 フレームの部位の動かし方と motor の強さ
struct RagdollDrive
{
	RagdollPartDrive part[kRagdollMaxParts] = {};
	float motorFrequency = 8.0f;   ///< motor のばねの固有振動数 (Hz)
	float motorDamping = 1.0f;     ///< 1 で振動しない
	float maxTorque = 600.0f;      ///< motor が出せる力のモーメント (N m)
	float limpFriction = 1.5f;     ///< motor を切った関節の摩擦 (N m)
};

/// @brief 切り替えの時間と強さ
struct RagdollTiming
{
	float hitSeconds = 0.9f;        ///< 被弾から立ち直るまで
	float hitMotorStart = 1.0f;     ///< 被弾の直後の motor の固有振動数 (Hz)
	float hitMotorEnd = 8.0f;       ///< 立ち直った時の motor の固有振動数 (Hz)
	float getUpSeconds = 0.8f;      ///< 物理の姿勢からアニメへ戻す時間
};

/// @brief GameMemory に置く状態 (8 バイト)
struct RagdollState
{
	RagdollMode mode = RagdollMode::Animated;
	std::uint8_t reserved[3] = {};
	float timer = 0.0f;             ///< 今の状態に入ってからの秒数
};
static_assert(std::is_trivially_copyable_v<RagdollState>);
static_assert(sizeof(RagdollState) == 8);

/// @brief 殴られた。上半身を崩す (倒れている間は何もしない)
inline void ragdollHit(RagdollState& s) noexcept
{
	if (s.mode == RagdollMode::Dead || s.mode == RagdollMode::GettingUp) { return; }
	s = {RagdollMode::HitReact, {}, 0.0f};
}

/// @brief 倒れた。全身を崩す
inline void ragdollKill(RagdollState& s) noexcept { s = {RagdollMode::Dead, {}, 0.0f}; }

/// @brief 起き上がり始める (倒れている時だけ)
inline void ragdollGetUp(RagdollState& s) noexcept
{
	if (s.mode == RagdollMode::Dead) { s = {RagdollMode::GettingUp, {}, 0.0f}; }
}

/// @brief 時間を進める。起き上がり終えたフレームに true (部位をアニメの姿勢へ置き直す合図)
[[nodiscard]] inline bool ragdollAdvance(RagdollState& s, float dt, const RagdollTiming& t = {}) noexcept
{
	s.timer += dt;
	if (s.mode == RagdollMode::HitReact && s.timer >= t.hitSeconds) { s = {}; }
	if (s.mode == RagdollMode::GettingUp && s.timer >= t.getUpSeconds)
	{
		s = {};
		return true;
	}
	return false;
}

namespace detail
{

[[nodiscard]] constexpr float smoothstep01(float x) noexcept
{
	const float c = std::clamp(x, 0.0f, 1.0f);
	return c * c * (3.0f - 2.0f * c);
}

} // namespace detail

/// @brief 状態から部位の動かし方を決める
[[nodiscard]] inline RagdollDrive ragdollDriveFor(const RagdollState& s, const RagdollRig& rig, const RagdollTiming& t = {})
{
	RagdollDrive d;
	for (std::size_t i = 0; i < rig.parts.size(); ++i)
	{
		RagdollPartDrive mode = RagdollPartDrive::Follow;
		if (s.mode == RagdollMode::HitReact && rig.parts[i].upper) { mode = RagdollPartDrive::Motor; }
		if (s.mode == RagdollMode::Dead || s.mode == RagdollMode::GettingUp) { mode = RagdollPartDrive::Limp; }
		d.part[i] = mode;
	}
	if (s.mode == RagdollMode::HitReact)
	{
		const float u = std::clamp(s.timer / std::max(t.hitSeconds, 1e-3f), 0.0f, 1.0f);
		d.motorFrequency = t.hitMotorStart + (t.hitMotorEnd - t.hitMotorStart) * u * u;
	}
	return d;
}

/// @brief 描く姿勢で部位ごとに物理をどれだけ使うか (blendRagdollPose の weights)
inline void ragdollWeightsFor(const RagdollState& s, const RagdollRig& rig, std::span<float> out,
                              const RagdollTiming& t = {})
{
	float upper = 0.0f, all = 0.0f;
	switch (s.mode)
	{
	case RagdollMode::Animated: break;
	case RagdollMode::HitReact:
		upper = 1.0f - detail::smoothstep01((s.timer / std::max(t.hitSeconds, 1e-3f) - 0.8f) / 0.2f);
		break;
	case RagdollMode::Dead: all = 1.0f; break;
	case RagdollMode::GettingUp: all = 1.0f - detail::smoothstep01(s.timer / std::max(t.getUpSeconds, 1e-3f)); break;
	}
	for (std::size_t i = 0; i < out.size(); ++i)
	{
		const bool isUpper = i < rig.parts.size() && rig.parts[i].upper;
		out[i] = std::max(all, isUpper ? upper : 0.0f);
	}
}

} // namespace mitiru::physics3d
