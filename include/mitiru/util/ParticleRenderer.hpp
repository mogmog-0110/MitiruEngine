#pragma once

/// @file ParticleRenderer.hpp
/// @brief sgc::ParticleSystem の描画ヘルパー

#include <sgc/effects/ParticleSystem.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>
#include <mitiru/core/Screen.hpp>

namespace mitiru::util
{
	/// @brief Screen に丸パーティクルを描画する
	/// @param screen 描画先の Screen
	/// @param system 描画するパーティクルシステム
	inline void drawParticles(Screen& screen, const sgc::ParticleSystem& system)
	{
		for (const auto& p : system.activeParticles())
		{
			screen.drawCircle(
				sgc::Vec2f{ p.x, p.y }, p.size,
				sgc::Colorf{ p.r, p.g, p.b, p.a });
		}
	}

	/// @brief Screen に矩形パーティクルを描画する
	/// @param screen 描画先の Screen
	/// @param system 描画するパーティクルシステム
	inline void drawParticlesAsRects(Screen& screen, const sgc::ParticleSystem& system)
	{
		for (const auto& p : system.activeParticles())
		{
			screen.drawRect(
				sgc::Rectf{ p.x - p.size * 0.5f, p.y - p.size * 0.5f, p.size, p.size },
				sgc::Colorf{ p.r, p.g, p.b, p.a });
		}
	}

} // namespace mitiru::util
