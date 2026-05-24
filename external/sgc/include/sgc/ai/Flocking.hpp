#pragma once

/// @file Flocking.hpp
/// @brief 群れ行動（フロッキング）アルゴリズム
///
/// Craig Reynoldsのボイドモデルに基づく3つの基本行動
/// （分離・整列・結合）と、それらを統合するflock関数を提供する。
///
/// @code
/// std::vector<sgc::ai::SteeringAgent<float>> boids = /* ... */;
/// for (auto& boid : boids)
/// {
///     auto force = sgc::ai::flock(boid, boids, 100.0f);
///     boid = sgc::ai::applyForce(boid, force, dt);
/// }
/// @endcode

#include <cstddef>
#include <span>

#include "sgc/ai/SteeringBehaviors.hpp"

namespace sgc::ai
{

/// @brief フロッキングの重み係数
struct FlockWeights
{
	float separation = 1.5f;  ///< 分離の重み
	float alignment = 1.0f;   ///< 整列の重み
	float cohesion = 1.0f;    ///< 結合の重み

	[[nodiscard]] constexpr bool operator==(const FlockWeights&) const noexcept = default;
};

/// @brief 近隣のエージェントから離れる分離力を計算する
/// @tparam T 浮動小数点型
/// @param agent 対象エージェント
/// @param neighbors 近隣エージェントのスパン
/// @param desiredSeparation 理想的な分離距離
/// @return 分離力ベクトル
template <FloatingPoint T>
[[nodiscard]] inline Vec2<T> separation(
	const SteeringAgent<T>& agent,
	std::span<const SteeringAgent<T>> neighbors,
	T desiredSeparation) noexcept
{
	Vec2<T> steer{};
	int count = 0;

	for (const auto& other : neighbors)
	{
		const Vec2<T> diff = agent.position - other.position;
		const T distSq = diff.lengthSquared();

		if (distSq > T{0.0001} && distSq < desiredSeparation * desiredSeparation)
		{
			const T dist = std::sqrt(distSq);
			steer = steer + diff * (T{1} / dist);
			++count;
		}
	}

	if (count > 0)
	{
		steer = steer / static_cast<T>(count);
		if (steer.lengthSquared() > T{0.0001})
		{
			steer = steer.normalized() * agent.maxSpeed - agent.velocity;
			steer = truncate(steer, agent.maxForce);
		}
	}

	return steer;
}

/// @brief 近隣エージェントの平均速度に合わせる整列力を計算する
/// @tparam T 浮動小数点型
/// @param agent 対象エージェント
/// @param neighbors 近隣エージェントのスパン
/// @return 整列力ベクトル
template <FloatingPoint T>
[[nodiscard]] inline Vec2<T> alignment(
	const SteeringAgent<T>& agent,
	std::span<const SteeringAgent<T>> neighbors) noexcept
{
	Vec2<T> avgVelocity{};
	int count = 0;

	for (const auto& other : neighbors)
	{
		const T distSq = (agent.position - other.position).lengthSquared();
		if (distSq > T{0.0001})
		{
			avgVelocity = avgVelocity + other.velocity;
			++count;
		}
	}

	if (count > 0)
	{
		avgVelocity = avgVelocity / static_cast<T>(count);
		if (avgVelocity.lengthSquared() > T{0.0001})
		{
			avgVelocity = avgVelocity.normalized() * agent.maxSpeed;
			return truncate(avgVelocity - agent.velocity, agent.maxForce);
		}
	}

	return Vec2<T>{};
}

/// @brief 近隣エージェントの重心に向かう結合力を計算する
/// @tparam T 浮動小数点型
/// @param agent 対象エージェント
/// @param neighbors 近隣エージェントのスパン
/// @return 結合力ベクトル
template <FloatingPoint T>
[[nodiscard]] inline Vec2<T> cohesion(
	const SteeringAgent<T>& agent,
	std::span<const SteeringAgent<T>> neighbors) noexcept
{
	Vec2<T> center{};
	int count = 0;

	for (const auto& other : neighbors)
	{
		const T distSq = (agent.position - other.position).lengthSquared();
		if (distSq > T{0.0001})
		{
			center = center + other.position;
			++count;
		}
	}

	if (count > 0)
	{
		center = center / static_cast<T>(count);
		return seek(agent, center);
	}

	return Vec2<T>{};
}

/// @brief 3つの群れ行動を統合したフロッキング力を計算する
/// @tparam T 浮動小数点型
/// @param agent 対象エージェント
/// @param neighbors 全エージェントのスパン
/// @param neighborRadius 近隣とみなす半径
/// @param weights 各行動の重み係数
/// @return 統合されたフロッキング力
template <FloatingPoint T>
[[nodiscard]] inline Vec2<T> flock(
	const SteeringAgent<T>& agent,
	std::span<const SteeringAgent<T>> neighbors,
	T neighborRadius,
	const FlockWeights& weights = {}) noexcept
{
	// 近隣エージェントをフィルタリングする
	// スタックに小さなバッファを用意し、超えたら動的確保
	std::vector<SteeringAgent<T>> nearby;
	nearby.reserve(neighbors.size());

	const T radiusSq = neighborRadius * neighborRadius;
	for (const auto& other : neighbors)
	{
		const T distSq = (agent.position - other.position).lengthSquared();
		if (distSq > T{0.0001} && distSq < radiusSq)
		{
			nearby.push_back(other);
		}
	}

	if (nearby.empty())
	{
		return Vec2<T>{};
	}

	const std::span<const SteeringAgent<T>> nearbySpan{nearby};

	const Vec2<T> sep = separation(agent, nearbySpan, neighborRadius * T{0.5});
	const Vec2<T> ali = alignment(agent, nearbySpan);
	const Vec2<T> coh = cohesion(agent, nearbySpan);

	return sep * static_cast<T>(weights.separation)
		 + ali * static_cast<T>(weights.alignment)
		 + coh * static_cast<T>(weights.cohesion);
}

} // namespace sgc::ai
