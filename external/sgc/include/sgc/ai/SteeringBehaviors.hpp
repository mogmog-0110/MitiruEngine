#pragma once

/// @file SteeringBehaviors.hpp
/// @brief ステアリング行動（操舵行動）アルゴリズム
///
/// ゲームAIにおける基本的な操舵行動を純粋関数として提供する。
/// 全関数はステートレスで、力ベクトルを返す不変パターンを採用。
///
/// @code
/// sgc::ai::SteeringAgent<float> agent;
/// agent.position = {100.0f, 200.0f};
/// agent.velocity = {10.0f, 0.0f};
/// agent.maxSpeed = 100.0f;
/// agent.maxForce = 50.0f;
///
/// sgc::Vec2f target{500.0f, 300.0f};
/// auto force = sgc::ai::seek(agent, target);
/// agent = sgc::ai::applyForce(agent, force, 0.016f);
/// @endcode

#include <cmath>

#include "sgc/math/Vec2.hpp"
#include "sgc/types/Concepts.hpp"

namespace sgc::ai
{

/// @brief ステアリングエージェントの状態
/// @tparam T 浮動小数点型
template <FloatingPoint T>
struct SteeringAgent
{
	Vec2<T> position{};    ///< 現在位置
	Vec2<T> velocity{};    ///< 現在速度
	T maxSpeed{100};       ///< 最大速度
	T maxForce{50};        ///< 最大操舵力
	T mass{1};             ///< 質量

	[[nodiscard]] constexpr bool operator==(const SteeringAgent&) const noexcept = default;
};

/// @brief ベクトルを最大値で切り詰める
/// @tparam T 浮動小数点型
/// @param v 入力ベクトル
/// @param maxMag 最大の大きさ
/// @return 切り詰められたベクトル
template <FloatingPoint T>
[[nodiscard]] inline Vec2<T> truncate(const Vec2<T>& v, T maxMag) noexcept
{
	const T lenSq = v.lengthSquared();
	if (lenSq > maxMag * maxMag)
	{
		const T len = std::sqrt(lenSq);
		return v * (maxMag / len);
	}
	return v;
}

/// @brief ターゲットに向かう操舵力を計算する
/// @tparam T 浮動小数点型
/// @param agent エージェント
/// @param target 目標位置
/// @return 操舵力ベクトル
template <FloatingPoint T>
[[nodiscard]] inline Vec2<T> seek(const SteeringAgent<T>& agent, const Vec2<T>& target) noexcept
{
	const Vec2<T> desired = (target - agent.position).normalized() * agent.maxSpeed;
	return truncate(desired - agent.velocity, agent.maxForce);
}

/// @brief 脅威から逃げる操舵力を計算する
/// @tparam T 浮動小数点型
/// @param agent エージェント
/// @param threat 脅威の位置
/// @return 操舵力ベクトル
template <FloatingPoint T>
[[nodiscard]] inline Vec2<T> flee(const SteeringAgent<T>& agent, const Vec2<T>& threat) noexcept
{
	const Vec2<T> desired = (agent.position - threat).normalized() * agent.maxSpeed;
	return truncate(desired - agent.velocity, agent.maxForce);
}

/// @brief ターゲットに近づくにつれて減速する操舵力を計算する
/// @tparam T 浮動小数点型
/// @param agent エージェント
/// @param target 目標位置
/// @param slowingRadius 減速開始距離
/// @return 操舵力ベクトル
template <FloatingPoint T>
[[nodiscard]] inline Vec2<T> arrive(const SteeringAgent<T>& agent, const Vec2<T>& target, T slowingRadius) noexcept
{
	const Vec2<T> toTarget = target - agent.position;
	const T dist = toTarget.length();

	if (dist < T{0.0001})
	{
		return truncate(-agent.velocity, agent.maxForce);
	}

	T desiredSpeed = agent.maxSpeed;
	if (dist < slowingRadius)
	{
		desiredSpeed = agent.maxSpeed * (dist / slowingRadius);
	}

	const Vec2<T> desired = toTarget * (desiredSpeed / dist);
	return truncate(desired - agent.velocity, agent.maxForce);
}

/// @brief ランダムなさまよい行動の操舵力を計算する
/// @tparam T 浮動小数点型
/// @param agent エージェント
/// @param wanderRadius さまよい円の半径
/// @param wanderDistance さまよい円までの距離
/// @param wanderAngle さまよい角度（入出力パラメータ）
/// @param jitter 揺らぎの大きさ
/// @return 操舵力ベクトル
template <FloatingPoint T>
[[nodiscard]] inline Vec2<T> wander(
	const SteeringAgent<T>& agent,
	T wanderRadius,
	T wanderDistance,
	T& wanderAngle,
	T jitter) noexcept
{
	// 角度にランダムなジッターを加える（簡易的な擬似乱数）
	wanderAngle += jitter;

	// さまよい円上の目標点を計算する
	const Vec2<T> circleCenter = agent.velocity.lengthSquared() > T{0.0001}
		? agent.velocity.normalized() * wanderDistance
		: Vec2<T>{wanderDistance, T{0}};

	const Vec2<T> displacement{
		wanderRadius * std::cos(wanderAngle),
		wanderRadius * std::sin(wanderAngle)
	};

	const Vec2<T> wanderTarget = agent.position + circleCenter + displacement;
	return seek(agent, wanderTarget);
}

/// @brief 移動中のターゲットを予測追跡する操舵力を計算する
/// @tparam T 浮動小数点型
/// @param agent 追跡するエージェント
/// @param target 追跡対象のエージェント
/// @return 操舵力ベクトル
template <FloatingPoint T>
[[nodiscard]] inline Vec2<T> pursue(const SteeringAgent<T>& agent, const SteeringAgent<T>& target) noexcept
{
	const Vec2<T> toTarget = target.position - agent.position;
	const T dist = toTarget.length();

	// 予測時間は距離/速度で概算する
	const T speed = agent.velocity.length();
	const T lookAhead = (speed > T{0.0001}) ? (dist / speed) : T{1};

	const Vec2<T> futurePos = target.position + target.velocity * lookAhead;
	return seek(agent, futurePos);
}

/// @brief 移動中の脅威から回避する操舵力を計算する
/// @tparam T 浮動小数点型
/// @param agent 回避するエージェント
/// @param threat 脅威のエージェント
/// @return 操舵力ベクトル
template <FloatingPoint T>
[[nodiscard]] inline Vec2<T> evade(const SteeringAgent<T>& agent, const SteeringAgent<T>& threat) noexcept
{
	const Vec2<T> toThreat = threat.position - agent.position;
	const T dist = toThreat.length();

	const T speed = agent.velocity.length();
	const T lookAhead = (speed > T{0.0001}) ? (dist / speed) : T{1};

	const Vec2<T> futurePos = threat.position + threat.velocity * lookAhead;
	return flee(agent, futurePos);
}

/// @brief 操舵力をエージェントに適用し、更新されたエージェントを返す（不変パターン）
/// @tparam T 浮動小数点型
/// @param agent 現在のエージェント
/// @param force 適用する操舵力
/// @param dt タイムステップ（秒）
/// @return 更新されたエージェント
template <FloatingPoint T>
[[nodiscard]] inline SteeringAgent<T> applyForce(const SteeringAgent<T>& agent, const Vec2<T>& force, T dt) noexcept
{
	const Vec2<T> acceleration = force / agent.mass;
	Vec2<T> newVelocity = agent.velocity + acceleration * dt;
	newVelocity = truncate(newVelocity, agent.maxSpeed);
	const Vec2<T> newPosition = agent.position + newVelocity * dt;

	return SteeringAgent<T>{
		newPosition,
		newVelocity,
		agent.maxSpeed,
		agent.maxForce,
		agent.mass
	};
}

} // namespace sgc::ai
