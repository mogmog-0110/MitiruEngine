#pragma once

/// @file AffectorPresets.hpp
/// @brief プリセットアフェクターファクトリ
///
/// ParticleAffectorの定番効果をワンライナーで生成する。
///
/// @code
/// sgc::ParticleSystem ps(500);
/// ps.addAffector(sgc::makeGravity(0.0f, 98.0f));
/// ps.addAffector(sgc::makeDrag(0.5f));
/// ps.addAffector(sgc::makeColorOverLife(1,0,0, 1,1,0));
/// @endcode

#include <cmath>
#include <functional>

#include "sgc/effects/ParticleSystem.hpp"

namespace sgc
{

/// @brief 重力アフェクターを生成する
/// @param gx X方向の重力加速度
/// @param gy Y方向の重力加速度
/// @return パーティクルアフェクター
[[nodiscard]] inline ParticleAffector makeGravity(float gx, float gy)
{
	return [gx, gy](Particle& p, float dt)
	{
		p.vx += gx * dt;
		p.vy += gy * dt;
	};
}

/// @brief ドラッグ（速度減衰）アフェクターを生成する
/// @param coefficient 減衰係数（0.0〜1.0、大きいほど強い減衰）
/// @return パーティクルアフェクター
[[nodiscard]] inline ParticleAffector makeDrag(float coefficient)
{
	return [coefficient](Particle& p, float dt)
	{
		const float factor = 1.0f - coefficient * dt;
		const float clampedFactor = (factor > 0.0f) ? factor : 0.0f;
		p.vx *= clampedFactor;
		p.vy *= clampedFactor;
	};
}

/// @brief 渦巻きアフェクターを生成する
/// @param cx 渦の中心X
/// @param cy 渦の中心Y
/// @param strength 渦の強さ（正で反時計回り）
/// @return パーティクルアフェクター
[[nodiscard]] inline ParticleAffector makeVortex(float cx, float cy, float strength)
{
	return [cx, cy, strength](Particle& p, float dt)
	{
		const float dx = p.x - cx;
		const float dy = p.y - cy;
		const float distSq = dx * dx + dy * dy;

		if (distSq < 1e-6f) return;

		const float dist = std::sqrt(distSq);
		// 接線方向（反時計回り）に力を加える
		const float tangentX = -dy / dist;
		const float tangentY = dx / dist;

		const float force = strength / dist;
		p.vx += tangentX * force * dt;
		p.vy += tangentY * force * dt;
	};
}

/// @brief 寿命に応じた色補間アフェクターを生成する
/// @param r0 開始色R
/// @param g0 開始色G
/// @param b0 開始色B
/// @param r1 終了色R
/// @param g1 終了色G
/// @param b1 終了色B
/// @return パーティクルアフェクター
[[nodiscard]] inline ParticleAffector makeColorOverLife(
	float r0, float g0, float b0,
	float r1, float g1, float b1)
{
	return [r0, g0, b0, r1, g1, b1](Particle& p, float /*dt*/)
	{
		if (p.maxLifetime <= 0.0f) return;

		const float t = 1.0f - (p.lifetime / p.maxLifetime);
		p.r = r0 + (r1 - r0) * t;
		p.g = g0 + (g1 - g0) * t;
		p.b = b0 + (b1 - b0) * t;
	};
}

/// @brief 乱流アフェクターを生成する
///
/// 疑似ノイズベースの揺らぎを速度に加える。
///
/// @param strength 乱流の強さ
/// @param frequency 乱流の周波数
/// @return パーティクルアフェクター
[[nodiscard]] inline ParticleAffector makeTurbulence(float strength, float frequency)
{
	return [strength, frequency](Particle& p, float dt)
	{
		// 簡易ハッシュベースの疑似ノイズ
		const float fx = p.x * frequency;
		const float fy = p.y * frequency;

		// sinベースの疑似ノイズ（高速・GPU非依存）
		const float noiseX = std::sin(fx * 12.9898f + fy * 78.233f) * 43758.5453f;
		const float noiseY = std::sin(fx * 93.9898f + fy * 67.345f) * 24634.6345f;

		// 小数部分のみ使用（-1〜1の範囲にマップ）
		const float nx = (noiseX - std::floor(noiseX)) * 2.0f - 1.0f;
		const float ny = (noiseY - std::floor(noiseY)) * 2.0f - 1.0f;

		p.vx += nx * strength * dt;
		p.vy += ny * strength * dt;
	};
}

} // namespace sgc
